#include "SnapEngine.h"

#include "ConstructionValidator.h"
#include "ConstructionWorld.h"
#include "SnapTestFixtures.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>
#include <offset/WallOffset.h>

#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace construction;
using assets::ConstructionRegistry;
using assets::SnappingConfig;
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

	// Every helper on unless a case says otherwise (D12, Tests), set here so no test depends
	// on the player's defaults.
	const DrawingAids kAllAids = snap_test::guideAidsOn();

	SnapInput input(bool freeform = false, float originCloseRadiusMeters = -1.0F, const DrawingAids& aids = kAllAids) {
		return SnapInput{.aids = aids, .freeform = freeform, .originCloseRadiusMeters = originCloseRadiusMeters};
	}

	bool isGuide(SnapKind kind) {
		return kind == SnapKind::GuideLine || kind == SnapKind::GuideCrossing;
	}

	SnapInput wallInput(bool freeform, std::int64_t wallHalfThicknessMm, float originCloseRadiusMeters = -1.0F) {
		return SnapInput{
			.aids = kAllAids,
			.freeform = freeform,
			.originCloseRadiusMeters = originCloseRadiusMeters,
			.wallHalfThicknessMm = wallHalfThicknessMm,
		};
	}

} // namespace

TEST(SnapEngine, InputBuiltFromATemporaryAidsSurvivesACopy) {
	SnapInput built = input(/*freeform=*/false, /*originCloseRadiusMeters=*/-1.0F, DrawingAids{.angleSteps = true});
	SnapInput copy;
	copy = built;
	built.aids = DrawingAids{};
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	const auto		  r = engine.snap({{0.0F, 0.0F}}, {5.0F, 0.4F}, copy);
	EXPECT_EQ(r.kind, SnapKind::Angle);
}

TEST(SnapEngine, FirstPointFreeWhenNoGeometry) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	auto			  r = engine.snap({}, {3.3F, 2.7F}, input(/*freeform=*/false));
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
	auto			  r = engine.snap(points, cursor, input(/*freeform=*/false));
	EXPECT_EQ(r.kind, SnapKind::Angle);
	EXPECT_NEAR(r.point.y, 0.0F, 1e-3F); // snapped onto the axis
	EXPECT_NEAR(r.point.x, len, 1e-3F);	 // distance preserved
}

TEST(SnapEngine, GuideRightAngleOffPreviousSegment) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(config, world);
	// The 90-degree turn off the previous edge is the shape's perpendicular through the
	// previous point, caught by angle (2.9 degrees off it as seen from (6, 0)).
	std::vector<Vec2> points = {{0.0F, 0.0F}, {6.0F, 0.0F}};
	auto			  result = engine.snap(points, {6.2F, 4.0F}, input(/*freeform=*/false));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_NEAR(result.point.x, 6.0F, 1e-3F);
	EXPECT_NEAR(result.point.y, 4.0F, 1e-3F);
	ASSERT_EQ(result.guideCount, 1U);
	EXPECT_EQ(result.guides[0].source, GuideSource::DrawnShape);
	EXPECT_TRUE(result.guides[0].rightAngle);
}

TEST(SnapEngine, FreeformSuppressesGuidesAndAngleSteps) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(config, world);
	// One cursor on the north-south guide through the point, one a few degrees off +x that
	// only the angle step takes. Freeform (Alt) leaves both raw.
	const std::vector<Vec2>			points = {{0.0F, 0.0F}};
	const std::pair<Vec2, SnapKind> cases[] = {{{0.2F, 5.0F}, SnapKind::GuideLine}, {{5.0F, 0.44F}, SnapKind::Angle}};
	for (const auto& [cursor, snapped] : cases) {
		EXPECT_EQ(engine.snap(points, cursor, input(/*freeform=*/false)).kind, snapped);
		const SnapResult result = engine.snap(points, cursor, input(/*freeform=*/true));
		EXPECT_EQ(result.kind, SnapKind::None);
		EXPECT_EQ(result.guideCount, 0U);
		EXPECT_FLOAT_EQ(result.point.x, cursor.x);
		EXPECT_FLOAT_EQ(result.point.y, cursor.y);
	}
}

TEST(SnapEngine, OriginCloseWithinRadius) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	std::vector<Vec2> points = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}};
	// Cursor 0.3 m from origin, inside the 0.5 m close radius.
	auto r = engine.snap(points, {0.3F, 0.0F}, input(/*freeform=*/false));
	EXPECT_EQ(r.kind, SnapKind::Origin);
	EXPECT_TRUE(r.closesShape());
	EXPECT_FLOAT_EQ(r.point.x, 0.0F);
	EXPECT_FLOAT_EQ(r.point.y, 0.0F);
}

TEST(SnapEngine, NoOriginCloseBeforeThreePoints) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(config, world);
	// The shape's first edge line catches it instead.
	std::vector<Vec2> points = {{0.0F, 0.0F}, {6.0F, 0.0F}};
	auto			  result = engine.snap(points, {0.3F, 0.2F}, input(/*freeform=*/false));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_NEAR(result.point.x, 0.3F, 1e-4F);
	EXPECT_NEAR(result.point.y, 0.0F, 1e-4F);
}

TEST(SnapEngine, OriginCloseRadiusOverrideEnlargesCatchRange) {
	SnappingConfig	  cfg = defaults(); // originCloseRadius 0.5 m
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	std::vector<Vec2> points = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}};
	// 0.7 m from origin: outside the 0.5 m default, inside a 1.5 m override (the
	// zoom-stable radius the foundation tool passes so closing stays reachable when
	// zoomed out).
	auto r = engine.snap(points, {0.7F, 0.0F}, input(/*freeform=*/false, /*originCloseRadiusMeters=*/1.5F));
	EXPECT_EQ(r.kind, SnapKind::Origin);
	EXPECT_TRUE(r.closesShape());
}

TEST(SnapEngine, OriginCloseRadiusDefaultIgnoresOverride) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	std::vector<Vec2> points = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}};
	// Same 0.7 m cursor with no override (negative -> config 0.5 m): no close. Proves
	// the wider radius is an explicit per-call override, not a silent global change.
	auto r = engine.snap(points, {0.7F, 0.0F}, input(/*freeform=*/false));
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
	auto r = engine.snap({}, {5.15F, 5.1F}, input(/*freeform=*/false));
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
	auto r = engine.snap({}, {2.5F, -0.2F}, input(/*freeform=*/false));
	EXPECT_EQ(r.kind, SnapKind::Edge);
	EXPECT_NEAR(r.point.y, 0.0F, 1e-4F);
	EXPECT_NEAR(r.point.x, 2.5F, 1e-4F);
	EXPECT_LT(dist(r.point, {2.5F, 0.0F}), 1e-3F);
}

// --- Guides ------------------------------------------------------------------
// The world-axis lock is the world guide pair through each node; the cases are the
// tuned prototype's (D12, Tests). The rest of the guide cases are in SnapGuides.test.cpp.

TEST(SnapEngine, GuideShapeLinesCrossAtSquareCorner) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(config, world);
	std::vector<Vec2> points = {{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 6.0F}};
	auto			  result = engine.snap(points, {0.15F, 5.9F}, input());
	EXPECT_EQ(result.kind, SnapKind::GuideCrossing);
	EXPECT_NEAR(result.point.x, 0.0F, 1e-4F);
	EXPECT_NEAR(result.point.y, 6.0F, 1e-4F);
	ASSERT_EQ(result.guideCount, 2U);
	EXPECT_EQ(result.guides[0].source, GuideSource::DrawnShape);
	EXPECT_EQ(result.guides[1].source, GuideSource::DrawnShape);
}

TEST(SnapEngine, GuideToleranceBoundary) {
	SnappingConfig	  config = defaults(); // tolerance 0.3 m
	ConstructionWorld world;
	SnapEngine		  engine(config, world);
	// 0.35 m off the north-south line through the point: past the tolerance, so the angle step.
	auto result = engine.snap({{0.0F, 0.0F}}, {0.35F, 5.0F}, input());
	EXPECT_EQ(result.kind, SnapKind::Angle);
	EXPECT_NEAR(result.point.x, 0.0F, 1e-4F);
	EXPECT_NEAR(result.point.y, 5.012235F, 1e-4F);
}

TEST(SnapEngine, GuideCommittedCornersFollowAlignToExisting) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(std::vector<Vec2>{{3.0F, 10.0F}, {6.0F, 10.0F}, {6.0F, 12.0F}, {3.0F, 12.0F}}, "Wood").ok());
	SnapEngine engine(config, world);
	auto	   result = engine.snap({}, {-2.0F, 10.2F}, input());
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_NEAR(result.point.x, -2.0F, 1e-4F);
	EXPECT_NEAR(result.point.y, 10.0F, 1e-4F);
	DrawingAids alignOff = kAllAids;
	alignOff.alignToExistingBuildings = false;
	EXPECT_EQ(engine.snap({}, {-2.0F, 10.2F}, input(/*freeform=*/false, /*originCloseRadiusMeters=*/-1.0F, alignOff)).kind, SnapKind::None);
}

TEST(SnapEngine, GuideRangeExcludesFarCorners) {
	SnappingConfig	  config = defaults(); // guide range 8 m
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(std::vector<Vec2>{{3.0F, 10.0F}, {6.0F, 10.0F}, {6.0F, 12.0F}, {3.0F, 12.0F}}, "Wood").ok());
	SnapEngine engine(config, world);
	// The nearest corner, (3, 10), is 9 m away.
	EXPECT_EQ(engine.snap({}, {-6.0F, 10.2F}, input()).kind, SnapKind::None);
}

TEST(SnapEngine, AngleStepsOffFallsToRaw) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(config, world);
	// The AngleSnapsToAxis cursor, 5 degrees off +x: with angle steps off it stays raw.
	std::vector<Vec2> points = {{0.0F, 0.0F}};
	const Vec2		  cursor{5.0F * std::cos(0.0873F), 5.0F * std::sin(0.0873F)};
	DrawingAids		  aids = kAllAids;
	aids.angleSteps = false;
	auto result = engine.snap(points, cursor, input(/*freeform=*/false, /*originCloseRadiusMeters=*/-1.0F, aids));
	EXPECT_EQ(result.kind, SnapKind::None);
	EXPECT_EQ(result.guideCount, 0U);
	EXPECT_FLOAT_EQ(result.point.x, cursor.x);
	EXPECT_FLOAT_EQ(result.point.y, cursor.y);
}

TEST(SnapEngine, WorldGuidesOffLeaveTheAngleStep) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(config, world);
	// 0.15 m off the north-south line through the only point: with the world guides off
	// nothing else draws a line there, and the angle step takes it (90 degrees off +x,
	// distance kept).
	std::vector<Vec2> points = {{0.0F, 0.0F}};
	const Vec2		  cursor{0.15F, 3.0F};
	DrawingAids		  aids = kAllAids;
	aids.guidesFromWorldAxes = false;
	auto result = engine.snap(points, cursor, input(/*freeform=*/false, /*originCloseRadiusMeters=*/-1.0F, aids));
	EXPECT_EQ(result.kind, SnapKind::Angle);
	EXPECT_NEAR(result.point.x, 0.0F, 1e-4F);
	EXPECT_NEAR(result.point.y, dist(cursor, points.front()), 1e-4F);
	ASSERT_EQ(result.guideCount, 1U);
	EXPECT_EQ(result.guides[0].relation, GuideRelation::AngleStep);
	EXPECT_EQ(result.guides[0].source, GuideSource::WorldAxes);
	EXPECT_TRUE(result.guides[0].throughPreviousPoint);

	// Both off: nothing but the raw cursor.
	aids.angleSteps = false;
	EXPECT_EQ(engine.snap(points, cursor, input(/*freeform=*/false, /*originCloseRadiusMeters=*/-1.0F, aids)).kind, SnapKind::None);
}

TEST(SnapEngine, NearestFoundationCornerAndEdgeNameTheirFoundation) {
	ConstructionWorld world;
	ASSERT_TRUE(world.commitFoundation(std::vector<Vec2>{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}, {0.0F, 5.0F}}, "Wood").ok());
	const auto second = world.commitFoundation(std::vector<Vec2>{{10.0F, 0.0F}, {15.0F, 0.0F}, {15.0F, 5.0F}, {10.0F, 5.0F}}, "Wood");
	ASSERT_TRUE(second.ok());

	const auto corner = nearestFoundationCorner(world, {15.1F, 5.2F}, 0.4F);
	ASSERT_TRUE(corner.has_value());
	EXPECT_EQ(corner->foundation, second.id);
	EXPECT_EQ(geometry::quantize(corner->point), world.get(second.id)->ring[corner->vertexIndex]);
	EXPECT_FALSE(nearestFoundationCorner(world, {7.5F, 2.5F}, 0.4F).has_value());

	const auto edge = nearestFoundationEdge(world, {12.5F, -0.2F}, 0.3F);
	ASSERT_TRUE(edge.has_value());
	EXPECT_EQ(edge->foundation, second.id);
	EXPECT_NEAR(edge->point.x, 12.5F, 1e-4F);
	EXPECT_NEAR(edge->point.y, 0.0F, 1e-4F);
	EXPECT_NEAR(edge->distanceMeters, 0.2F, 1e-4F);
	EXPECT_FALSE(nearestFoundationEdge(world, {7.5F, 2.5F}, 0.3F).has_value());
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
		auto seg = world.commitSegment(mm(2.0F, 2.0F), mm(8.0F, 2.0F), "Wood", "Standard", host, world.version());
		ASSERT_TRUE(seg.ok());
	}

} // namespace

TEST(SnapEngine, WallSnapFirstPointFreeWhenNoGeometry) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	auto			  r = engine.snapWall({}, {3.3F, 7.1F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
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
	auto r = engine.snapWall({}, {8.15F, 2.1F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
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
	ASSERT_TRUE(world.commitSegment(mm(0.0F, 0.0F), mm(5.0F, 3.0F), "Wood", "Standard", 1, world.version()).ok());

	SnapEngine engine(cfg, world);
	auto	   r = engine.snapWall({}, {0.1F, 0.1F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
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
	// Cursor near the (10,10) foundation corner, no wall vertex within radius. The
	// wall tool insets the corner to the outer-face-flush miter (inward by the
	// 0.1 m half-thickness plus a small safety bias), so the snapped centerline sits
	// ON the foundation rather than on the perimeter. The miter of a right-angle
	// corner is inset equally on both axes: ~(9.9, 9.9), strictly inside the ring.
	auto r = engine.snapWall({}, {10.1F, 9.9F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_EQ(r.kind, SnapKind::Vertex);
	EXPECT_LT(r.point.x, 10.0F);
	EXPECT_LT(r.point.y, 10.0F);
	EXPECT_NEAR(r.point.x, 9.9F, 0.02F);
	EXPECT_NEAR(r.point.y, 9.9F, 0.02F);
	EXPECT_EQ(geometry::pointInPolygon(geometry::quantize(r.point), world.get(host)->ring), geometry::PointInPolygon::Inside);
}

TEST(SnapEngine, WallSnapsToPointOnWallSegment) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host); // wall (2,2)->(8,2)

	SnapEngine engine(cfg, world);
	// Cursor 0.2 m off the wall midpoint (5,2), within edge radius, far from both
	// endpoints and from any foundation vertex/edge. T-junction target.
	auto r = engine.snapWall({}, {5.0F, 2.2F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
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
	auto r = engine.snapWall({}, {8.45F, 2.1F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
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
	auto r = engine.snapWall({}, {5.0F, 2.25F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
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
	auto r = engine.snapWall({}, {2.1F, 2.1F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_EQ(r.kind, SnapKind::WallEndpoint);
	EXPECT_FLOAT_EQ(r.point.x, 2.0F);
	EXPECT_FLOAT_EQ(r.point.y, 2.0F);
}

TEST(SnapEngine, WallGuideRightAngleOffPreviousChainPoint) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world; // empty: only the chain's own lines
	SnapEngine		  engine(config, world);
	// Chain heading +y; a cursor heading +x 0.8 m off the corner's east-west line, 5.7
	// degrees off it as seen from the corner, is caught by angle on the chain's
	// perpendicular: a 90-degree turn flat off the corner.
	std::vector<Vec2> points = {{1.0F, 1.0F}, {1.0F, 6.0F}};
	auto			  result = engine.snapWall(points, {9.0F, 6.8F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_NEAR(result.point.x, 9.0F, 1e-3F);
	EXPECT_NEAR(result.point.y, points.back().y, 1e-3F);
	ASSERT_EQ(result.guideCount, 1U);
	EXPECT_TRUE(result.guides[0].rightAngle);
}

TEST(SnapEngine, WallFreeformSuppressesGuidesAndAngleSteps) {
	SnappingConfig	  config = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(config, world);
	// As FreeformSuppressesGuidesAndAngleSteps, for the wall tool.
	const std::vector<Vec2>			points = {{1.0F, 1.0F}};
	const std::pair<Vec2, SnapKind> cases[] = {{{1.2F, 6.0F}, SnapKind::GuideLine}, {{6.0F, 1.44F}, SnapKind::Angle}};
	for (const auto& [cursor, snapped] : cases) {
		EXPECT_EQ(engine.snapWall(points, cursor, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100)).kind, snapped);
		const SnapResult result = engine.snapWall(points, cursor, wallInput(/*freeform=*/true, /*wallHalfThicknessMm=*/100));
		EXPECT_EQ(result.kind, SnapKind::None);
		EXPECT_EQ(result.guideCount, 0U);
		EXPECT_FLOAT_EQ(result.point.x, cursor.x);
		EXPECT_FLOAT_EQ(result.point.y, cursor.y);
	}
}

// --- Closing a wall chain on its first point --------------------------------

namespace {

	// A chain of kMinClosingWallChainPoints or more whose first point is `first`; the rest sit far from it.
	std::vector<Vec2> chainFrom(Vec2 first) {
		return {first, {first.x + 4.0F, first.y}, {first.x + 4.0F, first.y + 4.0F}, {first.x, first.y + 4.0F}};
	}

} // namespace

TEST(SnapEngine, WallChainStartedOnAFoundationEdgeSnapsBackToItsFirstPoint) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
	ASSERT_TRUE(world.commitFoundation(sq, "Wood").ok());
	SnapEngine engine(cfg, world);

	const SnapResult start = engine.snapWall({}, {5.0F, -0.2F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	ASSERT_EQ(start.kind, SnapKind::Edge);
	const std::vector<Vec2> chain = chainFrom(start.point);
	// 0.38 m from the first point and 0.25 m from the edge: the edge would take it without the close.
	const Vec2 cursor{5.35F, 0.25F};
	ASSERT_LT(dist(cursor, chain.front()), cfg.originCloseRadiusMeters);
	const SnapResult r = engine.snapWall(chain, cursor, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_EQ(r.kind, SnapKind::Origin);
	EXPECT_TRUE(r.closesShape());
	EXPECT_EQ(r.point.x, chain.front().x);
	EXPECT_EQ(r.point.y, chain.front().y);
	EXPECT_TRUE(closesWallChain(chain, r.point));
}

TEST(SnapEngine, WallOriginCloseHonorsTheRadius) {
	SnappingConfig			cfg = defaults(); // originCloseRadius 0.5 m
	ConstructionWorld		world;
	SnapEngine				engine(cfg, world);
	const std::vector<Vec2> chain = chainFrom({2.0F, 2.0F});
	// 0.6 m off in x, 0.4 m off in y: outside the radius and both axis locks.
	const Vec2		 cursor{2.6F, 1.6F};
	const SnapResult outside = engine.snapWall(chain, cursor, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_NE(outside.kind, SnapKind::Origin);
	EXPECT_FALSE(closesWallChain(chain, outside.point));
	// The wall tool passes its zoom-stable radius, as the foundation tool does.
	const SnapResult enlarged =
		engine.snapWall(chain, cursor, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100, /*originCloseRadiusMeters=*/1.0F));
	EXPECT_EQ(enlarged.kind, SnapKind::Origin);
}

TEST(SnapEngine, WallNoOriginCloseBeforeMinClosingPoints) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	SnapEngine		  engine(cfg, world);
	// 0.35 m from the first point: inside the radius, outside the X axis lock.
	const std::vector<Vec2> chain = {{2.0F, 2.0F}, {6.0F, 2.0F}};
	ASSERT_LT(chain.size(), kMinClosingWallChainPoints);
	const SnapResult r = engine.snapWall(chain, {2.35F, 2.1F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_NE(r.kind, SnapKind::Origin);
	EXPECT_NE(r.point.x, chain.front().x);
}

TEST(SnapEngine, WallFreeformSkipsOriginClose) {
	SnappingConfig			cfg = defaults();
	ConstructionWorld		world;
	SnapEngine				engine(cfg, world);
	const std::vector<Vec2> chain = chainFrom({2.0F, 2.0F});
	const Vec2				cursor{2.35F, 2.1F};
	ASSERT_LT(dist(cursor, chain.front()), cfg.originCloseRadiusMeters);
	const SnapResult r = engine.snapWall(chain, cursor, wallInput(/*freeform=*/true, /*wallHalfThicknessMm=*/100));
	EXPECT_NE(r.kind, SnapKind::Origin);
	EXPECT_FALSE(r.closesShape());
	EXPECT_FALSE(closesWallChain(chain, r.point));
	EXPECT_EQ(r.point.x, cursor.x);
	EXPECT_EQ(r.point.y, cursor.y);
}

TEST(SnapEngine, FoundationOriginCloseIgnoresFreeform) {
	SnappingConfig			cfg = defaults();
	ConstructionWorld		world;
	SnapEngine				engine(cfg, world);
	const std::vector<Vec2> points = {{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 5.0F}};
	EXPECT_EQ(engine.snap(points, {0.3F, 0.0F}, input(/*freeform=*/true)).kind, SnapKind::Origin);
	EXPECT_EQ(engine.snap(points, {0.3F, 0.0F}, input(/*freeform=*/false)).kind, SnapKind::Origin);
}

TEST(SnapEngine, WallOriginCloseBeatsFoundationVertex) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
	ASSERT_TRUE(world.commitFoundation(sq, "Wood").ok());
	SnapEngine engine(cfg, world);
	// The cursor is 0.35 m from both the (0,0) corner and the first point.
	const std::vector<Vec2> chain = chainFrom({0.5F, 0.5F});
	const SnapResult		r = engine.snapWall(chain, {0.25F, 0.25F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_EQ(r.kind, SnapKind::Origin);
	EXPECT_EQ(r.point.x, chain.front().x);
	EXPECT_EQ(r.point.y, chain.front().y);
}

TEST(SnapEngine, WallOriginCloseBeatsWallEndpointAndSegment) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	FoundationId	  host = kInvalidFoundation;
	buildHostAndWall(world, host); // wall (2,2)->(8,2)
	SnapEngine engine(cfg, world);

	// 0.25 m from the (2,2) endpoint and from the first point.
	const std::vector<Vec2> nearEndpoint = chainFrom({2.4F, 2.3F});
	const SnapResult		overEndpoint =
		engine.snapWall(nearEndpoint, {2.2F, 2.15F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_EQ(overEndpoint.kind, SnapKind::Origin);
	EXPECT_EQ(overEndpoint.hitVertex, kInvalidVertex);

	// 0.2 m from the wall's interior and 0.3 m from the first point.
	const std::vector<Vec2> nearSegment = chainFrom({5.0F, 2.5F});
	const SnapResult overSegment = engine.snapWall(nearSegment, {5.0F, 2.2F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_EQ(overSegment.kind, SnapKind::Origin);
	EXPECT_EQ(overSegment.hitSegment, kInvalidSegment);
	EXPECT_EQ(overSegment.point.y, 2.5F);
}

// --- Outer-face-flush alignment -------------------------------------------

TEST(SnapEngine, WallSnapInsetsFromFoundationEdge) {
	SnappingConfig	  cfg = defaults(); // edgeSnapRadius 0.3 m
	ConstructionWorld world;
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
	auto			  f = world.commitFoundation(sq, "Wood");
	ASSERT_TRUE(f.ok());

	SnapEngine engine(cfg, world);
	// Cursor 0.2 m below the bottom-edge midpoint, within edge radius and far from
	// corners. The wall snap insets inward (+y) by ~0.1 m so the band sits ON the
	// foundation, instead of landing the centerline on the perimeter (y = 0).
	auto r = engine.snapWall({}, {5.0F, -0.2F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/100));
	EXPECT_EQ(r.kind, SnapKind::Edge);
	EXPECT_NEAR(r.point.x, 5.0F, 1e-3F);
	EXPECT_GT(r.point.y, 0.0F); // pushed inward, not on the boundary
	EXPECT_NEAR(r.point.y, 0.1F, 0.02F);
	EXPECT_EQ(geometry::pointInPolygon(geometry::quantize(r.point), world.get(f.id)->ring), geometry::PointInPolygon::Inside);
}

TEST(SnapEngine, WallSnapZeroThicknessDoesNotInset) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
	ASSERT_TRUE(world.commitFoundation(sq, "Wood").ok());

	SnapEngine engine(cfg, world);
	// A zero-thickness preset has no band to keep inside, so the snap lands exactly on
	// the edge (no inset) -- the degenerate case the validator accepts as on-boundary.
	auto r = engine.snapWall({}, {5.0F, -0.2F}, wallInput(/*freeform=*/false, /*wallHalfThicknessMm=*/0));
	EXPECT_EQ(r.kind, SnapKind::Edge);
	EXPECT_NEAR(r.point.x, 5.0F, 1e-4F);
	EXPECT_NEAR(r.point.y, 0.0F, 1e-4F);
}

TEST(SnapEngine, OuterFaceFlushCornerMitersInward) {
	ConstructionWorld world;
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {10.0F, 0.0F}, {10.0F, 10.0F}, {0.0F, 10.0F}};
	auto			  f = world.commitFoundation(sq, "Wood");
	ASSERT_TRUE(f.ok());
	const auto& ring = world.get(f.id)->ring;

	// Right-angle corner (10,10) at index 2: the miter insets equally on both axes.
	const Vec2 m = outerFaceFlushCorner(ring, 2, /*halfThicknessMm=*/100);
	EXPECT_LT(m.x, 10.0F);
	EXPECT_LT(m.y, 10.0F);
	EXPECT_NEAR(m.x, 9.9F, 0.02F);
	EXPECT_NEAR(m.y, 9.9F, 0.02F);
	EXPECT_EQ(geometry::pointInPolygon(geometry::quantize(m), ring), geometry::PointInPolygon::Inside);

	// Zero thickness returns the raw corner unchanged.
	const Vec2 raw = outerFaceFlushCorner(ring, 2, /*halfThicknessMm=*/0);
	EXPECT_FLOAT_EQ(raw.x, 10.0F);
	EXPECT_FLOAT_EQ(raw.y, 10.0F);
}

TEST(SnapEngine, OuterFaceFlushCornerOnDiagonalFoundationStaysInside) {
	ConstructionWorld world;
	// A diamond (square rotated 45 deg): every edge runs diagonally, so the inward
	// offset is irrational and exercises the millimeter-rounding safety bias.
	std::vector<Vec2> diamond = {{6.0F, 0.0F}, {12.0F, 6.0F}, {6.0F, 12.0F}, {0.0F, 6.0F}};
	auto			  f = world.commitFoundation(diamond, "Wood");
	ASSERT_TRUE(f.ok());
	const auto& ring = world.get(f.id)->ring;

	for (std::size_t i = 0; i < ring.size(); ++i) {
		const Vec2 m = outerFaceFlushCorner(ring, i, /*halfThicknessMm=*/100);
		EXPECT_EQ(geometry::pointInPolygon(geometry::quantize(m), ring), geometry::PointInPolygon::Inside)
			<< "miter for diamond corner " << i << " must be strictly inside";
	}
}

// --- Opening snapping ------------------------------------------------------

namespace {

	// A 20x20 m foundation plus one BUILT wall (2,6)->(2+len,6), Wood/Standard.
	// Returns the segment id, promoted to Built.
	SegmentId buildBuiltWall(ConstructionWorld& world, float lengthMeters) {
		std::vector<Vec2> sq = {{0.0F, 0.0F}, {20.0F, 0.0F}, {20.0F, 20.0F}, {0.0F, 20.0F}};
		auto			  f = world.commitFoundation(sq, "Wood");
		EXPECT_TRUE(f.ok());
		auto seg = world.commitSegment(mm(2.0F, 6.0F), mm(2.0F + lengthMeters, 6.0F), "Wood", "Standard", f.id, world.version());
		EXPECT_TRUE(seg.ok());
		EXPECT_TRUE(world.setSegmentState(seg.id, FoundationState::Built));
		return seg.id;
	}

} // namespace

TEST(SnapEngine, OpeningSnapsToNearestBuiltWall) {
	SnappingConfig	  cfg = defaults(); // edgeSnapRadius 0.3 m
	ConstructionWorld world;
	SegmentId		  seg = buildBuiltWall(world, 8.0F); // (2,6)->(10,6)

	SnapEngine engine(cfg, world);
	// Cursor 0.2 m off the wall at x=6 (midpoint), within edge radius. A 0.9 m door.
	auto r = engine.snapOpening({6.0F, 6.2F}, 0.9F);
	ASSERT_TRUE(r.valid);
	EXPECT_EQ(r.segment, seg);
	// Snapped point is the centerline point at t; cursor at x=6 -> t at (6-2)/8 = 0.5.
	EXPECT_NEAR(r.t, 0.5F, 1e-3F);
	EXPECT_NEAR(r.point.x, 6.0F, 1e-3F);
	EXPECT_NEAR(r.point.y, 6.0F, 1e-3F);
}

TEST(SnapEngine, OpeningFarFromAnyWallInvalid) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	buildBuiltWall(world, 8.0F); // wall at y=6

	SnapEngine engine(cfg, world);
	// Cursor 2 m off the wall, far outside the 0.3 m edge radius.
	auto r = engine.snapOpening({6.0F, 8.0F}, 0.9F);
	EXPECT_FALSE(r.valid);
}

TEST(SnapEngine, OpeningTClampedAwayFromEnds) {
	SnappingConfig	  cfg = defaults(); // openingMargin 0.3 m (struct default)
	ConstructionWorld world;
	SegmentId		  seg = buildBuiltWall(world, 6.0F); // (2,6)->(8,6)

	SnapEngine engine(cfg, world);
	// Cursor right at the v0 end (x=2). Raw projection t=0, but the clamp must push
	// it inward so a 0.9 m door (half 0.45) honors the 0.3 m end margin:
	// minimum t = (0.45 + 0.3) / 6 = 0.125.
	auto r = engine.snapOpening({2.0F, 6.1F}, 0.9F);
	ASSERT_TRUE(r.valid);
	EXPECT_EQ(r.segment, seg);
	EXPECT_NEAR(r.t, 0.125F, 1e-3F);
	// Snapped point is the centerline at the clamped t, not at the raw end.
	EXPECT_NEAR(r.point.x, 2.0F + 0.125F * 6.0F, 1e-2F);
}

TEST(SnapEngine, OpeningSnapsToBlueprintWall) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	// A wall left in the default Blueprint state (NOT promoted to Built).
	std::vector<Vec2> sq = {{0.0F, 0.0F}, {20.0F, 0.0F}, {20.0F, 20.0F}, {0.0F, 20.0F}};
	auto			  f = world.commitFoundation(sq, "Wood");
	ASSERT_TRUE(f.ok());
	auto seg = world.commitSegment(mm(2.0F, 6.0F), mm(10.0F, 6.0F), "Wood", "Standard", f.id, world.version());
	ASSERT_TRUE(seg.ok());

	SnapEngine engine(cfg, world);
	auto r = engine.snapOpening({6.0F, 6.1F}, 0.9F);
	ASSERT_TRUE(r.valid);
	EXPECT_EQ(r.segment, seg.id);
	EXPECT_NEAR(r.t, 0.5F, 1e-3F);
}

TEST(SnapEngine, OpeningSkipsWallTooShortForOpening) {
	SnappingConfig	  cfg = defaults();
	ConstructionWorld world;
	// A 1.0 m built wall cannot host a 0.9 m door + 2*0.3 m margins (needs 1.5 m).
	buildBuiltWall(world, 1.0F);

	SnapEngine engine(cfg, world);
	// Cursor on it: no valid placement exists, so snap is invalid.
	auto r = engine.snapOpening({2.5F, 6.1F}, 0.9F);
	EXPECT_FALSE(r.valid);
}

// The snap clamps to the range the validator accepts, at corners of any angle and
// thickness, a T-junction, and a free end (WOR-864). Sliding the cursor along a
// wall, every snapped spot passes validateOpening and keeps the door's edges the
// margin clear of the band ends the renderer draws (resolveWallBands); near the
// corner it stops exactly there, and a hair closer the validator refuses.
class OpeningSnapAtCorners : public ::testing::Test {
  protected:
	void SetUp() override {
		ConstructionRegistry::Get().clear();
		std::filesystem::path folder = __FILE__;
		folder = folder.parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "construction";
		ASSERT_TRUE(ConstructionRegistry::Get().load(folder.string()));
	}
	void TearDown() override { ConstructionRegistry::Get().clear(); }

	// The trims (meters) at `segment`'s v0 and v1 as the renderer resolves every wall.
	static std::pair<double, double> drawnTrims(const ConstructionWorld& world, SegmentId segment) {
		std::vector<geometry::WallSegment> walls;
		std::size_t						   target = 0;
		for (const WallSegment& s : world.segments()) {
			const auto* preset = ConstructionRegistry::Get().getThicknessPreset(s.material, s.thicknessPreset);
			if (s.id == segment) {
				target = walls.size();
			}
			walls.push_back({world.getVertex(s.v0)->pos, world.getVertex(s.v1)->pos, preset->halfThicknessMm});
		}
		const geometry::WallBands bands = geometry::resolveWallBands(walls, geometry::kDefaultMiterLimit);
		EXPECT_EQ(bands.status, geometry::OffsetStatus::Ok);
		return {static_cast<double>(bands.trimAtA[target]) / 1000.0, static_cast<double>(bands.trimAtB[target]) / 1000.0};
	}

	// Slide a door along `segment`, a built wall from (x0,y) to (x0+length,y), its v0
	// at x0, with the cursor 0.1 m below it.
	static void expectSnapMatchesValidator(
		const ConstructionWorld& world,
		SegmentId				 segment,
		float					 x0,
		float					 y,
		float					 length,
		const std::string&		 label
	) {
		const auto&	 cfg = ConstructionRegistry::Get();
		const double margin = static_cast<double>(cfg.constraints().openingMarginMeters);
		const double half = 0.45;
		const auto [trimV0, trimV1] = drawnTrims(world, segment);
		SnapEngine			  engine(cfg.snapping(), world);
		ConstructionValidator validator(cfg.constraints(), world);

		for (float along = 0.05F; along < length - 0.04F; along += 0.05F) {
			const OpeningSnap snap = engine.snapOpening({x0 + along, y - 0.1F}, 0.9F);
			ASSERT_TRUE(snap.valid) << label << " at " << along;
			ASSERT_EQ(snap.segment, segment) << label << " at " << along;
			EXPECT_TRUE(validator.validateOpening(segment, snap.t, "Door", "Wood").ok()) << label << " at " << along;
			const double centre = static_cast<double>(snap.t) * static_cast<double>(length);
			EXPECT_GE(centre - half - trimV0, margin - 1e-4) << label << " at " << along;
			EXPECT_GE(static_cast<double>(length) - centre - half - trimV1, margin - 1e-4) << label << " at " << along;
		}

		const OpeningSnap nearest = engine.snapOpening({x0 + 0.05F, y - 0.1F}, 0.9F);
		ASSERT_TRUE(nearest.valid) << label;
		EXPECT_NEAR(static_cast<double>(nearest.t) * static_cast<double>(length) - half - trimV0, margin, 2e-3) << label;
		const float closer = nearest.t - 0.002F / length;
		EXPECT_EQ(validator.validateOpening(segment, closer, "Door", "Wood").code, ValidationCode::OpeningMarginTooSmall) << label;
	}

	static FoundationId host(ConstructionWorld& world) {
		std::vector<Vec2> sq = {{0.0F, 0.0F}, {20.0F, 0.0F}, {20.0F, 20.0F}, {0.0F, 20.0F}};
		auto			  f = world.commitFoundation(sq, "Wood");
		EXPECT_TRUE(f.ok());
		return f.id;
	}
};

TEST_F(OpeningSnapAtCorners, SnapStopsWhereTheValidatorStops) {
	const std::vector<std::pair<std::string, std::string>> thicknesses = {
		{"Light", "Light"}, {"Standard", "Standard"}, {"Heavy", "Heavy"}, {"Heavy", "Light"}, {"Light", "Heavy"}
	};
	for (const float degrees : {30.0F, 35.0F, 45.0F, 60.0F, 90.0F, 135.0F}) {
		for (const auto& [thickness, otherThickness] : thicknesses) {
			ConstructionWorld  world;
			const FoundationId f = host(world);
			const float		   radians = degrees * std::numbers::pi_v<float> / 180.0F;
			auto			   seg = world.commitSegment(mm(4.0F, 4.0F), mm(8.0F, 4.0F), "Wood", thickness, f, world.version());
			auto			   other = world.commitSegment(
				  mm(4.0F, 4.0F),
				  mm(4.0F + 4.0F * std::cos(radians), 4.0F + 4.0F * std::sin(radians)),
				  "Wood",
				  otherThickness,
				  f,
				  world.version()
			  );
			ASSERT_TRUE(seg.ok());
			ASSERT_TRUE(other.ok());
			ASSERT_TRUE(world.setSegmentState(seg.id, FoundationState::Built));
			ASSERT_TRUE(world.setSegmentState(other.id, FoundationState::Built));
			expectSnapMatchesValidator(
				world, seg.id, 4.0F, 4.0F, 4.0F, std::to_string(static_cast<int>(degrees)) + " deg " + thickness + "/" + otherThickness
			);
		}
	}
}

TEST_F(OpeningSnapAtCorners, FreeEndAndStraightJoinSnapMarginPlusHalfWidthFromTheVertex) {
	ConstructionWorld  world;
	const FoundationId f = host(world);
	auto			   first = world.commitSegment(mm(2.0F, 6.0F), mm(6.0F, 6.0F), "Wood", "Standard", f, world.version());
	auto			   second = world.commitSegment(mm(6.0F, 6.0F), mm(10.0F, 6.0F), "Wood", "Standard", f, world.version());
	ASSERT_TRUE(first.ok());
	ASSERT_TRUE(second.ok());
	ASSERT_TRUE(world.setSegmentState(first.id, FoundationState::Built));
	ASSERT_TRUE(world.setSegmentState(second.id, FoundationState::Built));

	const SnapEngine engine(ConstructionRegistry::Get().snapping(), world);
	// Cursor at each wall's ends: the free end at x=2, the join at x=6 (either wall), the free end at x=10.
	const auto nearestCentre = [&](Vec2 cursor, SegmentId segment) {
		const OpeningSnap snap = engine.snapOpening(cursor, 0.9F);
		EXPECT_TRUE(snap.valid);
		EXPECT_EQ(snap.segment, segment);
		return static_cast<double>(snap.t) * 4.0;
	};
	EXPECT_NEAR(nearestCentre({2.0F, 6.1F}, first.id), 0.75, 1e-3);
	EXPECT_NEAR(nearestCentre({5.9F, 6.1F}, first.id), 4.0 - 0.75, 1e-3);
	EXPECT_NEAR(nearestCentre({6.1F, 6.1F}, second.id), 0.75, 1e-3);
	EXPECT_NEAR(nearestCentre({10.0F, 6.1F}, second.id), 4.0 - 0.75, 1e-3);
}

TEST_F(OpeningSnapAtCorners, SnapStopsWhereTheValidatorStopsAtATJunction) {
	ConstructionWorld  world;
	const FoundationId f = host(world);
	ASSERT_TRUE(world.commitSegment(mm(4.0F, 4.0F), mm(12.0F, 4.0F), "Wood", "Light", f, world.version()).ok());
	// A Heavy stem splits the Light wall at (8,4); the half ending there is the one under test.
	ASSERT_TRUE(world.commitSegment(mm(8.0F, 4.0F), mm(8.0F, 8.0F), "Wood", "Heavy", f, world.version()).ok());
	SegmentId through = kInvalidSegment;
	for (const WallSegment& s : world.segments()) {
		ASSERT_TRUE(world.setSegmentState(s.id, FoundationState::Built));
		if (world.getVertex(s.v0)->pos == mm(8.0F, 4.0F) && world.getVertex(s.v1)->pos == mm(12.0F, 4.0F)) {
			through = s.id;
		}
	}
	ASSERT_NE(through, kInvalidSegment);
	ASSERT_GT(drawnTrims(world, through).first, 0.0);
	expectSnapMatchesValidator(world, through, 8.0F, 4.0F, 4.0F, "T-junction");
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
	auto r = engine.snap({}, {8.0F, 2.0F}, input(/*freeform=*/false));
	EXPECT_NE(r.kind, SnapKind::WallEndpoint);
	EXPECT_NE(r.kind, SnapKind::WallSegment);
}
