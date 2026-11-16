#include "RectangleSnap.h"

#include "ConstructionWorld.h"
#include "DrawingAids.h"

#include <assets/ConstructionRegistry.h>

#include <gtest/gtest.h>
#include <vector>

using namespace construction;
using assets::SnappingConfig;
using ::Foundation::Vec2;

namespace {

	constexpr float kGridTolerance = 1e-5F;

	void addFoundation(ConstructionWorld& world, const std::vector<Vec2>& ring) {
		ASSERT_TRUE(world.commitFoundation(ring, "Wood").ok());
	}

	// The oracle's single 4 x 3 building.
	ConstructionWorld shackWorld() {
		ConstructionWorld world;
		addFoundation(world, {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}, {0.0F, 3.0F}});
		return world;
	}

	// The prototype table's world: a rectangle, an L, and a diamond.
	ConstructionWorld tableWorld() {
		ConstructionWorld world;
		addFoundation(world, {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}, {0.0F, 3.0F}});
		addFoundation(world, {{10.0F, 0.0F}, {16.0F, 0.0F}, {16.0F, 2.0F}, {13.0F, 2.0F}, {13.0F, 5.0F}, {10.0F, 5.0F}});
		addFoundation(world, {{-6.0F, 0.0F}, {-4.0F, 2.0F}, {-6.0F, 4.0F}, {-8.0F, 2.0F}});
		return world;
	}

	RectangleCornerSnap
	snapCorner(const ConstructionWorld& world, Vec2 cursor, float spacing = 1.0F, bool adjacency = true, bool freeform = false) {
		SnappingConfig snapping;
		snapping.rectangleAdjacencySnaps = adjacency;
		DrawingAids aids;
		aids.rectangleGridSpacingMeters = spacing;
		return snapRectangleCorner(world, snapping, aids, cursor, freeform);
	}

} // namespace

TEST(RectangleSnap, RectangleOnGrid) {
	const ConstructionWorld	  world = shackWorld();
	const RectangleCornerSnap first = snapCorner(world, {6.3F, 0.8F});
	const RectangleCornerSnap second = snapCorner(world, {9.6F, 3.7F});
	EXPECT_EQ(first.kind, RectangleSnapKind::Grid);
	EXPECT_NEAR(first.point.x, 6.0F, kGridTolerance);
	EXPECT_NEAR(first.point.y, 1.0F, kGridTolerance);
	EXPECT_NEAR(second.point.x, 10.0F, kGridTolerance);
	EXPECT_NEAR(second.point.y, 4.0F, kGridTolerance);

	const auto shape = rectangleFromCorners(first.point, second.point);
	ASSERT_TRUE(shape.has_value());
	const geometry::Ring expected = {{6000, 1000}, {10000, 1000}, {10000, 4000}, {6000, 4000}};
	EXPECT_EQ(shape->ring, expected);
	EXPECT_EQ(shape->label, "4 m x 3 m");
	EXPECT_NEAR(shape->widthMeters, 4.0F, kGridTolerance);
	EXPECT_NEAR(shape->depthMeters, 3.0F, kGridTolerance);
}

TEST(RectangleSnap, RectangleHalfMetreGrid) {
	const ConstructionWorld	  world = shackWorld();
	const RectangleCornerSnap half = snapCorner(world, {6.3F, 0.8F}, 0.5F);
	EXPECT_EQ(half.kind, RectangleSnapKind::Grid);
	EXPECT_NEAR(half.point.x, 6.5F, kGridTolerance);
	EXPECT_NEAR(half.point.y, 1.0F, kGridTolerance);
}

TEST(RectangleSnap, RectangleAdjacency) {
	const ConstructionWorld	  world = shackWorld();
	const RectangleCornerSnap adjacent = snapCorner(world, {4.2F, 0.25F});
	EXPECT_EQ(adjacent.kind, RectangleSnapKind::Vertex);
	EXPECT_NEAR(adjacent.point.x, 4.0F, kGridTolerance);
	EXPECT_NEAR(adjacent.point.y, 0.0F, kGridTolerance);

	const RectangleCornerSnap off = snapCorner(world, {4.2F, 0.25F}, 1.0F, /*adjacency=*/false);
	EXPECT_EQ(off.kind, RectangleSnapKind::Grid);
	EXPECT_NEAR(off.point.x, 4.0F, kGridTolerance);
	EXPECT_NEAR(off.point.y, 0.0F, kGridTolerance);
}

TEST(RectangleSnap, RectangleAdjacencyOffSkipsEdgesToo) {
	const ConstructionWorld	  world = shackWorld();
	const RectangleCornerSnap off = snapCorner(world, {2.43F, 3.2F}, 1.0F, /*adjacency=*/false);
	EXPECT_EQ(off.kind, RectangleSnapKind::Grid);
	EXPECT_NEAR(off.point.x, 2.0F, kGridTolerance);
	EXPECT_NEAR(off.point.y, 3.0F, kGridTolerance);
}

TEST(RectangleSnap, RectangleAltReturnsRawCursor) {
	const ConstructionWorld	  world = shackWorld();
	const RectangleCornerSnap raw = snapCorner(world, {4.2F, 0.25F}, 1.0F, true, /*freeform=*/true);
	EXPECT_EQ(raw.kind, RectangleSnapKind::None);
	EXPECT_EQ(raw.point.x, 4.2F);
	EXPECT_EQ(raw.point.y, 0.25F);
}

TEST(RectangleSnap, RectangleEdgeRoundsAlongAxisAlignedEdge) {
	const ConstructionWorld world = shackWorld();
	// Top edge (y = 3): the foot is (2.43, 3) and the grid rounds x along the edge.
	const RectangleCornerSnap top = snapCorner(world, {2.43F, 3.2F});
	EXPECT_EQ(top.kind, RectangleSnapKind::Edge);
	EXPECT_NEAR(top.point.x, 2.0F, kGridTolerance);
	EXPECT_NEAR(top.point.y, 3.0F, kGridTolerance);
	// Right edge (x = 4): y rounds along it.
	const RectangleCornerSnap right = snapCorner(world, {4.15F, 1.6F});
	EXPECT_EQ(right.kind, RectangleSnapKind::Edge);
	EXPECT_NEAR(right.point.x, 4.0F, kGridTolerance);
	EXPECT_NEAR(right.point.y, 2.0F, kGridTolerance);
	// A rounding that would leave the edge is dropped and the foot stays.
	const RectangleCornerSnap pastEnd = snapCorner(world, {3.6F, 3.2F}, 5.0F);
	EXPECT_EQ(pastEnd.kind, RectangleSnapKind::Edge);
	EXPECT_NEAR(pastEnd.point.x, 3.6F, kGridTolerance);
	EXPECT_NEAR(pastEnd.point.y, 3.0F, kGridTolerance);
}

TEST(RectangleSnap, RectangleEdgeOnDiagonalKeepsFoot) {
	ConstructionWorld world;
	addFoundation(world, {{-6.0F, 0.0F}, {-4.0F, 2.0F}, {-6.0F, 4.0F}, {-8.0F, 2.0F}});
	// Edge (-6, 0) to (-4, 2), the line y = x + 6 reversed: x - y = -6 on it. The cursor
	// sits 0.2 m off the edge's middle, so the foot is (-5, 1) and nothing rounds.
	const RectangleCornerSnap hit = snapCorner(world, {-4.8586F, 0.8586F});
	EXPECT_EQ(hit.kind, RectangleSnapKind::Edge);
	EXPECT_NEAR(hit.point.y - hit.point.x, 6.0F, 1e-4F);
}

TEST(RectangleSnap, RectangleHalfwayRoundsTowardPositive) {
	const ConstructionWorld	  world;
	const RectangleCornerSnap snapped = snapCorner(world, {-2.5F, -3.5F});
	EXPECT_NEAR(snapped.point.x, -2.0F, kGridTolerance);
	EXPECT_NEAR(snapped.point.y, -3.0F, kGridTolerance);
}

TEST(RectangleSnap, RectangleFromCornersOrdersTheRing) {
	const auto shape = rectangleFromCorners({10.0F, 4.0F}, {6.0F, 1.0F});
	ASSERT_TRUE(shape.has_value());
	const geometry::Ring expected = {{6000, 1000}, {10000, 1000}, {10000, 4000}, {6000, 4000}};
	EXPECT_EQ(shape->ring, expected);
}

TEST(RectangleSnap, RectangleLabelTrimsTrailingZeros) {
	const auto fractional = rectangleFromCorners({0.0F, 0.0F}, {3.5F, 0.125F});
	ASSERT_TRUE(fractional.has_value());
	EXPECT_EQ(fractional->label, "3.5 m x 0.125 m");
	// 4.0625 is exactly between two millimetres: it rounds up, as the prototype's toFixed does.
	const auto halfway = rectangleFromCorners({0.0F, 0.0F}, {4.0625F, 2.0F});
	ASSERT_TRUE(halfway.has_value());
	EXPECT_EQ(halfway->label, "4.063 m x 2 m");
}

TEST(RectangleSnap, RectangleDegenerateReturnsNothing) {
	EXPECT_FALSE(rectangleFromCorners({2.0F, 2.0F}, {2.0F, 5.0F}).has_value());
	EXPECT_FALSE(rectangleFromCorners({2.0F, 2.0F}, {7.0F, 2.0F}).has_value());
	EXPECT_FALSE(rectangleFromCorners({2.0F, 2.0F}, {2.0F, 2.0F}).has_value());
	EXPECT_FALSE(rectangleFromCorners({2.0F, 2.0F}, {2.0005F, 5.0F}).has_value());
	EXPECT_TRUE(rectangleFromCorners({2.0F, 2.0F}, {2.01F, 5.0F}).has_value());
}

TEST(RectangleSnap, RectangleMatchesPrototype) {
	struct Case {
		Vec2			  cursor;
		bool			  freeform;
		float			  spacing;
		bool			  adjacency;
		RectangleSnapKind kind;
		Vec2			  point;
	};
	// The prototype's snapRectangle on tableWorld(), 20 inputs.
	const Case cases[] = {
		{{4.2F, 0.25F}, false, 1.0F, true, RectangleSnapKind::Vertex, {4.0F, 0.0F}},
		{{2.43F, 3.2F}, false, 1.0F, true, RectangleSnapKind::Edge, {2.0F, 3.0F}},
		{{-2.5F, -3.5F}, false, 1.0F, true, RectangleSnapKind::Grid, {-2.0F, -3.0F}},
		{{-2.5F, -3.5F}, false, 0.5F, true, RectangleSnapKind::Grid, {-2.5F, -3.5F}},
		{{13.1F, 3.4F}, false, 1.0F, true, RectangleSnapKind::Edge, {13.0F, 3.0F}},
		{{12.9F, 2.15F}, false, 0.5F, true, RectangleSnapKind::Vertex, {13.0F, 2.0F}},
		{{7.3F, 4.4F}, true, 1.0F, true, RectangleSnapKind::None, {7.3F, 4.4F}},
		{{4.2F, 0.25F}, false, 1.0F, false, RectangleSnapKind::Grid, {4.0F, 0.0F}},
		{{-5.0F, 0.2F}, false, 1.0F, true, RectangleSnapKind::Grid, {-5.0F, 0.0F}},
		{{3.9F, -0.2F}, false, 0.25F, true, RectangleSnapKind::Vertex, {4.0F, 0.0F}},
		{{9.344F, 0.743F}, false, 0.5F, true, RectangleSnapKind::Grid, {9.5F, 0.5F}},
		{{18.885F, 4.115F}, false, 0.5F, true, RectangleSnapKind::Grid, {19.0F, 4.0F}},
		{{-4.164F, 5.949F}, false, 0.5F, true, RectangleSnapKind::Grid, {-4.0F, 6.0F}},
		{{14.171F, 4.655F}, false, 1.0F, true, RectangleSnapKind::Grid, {14.0F, 5.0F}},
		{{13.229F, -0.27F}, false, 0.5F, true, RectangleSnapKind::Edge, {13.0F, 0.0F}},
		{{-2.455F, -0.119F}, false, 0.5F, true, RectangleSnapKind::Grid, {-2.5F, 0.0F}},
		{{15.397F, 5.596F}, false, 1.0F, true, RectangleSnapKind::Grid, {15.0F, 6.0F}},
		{{4.171F, 4.582F}, false, 1.0F, true, RectangleSnapKind::Grid, {4.0F, 5.0F}},
		{{-1.179F, 1.314F}, false, 0.5F, true, RectangleSnapKind::Grid, {-1.0F, 1.5F}},
		{{15.381F, 1.773F}, false, 1.0F, true, RectangleSnapKind::Edge, {15.0F, 2.0F}},
	};
	const ConstructionWorld world = tableWorld();
	for (const Case& c : cases) {
		SCOPED_TRACE(testing::Message() << "cursor (" << c.cursor.x << ", " << c.cursor.y << ") spacing " << c.spacing);
		const RectangleCornerSnap snapped = snapCorner(world, c.cursor, c.spacing, c.adjacency, c.freeform);
		EXPECT_EQ(snapped.kind, c.kind);
		EXPECT_NEAR(snapped.point.x, c.point.x, kGridTolerance);
		EXPECT_NEAR(snapped.point.y, c.point.y, kGridTolerance);
	}
}
