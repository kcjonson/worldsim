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
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <ecs/systems/VisionSystem.h>
#include <nav/NavCoords.h>

#include <predicates/Predicates.h>
#include <visibility/SightPolygon.h>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <map>
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

		// A square trunk `sideMeters` wide round `centre`, placed long ago.
		void place(glm::vec2 centre, float sideMeters) {
			const Vec2i64	   middle = nav::toMm(centre);
			const std::int64_t half	  = std::llround(sideMeters * 500.0F);
			navigation.placedOccluders().add(
				nextRing++,
				{middle + Vec2i64{-half, -half}, middle + Vec2i64{half, -half}, middle + Vec2i64{half, half}, middle + Vec2i64{-half, half}},
				"Test_Trunk",
				-1000000000
			);
		}

		ecs::World								 world;
		ecs::VisionSystem&						 vision = world.registerSystem<ecs::VisionSystem>();
		ecs::NavigationSystem&					 navigation = world.registerSystem<ecs::NavigationSystem>();
		ecs::PlayerVisibilitySystem&			 visibility = world.registerSystem<ecs::PlayerVisibilitySystem>();
		geometry::nav::RingId					 nextRing = 1;
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
	EXPECT_EQ(cover->layers().brightShapes, 0U) << "nobody in sight yet";
	EXPECT_EQ(cover->layers().pendingShapes, 0U);

	const ecs::EntityID colonist = addColonist({0.0F, 0.0F});
	walk(colonist, {5.0F, 0.0F}, 0.1F);
	frame();
	ASSERT_EQ(cover->layers().brightShapes, 1U);
	ASSERT_EQ(cover->colonists().size(), 1U);
	EXPECT_EQ(cover->colonists()[0].bright->size(), static_cast<std::size_t>(geometry::kSightSides));
	EXPECT_EQ(cover->layers().pendingShapes, 1U) << "his open leg";

	// The bright disc is drawn round where he stands.
	const double drawnMm = static_cast<double>(vision::drawnVisibleRadiusMm(30000));
	for (const Vec2i64& point : *cover->colonists()[0].bright) {
		EXPECT_LE(std::hypot(static_cast<double>(point.x) - 5000.0, static_cast<double>(point.y)), drawnMm + 1.0);
	}

	// A colonist far off the view adds nothing to the frame's layers.
	const ecs::EntityID faraway = addColonist({2000.0F, 0.0F});
	walk(faraway, {2005.0F, 0.0F}, 0.1F);
	frame();
	EXPECT_EQ(cover->layers().brightShapes, 1U);
	EXPECT_EQ(cover->layers().pendingShapes, 1U);
}

TEST_F(VisibilityCoverTest, NothingWhileRevealed) {
	const ecs::EntityID colonist = addColonist({0.0F, 0.0F});
	walk(colonist, {5.0F, 0.0F}, 0.1F);
	visibility.setRevealed(true);
	frame();
	EXPECT_FALSE(cover->layers().drawn);
	EXPECT_EQ(cover->layers().brightShapes, 0U);
	EXPECT_EQ(cover->layers().pendingShapes, 0U);
	EXPECT_EQ(cover->uploads(), 0U);

	visibility.setRevealed(false);
	frame();
	EXPECT_TRUE(cover->layers().drawn);
	EXPECT_EQ(cover->uploads(), 1U);
	EXPECT_EQ(cover->layers().brightShapes, 1U);
}

namespace {

	double greatestAt(const std::vector<worldview::ValueTriangle>& pieces, glm::dvec2 point) {
		double greatest = 0.0;
		for (const worldview::ValueTriangle& piece : pieces) {
			for (std::size_t corner = 1; corner + 1 < piece.points.size(); ++corner) {
				const glm::dvec2 origin = piece.points[0];
				const glm::dvec2 near	= piece.points[corner];
				const glm::dvec2 far	= piece.points[corner + 1];
				const double	 area	= (near.x - origin.x) * (far.y - origin.y) - (near.y - origin.y) * (far.x - origin.x);
				if (area == 0.0) {
					continue;
				}
				const double first	= ((near.x - point.x) * (far.y - point.y) - (near.y - point.y) * (far.x - point.x)) / area;
				const double second = ((far.x - point.x) * (origin.y - point.y) - (far.y - point.y) * (origin.x - point.x)) / area;
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
			if (geometry::pointInPolygon(point, *colonist.bright) == geometry::PointInPolygon::Outside) {
				continue;
			}
			const glm::dvec2 anchor{static_cast<double>(colonist.featherAnchor.x), static_cast<double>(colonist.featherAnchor.y)};
			double			 darkness = greatestAt(*colonist.feather, millimetres - anchor);
			if (colonist.shadows != nullptr) {
				std::vector<worldview::ValueTriangle> soft;
				for (std::size_t ring = 0; ring < worldview::DrawnShadows::kSoftRings; ++ring) {
					colonist.shadows->appendSoftPieces(ring, soft);
				}
				darkness = std::max({darkness, greatestAt(soft, millimetres), greatestAt(colonist.shadows->bandPieces(), millimetres)});
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
		EXPECT_EQ(colonist.rimSides, colonist.bright->size()) << "in the open every side is rim";
		EXPECT_EQ(colonist.feather->size(), 2 * colonist.bright->size()) << "a convex shape: a rectangle a side and no fan";
	}

	const double rimMm = static_cast<double>(vision::drawnVisibleRadiusMm(30000) - 400);
	EXPECT_NEAR(tintAt(*cover, {1000.0 - rimMm + 750.0, 0.0}), 0.5, 0.02) << "the west one's fade, half way in";
	EXPECT_NEAR(tintAt(*cover, {1000.0 + rimMm - 750.0, 0.0}), 0.0, 1e-9) << "his fade is lit fully by the east one";
	EXPECT_NEAR(tintAt(*cover, {11000.0, 0.0}), 0.0, 1e-9) << "both cores";
	EXPECT_EQ(tintAt(*cover, {-40000.0, 0.0}), 1.0) << "out of both sights";
}

// The bright shape lies inside seen ground, and the seen area's own fade lies inside it: the landing's
// edge fades a feather into the landing and the cells draw nothing deep inside it.
// A colonist who stands still, once his rim's ease has ended, builds and uploads nothing a frame.
TEST_F(VisibilityCoverTest, AStillColonistBuildsNothingOnceSettled) {
	const ecs::EntityID colonist = addColonist({0.0F, 0.0F});
	walk(colonist, {3.0F, 0.0F}, 0.05F);
	frame();
	EXPECT_GT(cover->counts().piecesBuilt, 0U);
	for (int settle = 0; settle < 10; ++settle) {
		world.update(0.016F);
		frame();
	}
	for (int still = 0; still < 20; ++still) {
		world.update(0.016F);
		frame();
		EXPECT_EQ(cover->counts().piecesBuilt, 0U) << "frame " << still;
		EXPECT_EQ(cover->counts().meshUploads, 0U) << "frame " << still;
	}
}

// A colonist who leaves the view and comes back builds nothing for it: his pieces and meshes are kept
// while he lives.
TEST_F(VisibilityCoverTest, LeavingTheViewKeepsHisPieces) {
	place({6.0F, 2.0F}, 0.4F);
	const ecs::EntityID colonist = addColonist({0.0F, 0.0F});
	walk(colonist, {3.0F, 0.0F}, 0.05F);
	for (int settle = 0; settle < 10; ++settle) {
		world.update(0.016F);
		frame();
	}
	ASSERT_EQ(cover->colonists().size(), 1U);
	ASSERT_GT(cover->colonists()[0].shadows->softPieceCount(), 0U);
	camera.setPosition({400.0F, 0.0F});
	frame();
	EXPECT_EQ(cover->colonists().size(), 0U);
	camera.setPosition({0.0F, 0.0F});
	frame();
	ASSERT_EQ(cover->colonists().size(), 1U);
	EXPECT_EQ(cover->counts().piecesBuilt, 0U);
	EXPECT_EQ(cover->counts().meshUploads, 0U);
}

// Fifty colonists walking 2 m/s through trunks 4 m apart: no frame builds more soft pieces than
// kDrawnShadowPiecesPerFrame and one body's, a frame that leaves bodies due spends the budget, the
// longest due waits a bounded time, and every walker gets builds in turn.
TEST_F(VisibilityCoverTest, FiftyWalkingKeepToTheBudgetAndEveryOneIsBuilt) {
	for (float x = -38.0F; x <= 62.0F; x += 4.0F) {
		for (float y = -32.0F; y <= 32.0F; y += 4.0F) {
			place({x, y}, 0.5F);
		}
	}
	std::vector<ecs::EntityID> walkers;
	for (int index = 0; index < 50; ++index) {
		walkers.push_back(addColonist({static_cast<float>(index % 10) * 4.0F - 20.0F, static_cast<float>(index / 10) * 4.0F - 10.0F}));
	}
	constexpr std::size_t kOneBodyMostPieces = 200;
	constexpr int		  kFrames			 = 150;
	constexpr int		  kWarmFrames		 = 60;
	std::map<ecs::EntityID, int> builtLate;
	std::size_t					 backlogFrames = 0;
	for (int step = 0; step < kFrames; ++step) {
		for (const ecs::EntityID walker : walkers) {
			world.getComponent<ecs::Position>(walker)->value.x += 2.0F / 60.0F;
		}
		world.update(1.0F / 60.0F);
		frame();
		const VisibilityCoverRenderer::Counts& counts = cover->counts();
		ASSERT_EQ(counts.colonists, 50U);
		EXPECT_LE(counts.softPiecesBuilt, worldview::kDrawnShadowPiecesPerFrame + kOneBodyMostPieces) << "frame " << step;
		// The next body is let in by its last count of pieces, so the budget left unspent is at most one
		// body's.
		if (counts.softBodiesDue > counts.softBodiesBuilt) {
			++backlogFrames;
			EXPECT_GE(counts.softPiecesBuilt + kOneBodyMostPieces, worldview::kDrawnShadowPiecesPerFrame) << "frame " << step << ": bodies wait, budget unspent";
		}
		if (step >= kWarmFrames) {
			EXPECT_LE(counts.softOldestDueFrames, 60U) << "frame " << step;
			for (const VisibilityCoverRenderer::ColonistCover& colonist : cover->colonists()) {
				builtLate[colonist.colonist] += colonist.shadows->counts().softBuilt > 0 ? 1 : 0;
			}
		}
	}
	EXPECT_GT(backlogFrames, 0U) << "fifty in forest outrun one frame's budget";
	ASSERT_EQ(builtLate.size(), 50U);
	for (const auto& [walker, frames] : builtLate) {
		EXPECT_GE(frames, 3) << "walker " << walker << ": 3 m of walk past trunks 4 m apart";
	}
}

TEST_F(VisibilityCoverTest, TheBrightShapeIsSeenAndTheSeenEdgeFadesInside) {
	const ecs::EntityID colonist = addColonist({0.0F, 0.0F});
	walk(colonist, {3.0F, 0.0F}, 0.05F);
	camera.setPosition({140.0F, 0.0F});
	frame();
	camera.setPosition({0.0F, 0.0F});
	frame();
	ASSERT_EQ(cover->colonists().size(), 1U);
	for (const Vec2i64& vertex : *cover->colonists()[0].bright) {
		EXPECT_TRUE(visibility.isSeen(nav::toMeters(vertex)));
	}

	camera.setPosition({140.0F, 0.0F});
	frame();
	std::vector<worldview::ValueTriangle> cells;
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
