#include "WallStand.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

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

		// A Standard Wood wall from `a` to `b` (mm), built unless asked for a blueprint. A wall split by it
		// keeps its own state.
		SegmentId wall(Vec2i64 a, Vec2i64 b, bool built = true) {
			const SegmentCommitResult result = world.commitSegment(a, b, "Wood", "Standard", kInvalidFoundation, world.version());
			EXPECT_TRUE(result.ok());
			if (built) {
				for (const SegmentId id : result.createdSegments) {
					world.setSegmentState(id, FoundationState::Built);
				}
			}
			return between(a, b);
		}

		// The segment joining the vertices at `a` and `b`.
		SegmentId between(Vec2i64 a, Vec2i64 b) const {
			const VertexId from = world.vertexAt(a);
			const VertexId to = world.vertexAt(b);
			for (const SegmentId id : world.segmentsOfVertex(from)) {
				const WallSegment* segment = world.getSegment(id);
				if ((segment->v0 == from && segment->v1 == to) || (segment->v0 == to && segment->v1 == from)) {
					return id;
				}
			}
			ADD_FAILURE() << "no segment between the two points";
			return kInvalidSegment;
		}

		void opening(SegmentId host, float t, const char* type, bool built = true) {
			const OpeningId id = world.addOpening(host, t, type, "Wood");
			ASSERT_NE(id, kInvalidOpening);
			if (built) {
				world.setOpeningState(id, FoundationState::Built);
			}
		}

		// Three built walls of the square from (x0, y0) to (x1, y1) and the fourth, its left side
		// (x0, y1) to (x0, y0), as a blueprint: the segment that closes it.
		SegmentId squareClosedBy(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
			wall({x0, y0}, {x1, y0});
			wall({x1, y0}, {x1, y1});
			wall({x1, y1}, {x0, y1});
			return wall({x0, y1}, {x0, y0}, false);
		}

		ConstructionWorld world;
	};

	// Its one stand, which must lie toward `side` from the segment's midpoint.
	void expectOneStandToward(const std::vector<WallStand>& stands, glm::vec2 side) {
		ASSERT_EQ(stands.size(), 1U);
		EXPECT_GT(glm::dot(stands.front().standPoint(kAgentRadius) - stands.front().midpoint, side), 0.0F);
	}

	TEST_F(WallStandTest, AStandIsBesideTheWallClearOfItsBand) {
		const SegmentId segment = wall({0, 0}, {4000, 0}, false);
		const auto		stands = wallStands(world, segment);
		ASSERT_EQ(stands.size(), 2U);
		const float halfThickness = 0.1F; // Wood Standard, 0.20 m
		for (const WallStand& stand : stands) {
			EXPECT_NEAR(stand.midpoint.x, 2.0F, 1e-5F);
			EXPECT_NEAR(stand.midpoint.y, 0.0F, 1e-5F);
			EXPECT_NEAR(stand.halfThicknessMeters, halfThickness, 1e-5F);
			const glm::vec2 point = stand.standPoint(kAgentRadius);
			EXPECT_NEAR(std::abs(point.y), halfThickness + kAgentRadius + WallStand::kStandMarginMeters, 1e-5F);
			EXPECT_TRUE(stand.onSide(point));
			EXPECT_FALSE(wallBand(world, *world.getSegment(segment))->overlaps(point, kAgentRadius));
		}
		EXPECT_LT(glm::dot(stands[0].outward, stands[1].outward), 0.0F) << "one on each side";
	}

	TEST_F(WallStandTest, AnOpenChainLeavesBothSides) {
		wall({0, 0}, {4000, 0});
		wall({4000, 0}, {4000, 4000});
		EXPECT_EQ(wallStands(world, wall({4000, 4000}, {0, 4000}, false)).size(), 2U);
	}

	TEST_F(WallStandTest, TheSegmentClosingADoorlessSquareIsBuiltFromOutside) {
		expectOneStandToward(wallStands(world, squareClosedBy(0, 0, 4000, 4000)), {-1.0F, 0.0F});
	}

	TEST_F(WallStandTest, ALoopWithABuiltDoorLeavesBothSides) {
		const SegmentId closing = squareClosedBy(0, 0, 4000, 4000);
		opening(between({0, 0}, {4000, 0}), 0.5F, "Door");
		EXPECT_EQ(wallStands(world, closing).size(), 2U);
	}

	TEST_F(WallStandTest, ADoorBlueprintIsNoWayOut) {
		const SegmentId closing = squareClosedBy(0, 0, 4000, 4000);
		opening(between({0, 0}, {4000, 0}), 0.5F, "Door", false);
		expectOneStandToward(wallStands(world, closing), {-1.0F, 0.0F});
	}

	TEST_F(WallStandTest, AWindowIsNoWayOut) {
		const SegmentId closing = squareClosedBy(0, 0, 4000, 4000);
		opening(between({0, 0}, {4000, 0}), 0.5F, "Window");
		expectOneStandToward(wallStands(world, closing), {-1.0F, 0.0F});
	}

	// A divider closing across a room with one door: the side that keeps the door.
	TEST_F(WallStandTest, ADividerIsBuiltFromTheSideWithTheDoor) {
		wall({0, 0}, {6000, 0});
		wall({6000, 0}, {6000, 4000});
		wall({6000, 4000}, {0, 4000});
		wall({0, 4000}, {0, 0});
		opening(between({0, 0}, {6000, 0}), 0.25F, "Door"); // x = 1.5 m, west of the divider
		const SegmentId divider = wall({3000, 0}, {3000, 4000}, false);
		expectOneStandToward(wallStands(world, divider), {-1.0F, 0.0F});
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
		EXPECT_EQ(wallStands(world, squareClosedBy(3000, 3000, 6000, 6000)).size(), 2U);
	}

	TEST_F(WallStandTest, TheSegmentClosingAConcaveLoopIsBuiltFromOutside) {
		wall({0, 0}, {6000, 0});
		wall({6000, 0}, {6000, 3000});
		wall({6000, 3000}, {3000, 3000});
		wall({3000, 3000}, {3000, 6000});
		wall({3000, 6000}, {0, 6000});
		expectOneStandToward(wallStands(world, wall({0, 6000}, {0, 0}, false)), {-1.0F, 0.0F});
	}

	TEST_F(WallStandTest, EndsJoinedOnlyThroughUnbuiltWallsCloseNothing) {
		wall({0, 0}, {4000, 0});
		wall({4000, 0}, {4000, 4000}, false);
		wall({4000, 4000}, {0, 4000});
		EXPECT_EQ(wallStands(world, wall({0, 4000}, {0, 0}, false)).size(), 2U);
	}

	TEST_F(WallStandTest, AnUnknownSegmentHasNoStand) {
		EXPECT_TRUE(wallStands(world, 42).empty());
	}

} // namespace
