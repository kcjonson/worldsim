#include "SnapEngine.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>

#include <cmath>
#include <gtest/gtest.h>
#include <numbers>
#include <vector>

using namespace engine::construction;
using engine::assets::SnappingConfig;
using ::Foundation::Vec2;

namespace {

	// Defaults match assets/config/construction/snapping.xml: angle 15 deg,
	// vertexSnapRadius 0.4 m, edgeSnapRadius 0.3 m, originCloseRadius 0.5 m.
	SnappingConfig defaults() {
		return SnappingConfig{};
	}

	float dist(Vec2 a, Vec2 b) {
		return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
	}

} // namespace

TEST(SnapEngine, FirstPointFreeWhenNoGeometry) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	auto			  r = engine.snap({}, {3.3F, 2.7F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::None);
	EXPECT_FLOAT_EQ(r.point.x, 3.3F);
	EXPECT_FLOAT_EQ(r.point.y, 2.7F);
}

TEST(SnapEngine, AngleSnapsToAxis) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	// One point at origin; cursor near +x but a few degrees off. With no previous
	// segment the reference is the +x axis, so it should snap to 0 deg.
	std::vector<Vec2> points = {{0.0F, 0.0F}};
	const float		  len = 5.0F;
	const float		  off = 5.0F * (std::numbers::pi_v<float> / 180.0F); // 5 deg
	Vec2			  cursor{len * std::cos(off), len * std::sin(off)};
	auto			  r = engine.snap(points, cursor, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::Angle);
	EXPECT_NEAR(r.point.y, 0.0F, 1e-3F); // snapped onto the axis
	EXPECT_NEAR(r.point.x, len, 1e-3F);	 // distance preserved
}

TEST(SnapEngine, AngleSnapRelativeToPreviousSegment) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	// Previous segment goes straight up (+y). A cursor heading +x but a few
	// degrees off snaps to a 90 deg turn: pure +x from the corner.
	std::vector<Vec2> points = {{0.0F, 0.0F}, {0.0F, 5.0F}};
	Vec2			  cursor{points.back().x + 4.0F, points.back().y + 0.25F};
	auto			  r = engine.snap(points, cursor, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::Angle);
	EXPECT_NEAR(r.point.y, points.back().y, 1e-3F); // snapped flat off the corner
}

TEST(SnapEngine, FreeformSuppressesAngleSnap) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	std::vector<Vec2> points = {{0.0F, 0.0F}};
	Vec2			  cursor{5.0F, 0.44F}; // a few degrees off axis
	auto			  r = engine.snap(points, cursor, /*freeform=*/true);
	EXPECT_EQ(r.kind, SnapKind::None);
	EXPECT_FLOAT_EQ(r.point.y, 0.44F);
}

TEST(SnapEngine, OriginCloseWithinRadius) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	std::vector<Vec2> points = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}};
	// Cursor 0.3 m from origin, inside the 0.5 m close radius.
	auto r = engine.snap(points, {0.3F, 0.0F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::Origin);
	EXPECT_TRUE(r.closesShape());
	EXPECT_FLOAT_EQ(r.point.x, 0.0F);
	EXPECT_FLOAT_EQ(r.point.y, 0.0F);
}

TEST(SnapEngine, NoOriginCloseBeforeThreePoints) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	std::vector<Vec2> points = {{0.0F, 0.0F}, {5.0F, 0.0F}};
	auto			  r = engine.snap(points, {0.1F, 0.0F}, /*freeform=*/false);
	EXPECT_NE(r.kind, SnapKind::Origin);
}

TEST(SnapEngine, SnapsToExistingVertex) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	// Commit a square; its vertices become snap targets.
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 5.0F}};
	ASSERT_TRUE(world.commitFoundation(sq, "Wood").ok());

	SnapEngine engine(cfg, world);
	// Cursor 0.2 m from the (5,5) corner, inside the 0.4 m vertex radius. No
	// in-progress points, so vertex snap wins.
	auto r = engine.snap({}, {5.15F, 5.1F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::Vertex);
	EXPECT_FLOAT_EQ(r.point.x, 5.0F);
	EXPECT_FLOAT_EQ(r.point.y, 5.0F);
}

TEST(SnapEngine, SnapsToExistingEdge) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 5.0F}};
	ASSERT_TRUE(world.commitFoundation(sq, "Wood").ok());

	SnapEngine engine(cfg, world);
	// Cursor just outside the bottom edge midpoint (y = -0.2), within 0.3 m edge
	// radius but outside any vertex radius.
	auto r = engine.snap({}, {2.5F, -0.2F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::Edge);
	EXPECT_NEAR(r.point.y, 0.0F, 1e-4F);
	EXPECT_NEAR(r.point.x, 2.5F, 1e-4F);
	EXPECT_LT(dist(r.point, {2.5F, 0.0F}), 1e-3F);
}

// --- Wall snapping --------------------------------------------------------

namespace {

	geometry::Vec2i64 mm(float x, float y) {
		return geometry::quantize(Vec2{x, y});
	}

	// A 10x10 m foundation plus one interior wall from (2,2) to (8,2), Wood/Standard.
	// Returns the host foundation id via `host`.
	void buildHostAndWall(ConstructionWorld& world, FoundationId& host) {
		std::vector<Vec2> sq = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
		auto			  f = world.commitFoundation(sq, "Wood");
		ASSERT_TRUE(f.ok());
		host = f.id;
		auto seg = world.commitSegment(mm(2.0F, 2.0F), mm(8.0F, 2.0F), "Wood", "Standard", host);
		ASSERT_TRUE(seg.ok());
	}

} // namespace

TEST(SnapEngine, WallSnapFirstPointFreeWhenNoGeometry) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	auto			  r = engine.snapWall({}, {3.3F, 7.1F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::None);
	EXPECT_FLOAT_EQ(r.point.x, 3.3F);
	EXPECT_FLOAT_EQ(r.point.y, 7.1F);
}

TEST(SnapEngine, WallSnapsToWallEndpoint) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host);

	SnapEngine engine(cfg, world);
	// Cursor 0.2 m from the wall endpoint (8,2), inside the 0.4 m vertex radius.
	auto r = engine.snapWall({}, {8.15F, 2.1F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::WallEndpoint);
	EXPECT_FLOAT_EQ(r.point.x, 8.0F);
	EXPECT_FLOAT_EQ(r.point.y, 2.0F);
	// The hit vertex is read by the WallTool to join the existing wall exactly.
	EXPECT_NE(r.hitVertex, kInvalidVertex);
	EXPECT_EQ(r.hitSegment, kInvalidSegment);
	EXPECT_EQ(world.getVertex(r.hitVertex)->pos, mm(8.0F, 2.0F));
}

TEST(SnapEngine, WallEndpointBeatsFoundationVertex) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	// Foundation corner at (0,0); a wall endpoint also at (0,0). The wall endpoint
	// must win on priority even though both are equidistant.
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
	ASSERT_TRUE(world.commitFoundation(sq, "Wood").ok());
	ASSERT_TRUE(world.commitSegment(mm(0.0F, 0.0F), mm(5.0F, 3.0F), "Wood", "Standard", 1).ok());

	SnapEngine engine(cfg, world);
	auto	   r = engine.snapWall({}, {0.1F, 0.1F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::WallEndpoint);
	EXPECT_FLOAT_EQ(r.point.x, 0.0F);
	EXPECT_FLOAT_EQ(r.point.y, 0.0F);
}

TEST(SnapEngine, WallSnapsToFoundationVertexWhenNoWallNear) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host); // wall is at y=2, far from the (10,10) corner

	SnapEngine engine(cfg, world);
	// Cursor near the (10,10) foundation corner, no wall vertex within radius.
	auto r = engine.snapWall({}, {10.1F, 9.9F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::Vertex);
	EXPECT_FLOAT_EQ(r.point.x, 10.0F);
	EXPECT_FLOAT_EQ(r.point.y, 10.0F);
}

TEST(SnapEngine, WallSnapsToPointOnWallSegment) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host); // wall (2,2)->(8,2)

	SnapEngine engine(cfg, world);
	// Cursor 0.2 m off the wall midpoint (5,2), within edge radius, far from both
	// endpoints and from any foundation vertex/edge. T-junction target.
	auto r = engine.snapWall({}, {5.0F, 2.2F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::WallSegment);
	EXPECT_NEAR(r.point.x, 5.0F, 1e-3F);
	EXPECT_NEAR(r.point.y, 2.0F, 1e-3F);
	// hitSegment != kInvalidSegment is how the WallTool detects a T-junction.
	EXPECT_NE(r.hitSegment, kInvalidSegment);
	EXPECT_EQ(r.hitVertex, kInvalidVertex);
}

TEST(SnapEngine, WallSegmentEndpointDoesNotReportTJunction) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host); // wall (2,2)->(8,2)

	SnapEngine engine(cfg, world);
	// Cursor just past the (8,2) endpoint along the wall's axis (8.2,2): beyond the
	// 0.4 m vertex radius (0.2 m away, in radius -> would be an endpoint hit), so
	// move it OUTSIDE the vertex radius but still within edge radius of the segment's
	// end. The projection clamps to the endpoint, so this must NOT be WallSegment.
	// Place it 0.45 m past the end (outside the 0.4 m vertex radius) and 0.1 m off
	// axis: the perpendicular foot lands past t=1, so no interior point-on-wall.
	auto r = engine.snapWall({}, {8.45F, 2.1F}, /*freeform=*/false);
	EXPECT_NE(r.kind, SnapKind::WallSegment);
	EXPECT_EQ(r.hitSegment, kInvalidSegment);
}

TEST(SnapEngine, WallSegmentMidSpanReportsTJunction) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host); // wall (2,2)->(8,2)

	SnapEngine engine(cfg, world);
	// Mid-span (5,2), perpendicular foot strictly interior: a genuine T-junction.
	auto r = engine.snapWall({}, {5.0F, 2.25F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::WallSegment);
	EXPECT_NE(r.hitSegment, kInvalidSegment);
	EXPECT_EQ(r.hitVertex, kInvalidVertex);
	EXPECT_NEAR(r.point.x, 5.0F, 1e-3F);
	EXPECT_NEAR(r.point.y, 2.0F, 1e-3F);
}

TEST(SnapEngine, WallVertexBeatsPointOnSegment) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host); // endpoints (2,2) and (8,2)

	SnapEngine engine(cfg, world);
	// Near the (2,2) endpoint: both a wall-vertex and a point-on-segment target are
	// in range, the endpoint must win.
	auto r = engine.snapWall({}, {2.1F, 2.1F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::WallEndpoint);
	EXPECT_FLOAT_EQ(r.point.x, 2.0F);
	EXPECT_FLOAT_EQ(r.point.y, 2.0F);
}

TEST(SnapEngine, WallAngleSnapOffPreviousChainPoint) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world; // empty: no geometry to snap to, only angle snap
	SnapEngine		  engine(cfg, world);
	// Chain heading +y; cursor heading +x but a few degrees off snaps to a 90 deg
	// turn flat off the corner.
	std::vector<Vec2> points = {{1.0F, 1.0F}, {1.0F, 6.0F}};
	auto			  r = engine.snapWall(points, {5.0F, 6.25F}, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::Angle);
	EXPECT_NEAR(r.point.y, points.back().y, 1e-3F);
}

TEST(SnapEngine, WallFreeformSuppressesAngleSnap) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	std::vector<Vec2> points = {{1.0F, 1.0F}};
	auto			  r = engine.snapWall(points, {6.0F, 1.44F}, /*freeform=*/true);
	EXPECT_EQ(r.kind, SnapKind::None);
	EXPECT_FLOAT_EQ(r.point.y, 1.44F);
}

TEST(SnapEngine, FoundationSnapUnaffectedByWalls) {
	// A committed wall must not change foundation snap(): snap() ignores walls.
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host);

	SnapEngine engine(cfg, world);
	// Cursor right on the wall endpoint (8,2). snap() (foundation) must NOT report a
	// wall kind; the nearest foundation feature is the bottom edge, ~2 m away and
	// outside edge radius, so it falls through to angle/none, never WallEndpoint.
	auto r = engine.snap({}, {8.0F, 2.0F}, /*freeform=*/false);
	EXPECT_NE(r.kind, SnapKind::WallEndpoint);
	EXPECT_NE(r.kind, SnapKind::WallSegment);
}
