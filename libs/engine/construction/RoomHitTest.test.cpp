#include "RoomHitTest.h"

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <vector>

#include <gtest/gtest.h>

using namespace engine::construction;
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
	const std::vector<RoomHitCandidate> rooms{{7, &ring}};
	const auto							hit = roomAtPoint(Vec2i64{2000, 1500}, rooms);
	ASSERT_TRUE(hit.has_value());
	EXPECT_EQ(*hit, 7u);
}

TEST(RoomHitTest, ExteriorPointMisses) {
	const Ring							ring = rectangle();
	const std::vector<RoomHitCandidate> rooms{{7, &ring}};
	EXPECT_FALSE(roomAtPoint(Vec2i64{5000, 1500}, rooms).has_value());
}

TEST(RoomHitTest, BoundaryPointCountsAsHit) {
	const Ring							ring = rectangle();
	const std::vector<RoomHitCandidate> rooms{{7, &ring}};
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
	const std::vector<RoomHitCandidate> rooms{{3, &ring}};
	// Inside the leg of the L.
	ASSERT_TRUE(roomAtPoint(Vec2i64{1000, 1000}, rooms).has_value());
	// In the cut-out notch (top-right): a vertex-0 fan would wrongly report this as
	// inside; exact point-in-polygon reports Outside.
	EXPECT_FALSE(roomAtPoint(Vec2i64{3000, 3000}, rooms).has_value());
}

TEST(RoomHitTest, NestedRoomsHighestIdWins) {
	// A big room fully containing a smaller one; a click in the inner room lands in
	// both rings. The highest id wins (monotonic-id tie-break), regardless of the
	// candidate order.
	const Ring outer{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}};
	const Ring inner{{2000, 2000}, {6000, 2000}, {6000, 6000}, {2000, 6000}};

	const std::vector<RoomHitCandidate> idAscending{{1, &outer}, {9, &inner}};
	const auto							a = roomAtPoint(Vec2i64{4000, 4000}, idAscending);
	ASSERT_TRUE(a.has_value());
	EXPECT_EQ(*a, 9u);

	// Same result with the candidates in the opposite order.
	const std::vector<RoomHitCandidate> idDescending{{9, &inner}, {1, &outer}};
	const auto							b = roomAtPoint(Vec2i64{4000, 4000}, idDescending);
	ASSERT_TRUE(b.has_value());
	EXPECT_EQ(*b, 9u);

	// A click in the outer ring only (outside the inner) returns the outer id.
	const auto outerOnly = roomAtPoint(Vec2i64{8000, 8000}, idAscending);
	ASSERT_TRUE(outerOnly.has_value());
	EXPECT_EQ(*outerOnly, 1u);
}

TEST(RoomHitTest, EmptyAndDegenerateCandidatesMiss) {
	EXPECT_FALSE(roomAtPoint(Vec2i64{0, 0}, {}).has_value());

	const Ring							degenerate{{0, 0}, {1000, 0}}; // < 3 vertices
	const std::vector<RoomHitCandidate> rooms{{5, &degenerate}, {6, nullptr}};
	EXPECT_FALSE(roomAtPoint(Vec2i64{500, 0}, rooms).has_value());
}
