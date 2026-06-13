#include "SnapEngine.h"

#include "ConstructionWorld.h"

#include <assets/ConstructionRegistry.h>

#include <cmath>
#include <numbers>
#include <vector>
#include <gtest/gtest.h>

using namespace engine::construction;
using engine::assets::SnappingConfig;
using ::Foundation::Vec2;

namespace {

	// Defaults match assets/config/construction/snapping.xml: angle 15 deg,
	// vertexSnapRadius 0.4 m, edgeSnapRadius 0.3 m, originCloseRadius 0.5 m.
	SnappingConfig defaults() { return SnappingConfig{}; }

	float dist(Vec2 a, Vec2 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }

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
	const float		  len	 = 5.0F;
	const float		  off	 = 5.0F * (std::numbers::pi_v<float> / 180.0F); // 5 deg
	Vec2			  cursor{len * std::cos(off), len * std::sin(off)};
	auto			  r = engine.snap(points, cursor, /*freeform=*/false);
	EXPECT_EQ(r.kind, SnapKind::Angle);
	EXPECT_NEAR(r.point.y, 0.0F, 1e-3F);	  // snapped onto the axis
	EXPECT_NEAR(r.point.x, len, 1e-3F);		  // distance preserved
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
	auto			  r		 = engine.snap(points, {0.1F, 0.0F}, /*freeform=*/false);
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
