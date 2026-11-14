// The player visibility cover's logic on the null backend, so no GL: the record split into meshes at
// the 16-bit index limit, uploaded only when its epoch moves, and the layers a frame gathers from
// the record's pending shapes and the colonists' sight.

#include "world/rendering/VisibilityCoverRenderer.h"

#include "BaseShader.h"
#include "backend/NullBackend.h"
#include "primitives/Primitives.h"

#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <ecs/systems/VisionSystem.h>
#include <nav/NavCoords.h>

#include <visibility/SightPolygon.h>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

namespace {

	using geometry::Vec2i64;
	using worldview::VisibilityCoverRenderer;

	constexpr int	kViewportWidth = 800;
	constexpr int	kViewportHeight = 600;
	constexpr float kPixelsPerMeter = 8.0F;

	std::vector<vision::SeenArea::Triangle> triangleStrip(std::size_t count) {
		std::vector<vision::SeenArea::Triangle> triangles;
		triangles.reserve(count);
		for (std::size_t index = 0; index < count; ++index) {
			const auto x = static_cast<std::int64_t>(index) * 1000;
			triangles.push_back({.corners = {{Vec2i64{x, 0}, Vec2i64{x + 1000, 0}, Vec2i64{x, 1000}}}});
		}
		return triangles;
	}

	// A colony on the null backend: vision every frame, the landing round the origin, and the camera
	// over it at the game's default zoom.
	class VisibilityCoverTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			Renderer::Primitives::init(std::make_unique<Renderer::backend::NullBackend>(), base_shader::source());
			Renderer::Primitives::setViewport(kViewportWidth, kViewportHeight);
			vision.setUpdateInterval(1);
			visibility.revealLanding({0.0F, 0.0F}, 150.0F);
			camera.setPosition({0.0F, 0.0F});
		}

		void TearDown() override {
			cover.reset(); // its meshes go while the backend is there
			Renderer::Primitives::shutdown();
		}

		ecs::EntityID addColonist(glm::vec2 at) {
			const ecs::EntityID colonist = world.createEntity();
			world.addComponent<ecs::Position>(colonist, ecs::Position{at});
			world.addComponent<ecs::Colonist>(colonist, ecs::Colonist{"Test"});
			world.addComponent<ecs::Memory>(colonist, ecs::Memory{});
			world.getComponent<ecs::Memory>(colonist)->owner = colonist;
			return colonist;
		}

		void walk(ecs::EntityID colonist, glm::vec2 to, float stepMeters) {
			const glm::vec2 from = world.getComponent<ecs::Position>(colonist)->value;
			const int		steps = static_cast<int>(std::ceil(glm::length(to - from) / stepMeters));
			for (int step = 1; step <= steps; ++step) {
				world.getComponent<ecs::Position>(colonist)->value =
					from + (to - from) * (static_cast<float>(step) / static_cast<float>(steps));
				world.update(0.016F);
			}
		}

		void frame() {
			Renderer::Primitives::beginFrame();
			cover->render(world, camera, kViewportWidth, kViewportHeight, kPixelsPerMeter);
			Renderer::Primitives::endFrame();
		}

		ecs::World								 world;
		ecs::VisionSystem&						 vision = world.registerSystem<ecs::VisionSystem>();
		ecs::PlayerVisibilitySystem&			 visibility = world.registerSystem<ecs::PlayerVisibilitySystem>();
		world::WorldCamera						 camera;
		std::unique_ptr<VisibilityCoverRenderer> cover = std::make_unique<VisibilityCoverRenderer>();
	};

} // namespace

TEST(VisibilityCoverMeshes, SplitAtTheSixteenBitLimit) {
	constexpr std::size_t kTrianglesInOneMesh = worldview::kSeenMeshVertexLimit / 3;

	const std::vector<renderer::TessellatedMesh> whole = worldview::seenMeshes(triangleStrip(kTrianglesInOneMesh));
	ASSERT_EQ(whole.size(), 1U);
	EXPECT_EQ(whole[0].vertices.size(), worldview::kSeenMeshVertexLimit);

	const std::vector<vision::SeenArea::Triangle> triangles = triangleStrip((2 * kTrianglesInOneMesh) + 5);
	const std::vector<renderer::TessellatedMesh>  split = worldview::seenMeshes(triangles);
	ASSERT_EQ(split.size(), 3U);
	std::size_t triangleIndex = 0;
	for (const renderer::TessellatedMesh& mesh : split) {
		ASSERT_LE(mesh.vertices.size(), worldview::kSeenMeshVertexLimit);
		ASSERT_EQ(mesh.colors.size(), mesh.vertices.size());
		ASSERT_EQ(mesh.indices.size() % 3, 0U);
		for (std::size_t corner = 0; corner < mesh.indices.size(); ++corner) {
			ASSERT_LT(mesh.indices[corner], mesh.vertices.size());
			const Vec2i64&	expected = triangles[triangleIndex + (corner / 3)].corners[corner % 3];
			const glm::vec2 meters = mesh.vertices[mesh.indices[corner]];
			ASSERT_NEAR(meters.x, static_cast<float>(expected.x) / 1000.0F, 0.01F);
			ASSERT_NEAR(meters.y, static_cast<float>(expected.y) / 1000.0F, 0.01F);
		}
		triangleIndex += mesh.indices.size() / 3;
	}
	EXPECT_EQ(triangleIndex, triangles.size()) << "every triangle in exactly one mesh";
	EXPECT_TRUE(worldview::seenMeshes({}).empty());
}

TEST_F(VisibilityCoverTest, UploadsOnlyWhenTheEpochMoves) {
	frame();
	EXPECT_EQ(cover->uploads(), 1U);
	EXPECT_EQ(cover->meshCount(), 1U) << "the landing";
	frame();
	frame();
	EXPECT_EQ(cover->uploads(), 1U) << "the record did not change";

	const ecs::EntityID colonist = addColonist({140.0F, 0.0F});
	walk(colonist, {200.0F, 0.0F}, 0.1F);
	world.destroyEntity(colonist); // ends his leg, which is committed within the budget
	const std::uint64_t before = visibility.epoch();
	for (int tick = 0; tick < 10 && visibility.seenArea().shapesPending() > 0; ++tick) {
		world.update(0.016F);
	}
	ASSERT_NE(visibility.epoch(), before);
	frame();
	EXPECT_EQ(cover->uploads(), 2U);
	frame();
	EXPECT_EQ(cover->uploads(), 2U);
}

TEST_F(VisibilityCoverTest, LayersFromSightAndPendingShapes) {
	frame();
	EXPECT_TRUE(cover->layers().drawn);
	EXPECT_TRUE(cover->layers().visibleOutlines.empty()) << "nobody in sight yet";
	EXPECT_TRUE(cover->layers().pendingOutlines.empty());

	const ecs::EntityID colonist = addColonist({0.0F, 0.0F});
	walk(colonist, {5.0F, 0.0F}, 0.1F);
	frame();
	ASSERT_EQ(cover->layers().visibleOutlines.size(), 1U);
	EXPECT_EQ(cover->layers().visibleOutlines[0].size(), static_cast<std::size_t>(geometry::kSightSides));
	ASSERT_EQ(cover->layers().pendingOutlines.size(), 1U) << "his open leg";

	// The bright disc is drawn round where he stands, in screen pixels.
	const glm::vec2 standing = camera.worldToScreen(5.0F, 0.0F, kViewportWidth, kViewportHeight, kPixelsPerMeter);
	const float		drawnPixels = static_cast<float>(vision::drawnVisibleRadiusMm(30000)) / 1000.0F * kPixelsPerMeter * camera.zoom();
	for (const glm::vec2 point : cover->layers().visibleOutlines[0]) {
		EXPECT_LE(glm::length(point - standing), drawnPixels + 0.5F);
	}

	// A colonist far off the view adds nothing to the frame's layers.
	const ecs::EntityID faraway = addColonist({2000.0F, 0.0F});
	walk(faraway, {2005.0F, 0.0F}, 0.1F);
	frame();
	EXPECT_EQ(cover->layers().visibleOutlines.size(), 1U);
	EXPECT_EQ(cover->layers().pendingOutlines.size(), 1U);
}

TEST_F(VisibilityCoverTest, NothingWhileRevealed) {
	const ecs::EntityID colonist = addColonist({0.0F, 0.0F});
	walk(colonist, {5.0F, 0.0F}, 0.1F);
	visibility.setRevealed(true);
	frame();
	EXPECT_FALSE(cover->layers().drawn);
	EXPECT_TRUE(cover->layers().visibleOutlines.empty());
	EXPECT_TRUE(cover->layers().pendingOutlines.empty());
	EXPECT_EQ(cover->uploads(), 0U);

	visibility.setRevealed(false);
	frame();
	EXPECT_TRUE(cover->layers().drawn);
	EXPECT_EQ(cover->uploads(), 1U);
	EXPECT_EQ(cover->layers().visibleOutlines.size(), 1U);
}
