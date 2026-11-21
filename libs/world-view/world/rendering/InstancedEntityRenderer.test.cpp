// The parts item draw path on the null backend (rotatable-placed-items 10): the renderer's first frame builds every
// view it draws before it draws, so nothing waits on the builder, and a stream record made before a registry clear
// is never read.

#include "world/rendering/InstancedEntityRenderer.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <assets/ItemView.h>
#include <assets/PartsAsset.test.h>
#include <assets/placement/DrawBound.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/RenderContext.h>
#include <assets/placement/WorldDepthSort.h>
#include <world/camera/WorldCamera.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

namespace {

	using namespace assets::parts_fixtures;

	constexpr int	kViewportWidth = 800;
	constexpr int	kViewportHeight = 600;
	constexpr float kPixelsPerMeter = 8.0F;

	class ItemFirstFrameTest : public PartsTest {
	  protected:
		void SetUp() override {
			PartsTest::SetUp();
			Renderer::Primitives::init(std::make_unique<Renderer::backend::NullBackend>(), base_shader::source());
			Renderer::Primitives::setViewport(kViewportWidth, kViewportHeight);
			writeDef(
				"Box", "<detailLevels>8, 24, 80</detailLevels><parts>" + block("box", -0.6F, -0.3F, 0.6F, 0.3F, 0.0F, 0.8F) + "</parts><placements>" +
						   place("box") + "</placements>"
			);
			ASSERT_NE(load("Box"), nullptr);
			box = assets::AssetRegistry::Get().templateId("Box", {});
			ASSERT_NE(box, 0U);
		}

		void TearDown() override {
			renderer.reset(); // its meshes go while the backend is there
			Renderer::Primitives::shutdown();
			PartsTest::TearDown();
		}

		// One record a strip of a box at each turn, 3 m apart along a row, as DynamicEntityRenderSystem makes them.
		void placeBoxes(const std::vector<double>& degrees) {
			records.clear();
			assets::AssetRegistry& registry = assets::AssetRegistry::Get();
			for (size_t i = 0; i < degrees.size(); ++i) {
				const assets::ItemLayout* layout = registry.stripLayout(box, assets::turnQuantaFromDegrees(degrees[i]));
				const glm::vec2			  position{static_cast<float>(i) * 3.0F, 0.0F};
				for (uint32_t strip = 0; strip < layout->layout.strips.size(); ++strip) {
					assets::PlacedEntity record;
					record.templateId = box;
					record.position = position;
					record.anchorY = position.y + layout->layout.strips[strip].anchorLocalY;
					record.itemLayout = layout;
					record.strip = strip;
					record.layoutGeneration = registry.generation();
					records.push_back(record);
				}
			}
		}

		// One frame over the records with the camera at `zoom` over the row.
		worldview::RenderStats frame(float zoom) {
			uprights.clear();
			for (const assets::PlacedEntity& record : records) {
				uprights.push_back(assets::uprightOf(record));
			}
			std::vector<assets::DepthSortItem> items;
			for (const assets::Upright& upright : uprights) {
				items.push_back({upright.anchorY, &upright});
			}
			std::stable_sort(items.begin(), items.end(), [](const assets::DepthSortItem& left, const assets::DepthSortItem& right) {
				return left.anchorY < right.anchorY;
			});
			world::WorldCamera camera;
			camera.setPosition({6.0F, 0.0F});
			camera.setZoom(zoom);
			const assets::PlacementExecutor				executor(assets::AssetRegistry::Get());
			const std::unordered_set<world::ChunkCoordinate> processed;
			const assets::DrawBound						bound = assets::DrawBound::everywhere();
			const assets::RenderContext					context{
								.executor = executor,
								.processedChunks = processed,
								.dynamicEntities = &records,
								.camera = camera,
								.viewportWidth = kViewportWidth,
								.viewportHeight = kViewportHeight,
								.pixelsPerMeter = kPixelsPerMeter,
								.frameCounter = 0,
								.drawBound = bound,
			};
			worldview::RenderStats stats;
			renderer->emitSorted(items, static_cast<uint32_t>(items.size()), context, stats);
			return stats;
		}

		uint32_t							   box = 0;
		std::unique_ptr<worldview::InstancedEntityRenderer> renderer;
		std::vector<assets::PlacedEntity>	   records;
		std::vector<assets::Upright>		   uprights;
	};

} // namespace

// At the reference density (24 px/m) and zoomed in (80 px/m) alike, the first frame draws every item at the level it
// wants, with nothing left for the builder.
TEST_F(ItemFirstFrameTest, TheFirstFrameDrawsEveryItemAtItsLevelWithNothingPending) {
	for (const float density : {24.0F, 80.0F}) {
		renderer = std::make_unique<worldview::InstancedEntityRenderer>();
		placeBoxes({0.0, 35.0, 90.0, 271.0});
		const worldview::RenderStats stats = frame(density / kPixelsPerMeter);
		const auto&					 levels = renderer->levelStats();
		EXPECT_EQ(levels.pendingRequests, 0U) << density << " px/m";
		ASSERT_EQ(levels.viewUses.size(), 1U) << density << " px/m";
		EXPECT_EQ(levels.viewUses.front().pixelsPerMeter, density);
		EXPECT_EQ(levels.viewUses.front().instances, 4U) << "every box";
		EXPECT_GE(stats.itemDrawCalls, 1U);
		EXPECT_EQ(levels.residentViews, density == 24.0F ? 4U : 8U) << "each turn's reference view, and its finer one when zoomed in";
	}
}

// A record made before a registry clear points at a layout the clear freed: the renderer skips it, and the next
// frame's records, made under the new generation, draw.
TEST_F(ItemFirstFrameTest, ARecordFromBeforeAClearIsNeverRead) {
	renderer = std::make_unique<worldview::InstancedEntityRenderer>();
	placeBoxes({0.0, 45.0});
	EXPECT_GE(frame(3.0F).itemDrawCalls, 1U);

	ASSERT_NE(load("Box"), nullptr); // a clear: the records' layouts are freed
	box = assets::AssetRegistry::Get().templateId("Box", {});
	EXPECT_EQ(frame(3.0F).itemDrawCalls, 0U) << "the stale records are skipped, not read";
	placeBoxes({0.0, 45.0});
	EXPECT_GE(frame(3.0F).itemDrawCalls, 1U);
}
