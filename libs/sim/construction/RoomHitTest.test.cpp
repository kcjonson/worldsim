#include "RoomHitTest.h"

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

using namespace construction;
using geometry::Ring;
using geometry::Vec2i64;

namespace {

	// 4m x 3m rectangle (CCW, integer mm), origin at (0,0).
	Ring rectangle() {
		return Ring{{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}};
	}

	// 4m x 4m L-shape: a full square with the top-right 2x2 quadrant cut out, so
	// (3000, 3000) is OUTSIDE the notch while (1000, 1000) is inside the leg.
	Ring lShape() {
		return Ring{{0, 0}, {4000, 0}, {4000, 2000}, {2000, 2000}, {2000, 4000}, {0, 4000}};
	}

} // namespace

TEST(RoomHitTest, InteriorPointHits) {
	const Ring							ring = rectangle();
	const std::vector<RoomHitCandidate> rooms{{7, &ring, nullptr}};
	const auto							hit = roomAtPoint(Vec2i64{2000, 1500}, rooms);
	ASSERT_TRUE(hit.has_value());
	EXPECT_EQ(*hit, 7u);
}

TEST(RoomHitTest, ExteriorPointMisses) {
	const Ring							ring = rectangle();
	const std::vector<RoomHitCandidate> rooms{{7, &ring, nullptr}};
	EXPECT_FALSE(roomAtPoint(Vec2i64{5000, 1500}, rooms).has_value());
}

TEST(RoomHitTest, BoundaryPointCountsAsHit) {
	const Ring							ring = rectangle();
	const std::vector<RoomHitCandidate> rooms{{7, &ring, nullptr}};
	// On the bottom edge (a vertex would also count) -- OnBoundary != Outside.
	const auto edge = roomAtPoint(Vec2i64{2000, 0}, rooms);
	ASSERT_TRUE(edge.has_value());
	EXPECT_EQ(*edge, 7u);
	const auto vertex = roomAtPoint(Vec2i64{0, 0}, rooms);
	ASSERT_TRUE(vertex.has_value());
	EXPECT_EQ(*vertex, 7u);
}

TEST(RoomHitTest, ConcaveRingRespectsNotch) {
	const Ring							ring = lShape();
	const std::vector<RoomHitCandidate> rooms{{3, &ring, nullptr}};
	// Inside the leg of the L.
	ASSERT_TRUE(roomAtPoint(Vec2i64{1000, 1000}, rooms).has_value());
	// In the cut-out notch (top-right): a vertex-0 fan would wrongly report this as
	// inside; exact point-in-polygon reports Outside.
	EXPECT_FALSE(roomAtPoint(Vec2i64{3000, 3000}, rooms).has_value());
}

namespace {

	// A room in a room, recorded the way RoomDetectionSystem does: the inner room's
	// ring is a hole (reversed, CW) of the outer room.
	struct NestedRooms {
		Ring			  outer{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}};
		std::vector<Ring> outerHoles{{{2000, 2000}, {2000, 6000}, {6000, 6000}, {6000, 2000}}};
		Ring			  inner{{2000, 2000}, {6000, 2000}, {6000, 6000}, {2000, 6000}};
	};

	std::uint64_t pick(const Vec2i64& click, const std::vector<RoomHitCandidate>& rooms) {
		const auto hit = roomAtPoint(click, rooms);
		EXPECT_TRUE(hit.has_value());
		return hit.value_or(0);
	}

} // namespace

TEST(RoomHitTest, ClickInInnerRoomBuiltFirstSelectsInnerRoom) {
	// The inner room formed first, so it has the lower id; geometry decides, not the id.
	const NestedRooms					rooms;
	const std::vector<RoomHitCandidate> candidates{{1, &rooms.inner, nullptr}, {2, &rooms.outer, &rooms.outerHoles}};
	EXPECT_EQ(pick(Vec2i64{4000, 4000}, candidates), 1u);
}

TEST(RoomHitTest, ClickInInnerRoomBuiltSecondSelectsInnerRoom) {
	const NestedRooms					rooms;
	const std::vector<RoomHitCandidate> candidates{{1, &rooms.outer, &rooms.outerHoles}, {2, &rooms.inner, nullptr}};
	EXPECT_EQ(pick(Vec2i64{4000, 4000}, candidates), 2u);

	const std::vector<RoomHitCandidate> reversed{{2, &rooms.inner, nullptr}, {1, &rooms.outer, &rooms.outerHoles}};
	EXPECT_EQ(pick(Vec2i64{4000, 4000}, reversed), 2u);
}

TEST(RoomHitTest, ClickInOuterRoomsOwnFloorSelectsOuterRoom) {
	const NestedRooms					rooms;
	const std::vector<RoomHitCandidate> innerFirst{{1, &rooms.inner, nullptr}, {2, &rooms.outer, &rooms.outerHoles}};
	const std::vector<RoomHitCandidate> innerSecond{{2, &rooms.inner, nullptr}, {1, &rooms.outer, &rooms.outerHoles}};
	EXPECT_EQ(pick(Vec2i64{8000, 8000}, innerFirst), 2u);
	EXPECT_EQ(pick(Vec2i64{8000, 8000}, innerSecond), 1u);
}

TEST(RoomHitTest, ClickOnSharedBoundaryGoesToHighestId) {
	// The inner room's ring is also the outer room's hole: a click on it touches both.
	const NestedRooms					rooms;
	const std::vector<RoomHitCandidate> innerFirst{{1, &rooms.inner, nullptr}, {2, &rooms.outer, &rooms.outerHoles}};
	const std::vector<RoomHitCandidate> innerSecond{{2, &rooms.inner, nullptr}, {1, &rooms.outer, &rooms.outerHoles}};
	EXPECT_EQ(pick(Vec2i64{2000, 4000}, innerFirst), 2u);
	EXPECT_EQ(pick(Vec2i64{2000, 4000}, innerSecond), 2u);

	// Two rooms side by side sharing a wall.
	const Ring							left{{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}};
	const Ring							right{{3000, 0}, {6000, 0}, {6000, 3000}, {3000, 3000}};
	const std::vector<RoomHitCandidate> sideBySide{{8, &right, nullptr}, {4, &left, nullptr}};
	EXPECT_EQ(pick(Vec2i64{3000, 1500}, sideBySide), 8u);
	EXPECT_EQ(pick(Vec2i64{1500, 1500}, sideBySide), 4u);
}

TEST(RoomHitTest, ThreeLevelNestSelectsTheRoomUnderTheClick) {
	const Ring							outer{{0, 0}, {9000, 0}, {9000, 9000}, {0, 9000}};
	const std::vector<Ring>				outerHoles{{{2000, 2000}, {2000, 7000}, {7000, 7000}, {7000, 2000}}};
	const Ring							middle{{2000, 2000}, {7000, 2000}, {7000, 7000}, {2000, 7000}};
	const std::vector<Ring>				middleHoles{{{4000, 4000}, {4000, 5000}, {5000, 5000}, {5000, 4000}}};
	const Ring							inner{{4000, 4000}, {5000, 4000}, {5000, 5000}, {4000, 5000}};
	const std::vector<RoomHitCandidate> candidates{
		{3, &outer, &outerHoles},
		{1, &inner, nullptr},
		{2, &middle, &middleHoles},
	};
	EXPECT_EQ(pick(Vec2i64{4500, 4500}, candidates), 1u);
	EXPECT_EQ(pick(Vec2i64{3000, 3000}, candidates), 2u);
	EXPECT_EQ(pick(Vec2i64{1000, 1000}, candidates), 3u);
	EXPECT_FALSE(roomAtPoint(Vec2i64{10000, 1000}, candidates).has_value());
}

TEST(RoomHitTest, EmptyAndDegenerateCandidatesMiss) {
	EXPECT_FALSE(roomAtPoint(Vec2i64{0, 0}, {}).has_value());

	const Ring							degenerate{{0, 0}, {1000, 0}}; // < 3 vertices
	const std::vector<RoomHitCandidate> rooms{{5, &degenerate, nullptr}, {6, nullptr, nullptr}};
	EXPECT_FALSE(roomAtPoint(Vec2i64{500, 0}, rooms).has_value());
}
