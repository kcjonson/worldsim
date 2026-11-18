// The player visibility cover's logic on the null backend, so no GL: the record split into meshes at
// the 16-bit index limit, uploaded only when its epoch moves, and the layers a frame gathers from
// the record's pending shapes and the colonists' sight.

#include "world/rendering/VisibilityCoverRenderer.h"

#include "assets/AssetRegistry.h"
#include "assets/placement/RenderContext.h"

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

#include <predicates/Predicates.h>
#include <visibility/SightPolygon.h>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <optional>
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

namespace {

	double greatestAt(const std::vector<worldview::ValuePolygon>& pieces, glm::dvec2 point) {
		double greatest = 0.0;
		for (const worldview::ValuePolygon& piece : pieces) {
			for (std::size_t corner = 1; corner + 1 < piece.points.size(); ++corner) {
				const glm::dvec2 a	  = piece.points[0];
				const glm::dvec2 b	  = piece.points[corner];
				const glm::dvec2 c	  = piece.points[corner + 1];
				const double	 area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
				if (area == 0.0) {
					continue;
				}
				const double first	= ((b.x - point.x) * (c.y - point.y) - (b.y - point.y) * (c.x - point.x)) / area;
				const double second = ((c.x - point.x) * (a.y - point.y) - (c.y - point.y) * (a.x - point.x)) / area;
				const double third	= 1.0 - first - second;
				if (first >= -1e-9 && second >= -1e-9 && third >= -1e-9) {
					greatest = std::max(greatest, first * piece.values[0] + second * piece.values[corner] + third * piece.values[corner + 1]);
				}
			}
		}
		return greatest;
	}

	// The tint's value at a point by 7.4's rule over the frame's colonists: the least over them of 1
	// outside his bright shape and inside it the greatest of his pieces over 0.
	double tintAt(const VisibilityCoverRenderer& cover, glm::dvec2 millimetres) {
		double least = 1.0;
		for (const VisibilityCoverRenderer::ColonistCover& colonist : cover.colonists()) {
			const Vec2i64 point{std::llround(millimetres.x), std::llround(millimetres.y)};
			if (geometry::pointInPolygon(point, colonist.bright) == geometry::PointInPolygon::Outside) {
				continue;
			}
			double darkness = greatestAt(colonist.feather, millimetres);
			if (colonist.shadows != nullptr) {
				darkness = std::max(darkness, greatestAt(colonist.shadows->pieces(), millimetres));
			}
			least = std::min(least, darkness);
		}
		return least;
	}

} // namespace

// Several colonists (7.4): one group a colonist, darkened in, every piece clipped to its own bright
// shape, so one colonist's fade never darkens ground another lights fully, the ground both leave
// out is tinted whole, and each bright shape fades a feather in from its rim.
TEST_F(VisibilityCoverTest, SeveralColonistsComposeAsTheLeastDarkness) {
	const ecs::EntityID west = addColonist({0.0F, 0.0F});
	const ecs::EntityID east = addColonist({20.0F, 0.0F});
	walk(west, {1.0F, 0.0F}, 0.05F);
	walk(east, {21.0F, 0.0F}, 0.05F);
	frame();
	ASSERT_EQ(cover->colonists().size(), 2U);
	EXPECT_EQ(cover->tintShape().groups, 2U);
	EXPECT_TRUE(cover->tintShape().everyGroupDarkens);
	EXPECT_TRUE(cover->tintShape().everyPieceInsideItsBrightShape);
	for (const VisibilityCoverRenderer::ColonistCover& colonist : cover->colonists()) {
		EXPECT_EQ(colonist.rimSides, colonist.bright.size()) << "in the open every side is rim";
		EXPECT_EQ(colonist.feather.size(), colonist.bright.size()) << "a convex shape: a rectangle a side and no fan";
	}

	const double rimMm = static_cast<double>(vision::drawnVisibleRadiusMm(30000) - 400);
	EXPECT_NEAR(tintAt(*cover, {1000.0 - rimMm + 750.0, 0.0}), 0.5, 0.02) << "the west one's fade, half way in";
	EXPECT_NEAR(tintAt(*cover, {1000.0 + rimMm - 750.0, 0.0}), 0.0, 1e-9) << "his fade is lit fully by the east one";
	EXPECT_NEAR(tintAt(*cover, {11000.0, 0.0}), 0.0, 1e-9) << "both cores";
	EXPECT_EQ(tintAt(*cover, {-40000.0, 0.0}), 1.0) << "out of both sights";
}

// The bright shape lies inside seen ground, and the seen area's own fade lies inside it: the landing's
// edge fades a feather into the landing and the cells draw nothing deep inside it.
TEST_F(VisibilityCoverTest, TheBrightShapeIsSeenAndTheSeenEdgeFadesInside) {
	const ecs::EntityID colonist = addColonist({0.0F, 0.0F});
	walk(colonist, {3.0F, 0.0F}, 0.05F);
	camera.setPosition({140.0F, 0.0F});
	frame();
	camera.setPosition({0.0F, 0.0F});
	frame();
	ASSERT_EQ(cover->colonists().size(), 1U);
	for (const Vec2i64& vertex : cover->colonists()[0].bright) {
		EXPECT_TRUE(visibility.isSeen(nav::toMeters(vertex)));
	}

	camera.setPosition({140.0F, 0.0F});
	frame();
	std::vector<worldview::ValuePolygon> cells;
	for (const worldview::SeenFeather::Cell* cell : cover->seenFeather().cellsInView()) {
		cells.insert(cells.end(), cell->pieces.begin(), cell->pieces.end());
	}
	ASSERT_FALSE(cells.empty());
	// The landing is the sight polygon of 150 m, its sides 14.7 m long: on a side's middle, along its normal.
	const double inradius = 150000.0 * std::cos(std::numbers::pi / static_cast<double>(geometry::kSightSides));
	const double normal	  = std::numbers::pi / static_cast<double>(geometry::kSightSides);
	const auto	 inward	  = [&](double depth) { return glm::dvec2{std::cos(normal), std::sin(normal)} * (inradius - depth); };
	EXPECT_NEAR(greatestAt(cells, inward(750.0)), 0.5, 0.02);
	EXPECT_NEAR(greatestAt(cells, inward(10.0)), 1.0, 0.02);
	EXPECT_EQ(greatestAt(cells, inward(5000.0)), 0.0);
}

// The draw bound holds the seen ground in view and nothing well past it: a view over the landing's
// edge meets it on the seen side only, a view over unseen ground meets nothing, a colonist walking
// past the record holds his open leg, and with reveal on it holds everything.
TEST_F(VisibilityCoverTest, DrawBoundHoldsWhatTheCoverLeavesShowing) {
	const auto boundAt = [this](glm::vec2 centre) {
		camera.setPosition({centre.x, centre.y});
		return worldview::coverDrawBound(world, camera, kViewportWidth, kViewportHeight, kPixelsPerMeter);
	};
	const auto region = [](float minX, float maxX) { return assets::VisibleBounds{minX, maxX, -5.0F, 5.0F}; };

	const assets::DrawBound overLanding = boundAt({0.0F, 0.0F});
	EXPECT_TRUE(overLanding.meets(region(-10.0F, 10.0F)));

	const assets::DrawBound overEdge = boundAt({150.0F, 0.0F});
	EXPECT_TRUE(overEdge.meets(region(140.0F, 145.0F)));
	EXPECT_FALSE(overEdge.meets(region(152.0F, 190.0F))) << "the landing is 150 m round the drop";
	const std::optional<assets::VisibleBounds> cut = overEdge.within(region(100.0F, 190.0F));
	ASSERT_TRUE(cut.has_value());
	EXPECT_LT(cut->maxX, 151.0F);

	const assets::DrawBound overUnseen = boundAt({1000.0F, 0.0F});
	EXPECT_FALSE(overUnseen.meets(region(900.0F, 1100.0F)));

	// A colonist past the record: his open leg is held, round where he walked and no farther.
	const ecs::EntityID colonist = addColonist({1000.0F, 0.0F});
	walk(colonist, {1005.0F, 0.0F}, 0.1F);
	const assets::DrawBound overColonist = boundAt({1000.0F, 0.0F});
	EXPECT_TRUE(overColonist.meets(region(1028.0F, 1029.0F)));
	EXPECT_FALSE(overColonist.meets(region(1036.0F, 1100.0F)));

	visibility.setRevealed(true);
	const assets::DrawBound revealed = boundAt({5000.0F, 0.0F});
	const std::optional<assets::VisibleBounds> whole = revealed.within(region(4900.0F, 5100.0F));
	ASSERT_TRUE(whole.has_value());
	EXPECT_EQ(whole->minX, 4900.0F);
	EXPECT_EQ(whole->maxX, 5100.0F);
	EXPECT_EQ(whole->minY, -5.0F);
	EXPECT_EQ(whole->maxY, 5.0F);
}

// The camera loads a chunk whose things can show: one with seen ground, and one with none whose crowns
// reach seen ground in its neighbour (within the draw side's reach of its edge); one beyond that reach,
// or far off, it does not. With reveal on it loads every chunk.
TEST_F(VisibilityCoverTest, CameraLoadsChunksWithinReachOfSeenGround) {
	const float reach = assets::entityCullMarginMeters(assets::AssetRegistry::Get().maxReachMeters()) +
						worldview::drawBoundMarginMeters(camera, kPixelsPerMeter);
	constexpr float kRadius = 20.0F;
	const float		border	= 3.0F * world::kChunkWorldSize;
	const float		rowNear = 0.5F * world::kChunkWorldSize;
	const float		rowFar	= 2.5F * world::kChunkWorldSize;
	visibility.revealLanding({border - kRadius - (0.5F * reach), rowNear}, kRadius);
	visibility.revealLanding({border - kRadius - (2.0F * reach), rowFar}, kRadius);
	const auto loads = [this](int x, int y) {
		return worldview::cameraLoadsChunk(world, camera, kPixelsPerMeter, world::ChunkCoordinate(x, y));
	};

	EXPECT_TRUE(loads(0, 0)) << "the landing";
	EXPECT_TRUE(loads(2, 0)) << "its own seen ground";
	EXPECT_TRUE(loads(3, 0)) << "no seen ground, but the seen edge next door is within reach";
	EXPECT_TRUE(loads(2, 2));
	EXPECT_FALSE(loads(3, 2)) << "the seen edge next door is beyond reach";
	EXPECT_FALSE(loads(5, 0));

	visibility.setRevealed(true);
	EXPECT_TRUE(loads(3, 2));
	EXPECT_TRUE(loads(5, 0));
}
