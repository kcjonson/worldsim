#include "WallSides.h"
#include "WallStand.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

using namespace construction;
using assets::ConstructionRegistry;
using geometry::Vec2i64;

namespace {

	constexpr float kAgentRadius = 0.3F;

	class WallStandTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ConstructionRegistry::Get().clear();
			std::filesystem::path folder = __FILE__; // libs/sim/construction/WallStand.test.cpp
			folder = folder.parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "construction";
			ASSERT_TRUE(ConstructionRegistry::Get().load(folder.string()));
		}
		void TearDown() override { ConstructionRegistry::Get().clear(); }

		// A Standard Wood wall from `from` to `to` (mm), built unless asked for a blueprint. A wall split by it
		// keeps its own state.
		SegmentId wall(Vec2i64 from, Vec2i64 to, bool built = true) {
			const SegmentCommitResult result = world.commitSegment(from, to, "Wood", "Standard", kInvalidFoundation, world.version());
			EXPECT_TRUE(result.ok());
			if (built) {
				for (const SegmentId id : result.createdSegments) {
					world.setSegmentState(id, FoundationState::Built);
				}
			}
			return between(from, to);
		}

		// The segment joining the vertices at `from` and `to`.
		SegmentId between(Vec2i64 from, Vec2i64 to) const {
			const VertexId first = world.vertexAt(from);
			const VertexId second = world.vertexAt(to);
			for (const SegmentId id : world.segmentsOfVertex(first)) {
				const WallSegment* segment = world.getSegment(id);
				if ((segment->v0 == first && segment->v1 == second) || (segment->v0 == second && segment->v1 == first)) {
					return id;
				}
			}
			ADD_FAILURE() << "no segment between the two points";
			return kInvalidSegment;
		}

		void opening(SegmentId host, float fraction, const char* type, bool built = true) {
			const OpeningId id = world.addOpening(host, fraction, type, "Wood");
			ASSERT_NE(id, kInvalidOpening);
			if (built) {
				world.setOpeningState(id, FoundationState::Built);
			}
		}

		// Three built walls of the square from (left, bottom) to (right, top) and the fourth, its left side
		// (left, top) to (left, bottom), as a blueprint: the segment that closes it.
		SegmentId squareClosedBy(std::int64_t left, std::int64_t bottom, std::int64_t right, std::int64_t top) {
			wall({left, bottom}, {right, bottom});
			wall({right, bottom}, {right, top});
			wall({right, top}, {left, top});
			return wall({left, top}, {left, bottom}, false);
		}

		ConstructionWorld world;
	};

	// Its one stand, which must lie toward `side` from the segment's midpoint.
	void expectOneStandToward(const std::vector<WallStand>& stands, glm::vec2 side) {
		ASSERT_EQ(stands.size(), 1U);
		EXPECT_GT(glm::dot(stands.front().standPoint(kAgentRadius) - stands.front().midpoint, side), 0.0F);
	}

	// A wall whose ends are joined by nothing built (here two built walls and an unbuilt one) closes
	// nothing: a stand each side, beside its midpoint and clear of its band.
	TEST_F(WallStandTest, AWallThatClosesNothingHasAStandBesideItEachSide) {
		wall({0, 0}, {4000, 0});
		wall({4000, 0}, {4000, 4000}, false);
		wall({4000, 4000}, {0, 4000});
		const SegmentId segment = wall({0, 4000}, {0, 0}, false);
		const auto		stands = wallStands(world, segment);
		ASSERT_EQ(stands.size(), 2U);
		const float halfThickness = 0.1F; // Wood Standard, 0.20 m
		for (const WallStand& stand : stands) {
			EXPECT_NEAR(stand.midpoint.x, 0.0F, 1e-5F);
			EXPECT_NEAR(stand.midpoint.y, 2.0F, 1e-5F);
			EXPECT_NEAR(stand.halfLengthMeters, 2.0F, 1e-5F);
			const glm::vec2 point = stand.standPoint(kAgentRadius);
			EXPECT_NEAR(std::abs(point.x), halfThickness + kAgentRadius + WallStand::kStandMarginMeters, 1e-5F);
			EXPECT_TRUE(stand.serves(point, point, kAgentRadius));
			EXPECT_FALSE(wallBand(world, *world.getSegment(segment))->overlaps(point, kAgentRadius));
		}
		EXPECT_LT(glm::dot(stands[0].outward, stands[1].outward), 0.0F) << "one on each side";
	}

	TEST_F(WallStandTest, TheSegmentClosingADoorlessSquareIsBuiltFromOutside) {
		expectOneStandToward(wallStands(world, squareClosedBy(0, 0, 4000, 4000)), {-1.0F, 0.0F});
	}

	TEST_F(WallStandTest, ALoopWithABuiltDoorLeavesBothSides) {
		const SegmentId closing = squareClosedBy(0, 0, 4000, 4000);
		opening(between({0, 0}, {4000, 0}), 0.5F, "Door");
		EXPECT_EQ(wallStands(world, closing).size(), 2U);
	}

	// A door blueprint is no door until it is built, and a window lets nobody through.
	TEST_F(WallStandTest, ADoorBlueprintOrAWindowIsNoWayOut) {
		const SegmentId closing = squareClosedBy(0, 0, 4000, 4000);
		opening(between({0, 0}, {4000, 0}), 0.3F, "Door", false);
		opening(between({4000, 4000}, {0, 4000}), 0.5F, "Window");
		expectOneStandToward(wallStands(world, closing), {-1.0F, 0.0F});
	}

	// Doors join room to room: the divider's west side opens through a door into a room with a door
	// outside, its east side onto nothing.
	TEST_F(WallStandTest, ADoorIntoARoomWithAWayOutIsAWayOut) {
		wall({0, 0}, {9000, 0});
		wall({9000, 0}, {9000, 4000});
		wall({9000, 4000}, {0, 4000});
		const SegmentId west = wall({0, 4000}, {0, 0});
		opening(west, 0.5F, "Door");
		opening(wall({3000, 0}, {3000, 4000}), 0.5F, "Door");
		expectOneStandToward(wallStands(world, wall({6000, 0}, {6000, 4000}, false)), {-1.0F, 0.0F});
	}

	TEST_F(WallStandTest, ARoomInsideARoomWithADoorIsBuiltFromTheOuterRoom) {
		wall({0, 0}, {10000, 0});
		wall({10000, 0}, {10000, 10000});
		wall({10000, 10000}, {0, 10000});
		wall({0, 10000}, {0, 0});
		opening(between({0, 0}, {10000, 0}), 0.5F, "Door");
		expectOneStandToward(wallStands(world, squareClosedBy(3000, 3000, 6000, 6000)), {-1.0F, 0.0F});
	}

	// Sealed either way: no side is better, so both are left and the builder takes the nearer.
	TEST_F(WallStandTest, ARoomInsideADoorlessRoomLeavesBothSides) {
		wall({0, 0}, {10000, 0});
		wall({10000, 0}, {10000, 10000});
		wall({10000, 10000}, {0, 10000});
		wall({0, 10000}, {0, 0});
		const SegmentId closing = squareClosedBy(3000, 3000, 6000, 6000);
		EXPECT_EQ(wallStands(world, closing).size(), 2U);
		EXPECT_FALSE(WallClosure(world, closing).shutsIn({4.5F, 4.5F})) << "nobody is shut in when every side is sealed";
	}

	TEST_F(WallStandTest, TheSegmentClosingAConcaveLoopIsBuiltFromOutside) {
		wall({0, 0}, {6000, 0});
		wall({6000, 0}, {6000, 3000});
		wall({6000, 3000}, {3000, 3000});
		wall({3000, 3000}, {3000, 6000});
		wall({3000, 6000}, {0, 6000});
		expectOneStandToward(wallStands(world, wall({0, 6000}, {0, 0}, false)), {-1.0F, 0.0F});
	}

	// Whom completing a closing wall shuts in: anyone in the room it closes, wherever he stands in it, and
	// nobody outside it.
	TEST_F(WallStandTest, AClosingWallShutsInWhoeverIsInTheRoom) {
		const WallClosure closure(world, squareClosedBy(0, 0, 4000, 4000));
		EXPECT_TRUE(closure.shutsIn({2.0F, 2.0F}));
		EXPECT_TRUE(closure.shutsIn({3.7F, 0.4F}));
		EXPECT_FALSE(closure.shutsIn({-0.6F, 2.0F}));
		EXPECT_FALSE(closure.shutsIn({2.0F, 9.0F}));
	}

	// An L-shaped room closed by the wall from (2, 2) to (4, 2), its outside the notch below (y < 2). The
	// room lies across the wall's line (y > 2) and past its east end at the reflex corner (x > 4, y < 2).
	// Only a point beside the wall on the outside, near its stand point, serves.
	TEST_F(WallStandTest, OnlyAPointBesideTheWallOnItsOutsideServes) {
		wall({4000, 2000}, {4000, 0});
		wall({4000, 0}, {6000, 0});
		wall({6000, 0}, {6000, 6000});
		wall({6000, 6000}, {0, 6000});
		wall({0, 6000}, {0, 2000});
		wall({0, 2000}, {2000, 2000});
		const auto stands = wallStands(world, wall({2000, 2000}, {4000, 2000}, false));
		expectOneStandToward(stands, {0.0F, -1.0F});

		const WallStand& outside = stands.front();
		const float		 offset = outside.standOffset(kAgentRadius);
		const glm::vec2	 middle = outside.standPoint(kAgentRadius);
		EXPECT_TRUE(outside.serves({3.0F, 2.0F - offset}, middle, kAgentRadius));
		EXPECT_TRUE(outside.serves({3.1F, 2.0F - offset + 0.2F}, middle, kAgentRadius)) << "a little off the stand point";
		EXPECT_FALSE(outside.serves({3.0F, 2.0F + offset}, middle, kAgentRadius)) << "across the wall's line, in the room";
		const auto		probes = outside.standPoints(kAgentRadius);
		const glm::vec2 nearEnd = *std::max_element(probes.begin(), probes.end(), [](glm::vec2 one, glm::vec2 other) { return one.x < other.x; });
		ASSERT_GT(nearEnd.x, 3.5F) << "the last stand point lies toward the east end";
		EXPECT_TRUE(outside.serves(nearEnd, nearEnd, kAgentRadius));
		EXPECT_FALSE(outside.serves({4.05F, nearEnd.y + 0.1F}, nearEnd, kAgentRadius)) << "past the wall's end, round the corner into the room";
		EXPECT_FALSE(outside.serves({3.0F, 2.0F - 2.5F * offset}, middle, kAgentRadius)) << "farther than the stand offset";
	}

	TEST_F(WallStandTest, AnUnknownSegmentHasNoStand) {
		EXPECT_TRUE(wallStands(world, 42).empty());
	}

} // namespace
