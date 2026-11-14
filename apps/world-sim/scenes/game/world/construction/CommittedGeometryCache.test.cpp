#include "scenes/game/world/construction/CommittedGeometryCache.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

	using construction::ConstructionWorld;
	using construction::FoundationState;
	using construction::SegmentId;
	using geometry::Vec2i64;
	using world_sim::CommittedGeometryCache;
	using world_sim::WallBandGeom;

	// <root>/apps/world-sim/scenes/game/world/construction/CommittedGeometryCache.test.cpp
	std::string constructionConfigFolder() {
		std::filesystem::path root = __FILE__;
		for (int level = 0; level < 7; ++level) {
			root = root.parent_path();
		}
		return (root / "assets" / "config" / "construction").string();
	}

	class CommittedGeometryCacheTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::ConstructionRegistry::Get().clear();
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load(constructionConfigFolder()));
		}
		void TearDown() override { assets::ConstructionRegistry::Get().clear(); }

		// Each wall gets a stand-in ECS mirror entity, the handle the cache carries
		// over, so the tests can tell its geometry apart.
		static ecs::EntityID entityOf(SegmentId segment) { return 1000 + segment; }

		SegmentId wall(Vec2i64 from, Vec2i64 to, FoundationState state) {
			const construction::SegmentCommitResult result = world.commitSegment(from, to, "Wood", "Standard", construction::kInvalidFoundation);
			EXPECT_TRUE(result.ok());
			world.setSegmentState(result.id, state);
			world.setSegmentEntity(result.id, entityOf(result.id));
			return result.id;
		}

		const WallBandGeom* geometryOf(SegmentId segment) const {
			for (const WallBandGeom& wallGeometry : cache.walls()) {
				if (wallGeometry.entity == entityOf(segment)) {
					return &wallGeometry;
				}
			}
			return nullptr;
		}

		std::size_t ringsOf(SegmentId segment) const {
			const WallBandGeom* wallGeometry = geometryOf(segment);
			return wallGeometry != nullptr ? wallGeometry->rings.size() : 0;
		}

		ConstructionWorld	   world;
		CommittedGeometryCache cache;
	};

	TEST_F(CommittedGeometryCacheTest, RejectedGraphFallsBackAloneAndOthersKeepTheirBands) {
		const SegmentId first = wall({0, 0}, {4000, 0}, FoundationState::Built);
		const SegmentId second = wall({4000, 0}, {4000, 3000}, FoundationState::Built);
		// A 50 mm leg off a corner: the corner's trim (half the 200 mm thickness)
		// overruns it, so the offsetter rejects this graph.
		const SegmentId longLeg = wall({10000, 0}, {11000, 0}, FoundationState::Built);
		const SegmentId shortLeg = wall({11000, 0}, {11000, 50}, FoundationState::Built);

		cache.refresh(world);

		EXPECT_EQ(cache.fallbackCenterlines().size(), 2u);
		EXPECT_EQ(geometryOf(longLeg), nullptr);
		EXPECT_EQ(geometryOf(shortLeg), nullptr);
		ASSERT_NE(geometryOf(first), nullptr);
		ASSERT_NE(geometryOf(second), nullptr);
		EXPECT_EQ(ringsOf(first) + ringsOf(second), 3u) << "two bands and their corner";
		EXPECT_FALSE(geometryOf(first)->outline.empty());
		EXPECT_FALSE(geometryOf(second)->outline.empty());
	}

	TEST_F(CommittedGeometryCacheTest, CornerBelongsToTheBlueprintAndMovesWhenItsStateChanges) {
		const SegmentId built = wall({0, 0}, {4000, 0}, FoundationState::Built);
		const SegmentId blueprint = wall({4000, 0}, {4000, 3000}, FoundationState::Blueprint);

		cache.refresh(world);
		EXPECT_EQ(ringsOf(built), 1u);
		EXPECT_EQ(ringsOf(blueprint), 2u) << "the blueprint's band and the corner";

		// The other wall becomes the unfinished one, so the corner moves to it.
		world.setSegmentState(built, FoundationState::Blueprint);
		world.setSegmentState(blueprint, FoundationState::Built);
		cache.refresh(world);
		EXPECT_EQ(ringsOf(built), 2u);
		EXPECT_EQ(ringsOf(blueprint), 1u);

		// Both built: the corner is still filled exactly once.
		world.setSegmentState(built, FoundationState::Built);
		cache.refresh(world);
		EXPECT_EQ(ringsOf(built) + ringsOf(blueprint), 3u);

		// Demolishing one wall takes the corner with it.
		ASSERT_TRUE(world.removeSegment(blueprint));
		cache.refresh(world);
		EXPECT_EQ(geometryOf(blueprint), nullptr);
		EXPECT_EQ(ringsOf(built), 1u);
	}

	TEST_F(CommittedGeometryCacheTest, WallThatIsAllOpeningKeepsItsOutlineAndBoundsCoverIt) {
		const SegmentId longWall = wall({0, 0}, {4000, 0}, FoundationState::Blueprint);
		const SegmentId shortWall = wall({4000, 0}, {4000, 800}, FoundationState::Built);
		// A 0.9 m door centred on a 0.8 m wall leaves none of it standing.
		ASSERT_NE(world.addOpening(shortWall, 0.5F, "Door", "Wood"), construction::kInvalidOpening);

		cache.refresh(world);

		const WallBandGeom* shortGeometry = geometryOf(shortWall);
		ASSERT_NE(shortGeometry, nullptr) << "dropped with its outline";
		EXPECT_TRUE(shortGeometry->rings.empty());
		EXPECT_FALSE(shortGeometry->outline.empty()) << "the corner's edge facing the door";
		EXPECT_EQ(ringsOf(longWall), 2u) << "the blueprint keeps its band and the corner";

		for (const WallBandGeom& wallGeometry : cache.walls()) {
			const Foundation::Rect& bounds = wallGeometry.aabb;
			for (const auto& edge : wallGeometry.outline) {
				for (const Foundation::Vec2& point : edge) {
					EXPECT_GE(point.x, bounds.x);
					EXPECT_GE(point.y, bounds.y);
					EXPECT_LE(point.x, bounds.x + bounds.width);
					EXPECT_LE(point.y, bounds.y + bounds.height);
				}
			}
		}
	}

} // namespace
