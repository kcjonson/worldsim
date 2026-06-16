#include "NavInputBuilder.h"

#include "NavCoords.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <nav/NavMesh.h>
#include <nav/PathQuery.h>
#include <polygon/Polygon.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace engine::nav;
using engine::assets::AssetDefinition;
using engine::assets::AssetRegistry;
using engine::assets::CollisionShapeType;
using engine::assets::ConstructionRegistry;
using engine::assets::PlacedEntity;
using engine::assets::SpatialIndex;
using engine::construction::ConstructionWorld;
using engine::construction::FoundationState;
using engine::construction::kInvalidFoundation;
using engine::construction::OpeningId;
using engine::construction::SegmentCommitResult;
using engine::construction::SegmentId;
using geometry::Ring;
using geometry::Vec2i64;
using geometry::nav::DoorPortal;
using geometry::nav::NavInputPolygon;
using geometry::nav::NavMesh;
using geometry::nav::NavMeshInput;

namespace {

	// Project root from __FILE__: this file lives at <root>/libs/engine/nav/.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

	std::string constructionConfigFolder() {
		return (projectRoot() / "assets" / "config" / "construction").string();
	}

	// Doubled signed area in mm^2 (shoelace), exact-ish in double for these sizes.
	double signedArea2(const Ring& r) {
		double acc = 0.0;
		const std::size_t n = r.size();
		for (std::size_t i = 0; i < n; ++i) {
			const Vec2i64& a = r[i];
			const Vec2i64& b = r[(i + 1) % n];
			acc += static_cast<double>(a.x) * static_cast<double>(b.y) - static_cast<double>(b.x) * static_cast<double>(a.y);
		}
		return acc;
	}

	bool isCcw(const Ring& r) { return signedArea2(r) > 0.0; }
	bool isCw(const Ring& r) { return signedArea2(r) < 0.0; }

	double areaSqMeters(const Ring& r) {
		return std::abs(signedArea2(r)) / 2.0 / (1000.0 * 1000.0);
	}

	// Build a built wall segment (all created sub-segments marked Built).
	SegmentId buildWall(ConstructionWorld& cw, Vec2i64 a, Vec2i64 b) {
		SegmentCommitResult r = cw.commitSegment(a, b, "Wood", "Standard", kInvalidFoundation);
		EXPECT_TRUE(r.ok());
		for (SegmentId id : r.createdSegments) {
			cw.setSegmentState(id, FoundationState::Built);
		}
		return r.id;
	}

} // namespace

// ---------------------------------------------------------------------------
// Water
// ---------------------------------------------------------------------------

TEST(NavInputBuilder, Water_TenByTenBlock_OneCcwPolygon) {
	auto isWater = [](int x, int y) { return x >= 5 && x < 15 && y >= 5 && y < 15; };
	std::vector<NavInputPolygon> polys = extractWaterObstacles(64, 64, isWater, {0, 0});

	ASSERT_EQ(polys.size(), 1u);
	EXPECT_TRUE(polys[0].blocked);
	EXPECT_EQ(polys[0].provenanceId, kProvenanceWater);
	EXPECT_TRUE(isCcw(polys[0].ring));
	// 10x10 tiles = 100 m^2; simplify should collapse the staircase to ~4 corners.
	EXPECT_NEAR(areaSqMeters(polys[0].ring), 100.0, 1e-6);
	EXPECT_LE(polys[0].ring.size(), 6u);
}

TEST(NavInputBuilder, Water_WithLandIsland_OuterCcwInnerCw) {
	// 12x12 water block with a single land tile hole at (8,8).
	auto isWater = [](int x, int y) {
		const bool inBlock = x >= 4 && x < 16 && y >= 4 && y < 16;
		const bool hole = (x == 8 && y == 8);
		return inBlock && !hole;
	};
	std::vector<NavInputPolygon> polys = extractWaterObstacles(64, 64, isWater, {0, 0});

	ASSERT_EQ(polys.size(), 2u);
	for (const auto& p : polys) {
		EXPECT_TRUE(p.blocked);
		EXPECT_EQ(p.provenanceId, kProvenanceWater);
	}
	int ccw = 0;
	int cw = 0;
	for (const auto& p : polys) {
		if (isCcw(p.ring)) {
			++ccw;
		}
		if (isCw(p.ring)) {
			++cw;
		}
	}
	EXPECT_EQ(ccw, 1); // outer water boundary
	EXPECT_EQ(cw, 1);  // the land island hole
}

TEST(NavInputBuilder, Water_NoWater_NoPolygons) {
	auto none = [](int, int) { return false; };
	EXPECT_TRUE(extractWaterObstacles(32, 32, none, {0, 0}).empty());
}

TEST(NavInputBuilder, Water_OriginOffsetMapsToWorldMm) {
	auto isWater = [](int x, int y) { return x >= 0 && x < 2 && y >= 0 && y < 2; };
	const Vec2i64 origin{7000, 3000};
	std::vector<NavInputPolygon> polys = extractWaterObstacles(8, 8, isWater, origin);
	ASSERT_EQ(polys.size(), 1u);
	// Every vertex sits on the origin-shifted tile grid (multiples of 1000 mm + origin).
	for (const Vec2i64& v : polys[0].ring) {
		EXPECT_EQ((v.x - origin.x) % 1000, 0);
		EXPECT_EQ((v.y - origin.y) % 1000, 0);
		EXPECT_GE(v.x, origin.x);
		EXPECT_GE(v.y, origin.y);
	}
}

// ---------------------------------------------------------------------------
// Flora
// ---------------------------------------------------------------------------

TEST(NavInputBuilder, Flora_CircleEntity_EmitsOctagon) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName = "Test_NavTree";
	tree.collision.type = CollisionShapeType::Circle;
	tree.collision.radiusMeters = 0.1F;
	reg.registerTestDefinition(tree);

	SpatialIndex index(4.0F);
	PlacedEntity e;
	e.defName = "Test_NavTree";
	e.position = {10.0F, 20.0F};
	e.scale = 1.0F;
	index.insert(e);

	std::vector<NavInputPolygon> polys = extractFloraObstacles(index, reg);
	ASSERT_EQ(polys.size(), 1u);
	EXPECT_TRUE(polys[0].blocked);
	EXPECT_EQ(polys[0].provenanceId, kProvenanceTree);
	EXPECT_EQ(polys[0].ring.size(), 8u);
	EXPECT_TRUE(isCcw(polys[0].ring));

	// Centroid near the entity position (10,20) m -> (10000, 20000) mm.
	std::int64_t cx = 0;
	std::int64_t cy = 0;
	for (const Vec2i64& v : polys[0].ring) {
		cx += v.x;
		cy += v.y;
	}
	cx /= static_cast<std::int64_t>(polys[0].ring.size());
	cy /= static_cast<std::int64_t>(polys[0].ring.size());
	EXPECT_NEAR(static_cast<double>(cx), 10000.0, 60.0);
	EXPECT_NEAR(static_cast<double>(cy), 20000.0, 60.0);

	// Circumradius ~ 0.1 m + 50 mm pad = 150 mm.
	double maxR = 0.0;
	for (const Vec2i64& v : polys[0].ring) {
		const double dx = static_cast<double>(v.x - cx);
		const double dy = static_cast<double>(v.y - cy);
		maxR = std::max(maxR, std::sqrt(dx * dx + dy * dy));
	}
	EXPECT_NEAR(maxR, 150.0, 5.0);
}

TEST(NavInputBuilder, Flora_NoCollision_EmitsNothing) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition bush;
	bush.defName = "Test_NavBush";
	// collision defaults to None.
	reg.registerTestDefinition(bush);

	SpatialIndex index(4.0F);
	PlacedEntity e;
	e.defName = "Test_NavBush";
	e.position = {5.0F, 5.0F};
	index.insert(e);

	EXPECT_TRUE(extractFloraObstacles(index, reg).empty());
}

// ---------------------------------------------------------------------------
// Walls
// ---------------------------------------------------------------------------

class NavWallTest : public ::testing::Test {
  protected:
	void SetUp() override {
		ConstructionRegistry::Get().clear();
		ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
	}
	void TearDown() override { ConstructionRegistry::Get().clear(); }
};

TEST_F(NavWallTest, SingleHorizontalSegment_OneBandWithSegmentProvenance) {
	ConstructionWorld cw;
	SegmentId id = buildWall(cw, {0, 0}, {4000, 0});

	std::vector<NavInputPolygon> polys;
	std::vector<DoorPortal>		 doors;
	extractWalls(cw, ConstructionRegistry::Get(), polys, doors);

	ASSERT_EQ(polys.size(), 1u);
	EXPECT_TRUE(polys[0].blocked);
	EXPECT_EQ(polys[0].provenanceId, static_cast<std::int64_t>(id));
	EXPECT_TRUE(doors.empty());
	// 4 m long, 0.2 m thick (Standard) -> 0.8 m^2.
	EXPECT_NEAR(areaSqMeters(polys[0].ring), 0.8, 1e-3);
}

TEST_F(NavWallTest, LShape_TwoBandsPlusJunction_AreasSumToUnion) {
	ConstructionWorld cw;
	buildWall(cw, {0, 0}, {4000, 0});
	buildWall(cw, {4000, 0}, {4000, 3000});

	std::vector<NavInputPolygon> polys;
	std::vector<DoorPortal>		 doors;
	extractWalls(cw, ConstructionRegistry::Get(), polys, doors);

	int bands = 0;
	int junctions = 0;
	double total = 0.0;
	for (const auto& p : polys) {
		EXPECT_TRUE(p.blocked);
		total += areaSqMeters(p.ring);
		if (p.provenanceId == kProvenanceJunction) {
			++junctions;
		} else {
			EXPECT_GT(p.provenanceId, 0); // a real segment id
			++bands;
		}
	}
	EXPECT_EQ(bands, 2);
	EXPECT_EQ(junctions, 1);
	// 4 m and 3 m walls at 0.2 m thick: 0.8 + 0.6 = 1.4 m^2. resolveWallBands trims
	// each band back from the shared corner and fills the gap with the junction
	// polygon so the pieces tile with no overlap and no gap; the total therefore
	// equals the two full bands' area (the corner is counted exactly once). Allow a
	// small tolerance for mm rounding of the offset corners.
	EXPECT_NEAR(total, 1.4, 0.02);
}

TEST_F(NavWallTest, BlueprintSegment_EmitsNothing) {
	ConstructionWorld cw;
	SegmentCommitResult r = cw.commitSegment({0, 0}, {4000, 0}, "Wood", "Standard", kInvalidFoundation);
	ASSERT_TRUE(r.ok());
	// Leave state at Blueprint (do not mark Built).

	std::vector<NavInputPolygon> polys;
	std::vector<DoorPortal>		 doors;
	extractWalls(cw, ConstructionRegistry::Get(), polys, doors);
	EXPECT_TRUE(polys.empty());
	EXPECT_TRUE(doors.empty());
}

// ---------------------------------------------------------------------------
// Doors / windows
// ---------------------------------------------------------------------------

TEST_F(NavWallTest, Door_SplitsBandIntoTwoFlanksAndEmitsPortal) {
	ConstructionWorld cw;
	SegmentId id = buildWall(cw, {0, 0}, {4000, 0});
	OpeningId op = cw.addOpening(id, 0.5F, "Door", "Wood");
	ASSERT_NE(op, engine::construction::kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));

	std::vector<NavInputPolygon> polys;
	std::vector<DoorPortal>		 doors;
	extractWalls(cw, ConstructionRegistry::Get(), polys, doors);

	// The solid band is gone; two flanking rings remain for this segment.
	int flanks = 0;
	for (const auto& p : polys) {
		if (p.provenanceId == static_cast<std::int64_t>(id)) {
			++flanks;
		}
	}
	EXPECT_EQ(flanks, 2);

	ASSERT_EQ(doors.size(), 1u);
	EXPECT_EQ(doors[0].openingId, static_cast<std::int64_t>(op));
	EXPECT_EQ(doors[0].clearWidthMm, ConstructionRegistry::Get().getOpeningType("Door")->widthMm);
	// Jamb points straddle the centerline gap (~0.9 m apart along the wall).
	const double gap = std::sqrt(std::pow(static_cast<double>(doors[0].a.x - doors[0].b.x), 2) +
								 std::pow(static_cast<double>(doors[0].a.y - doors[0].b.y), 2));
	EXPECT_NEAR(gap, 900.0, 5.0);
}

TEST_F(NavWallTest, Window_LeavesBandSolidEmitsZeroWidthPortal) {
	ConstructionWorld cw;
	SegmentId id = buildWall(cw, {0, 0}, {4000, 0});
	OpeningId op = cw.addOpening(id, 0.5F, "Window", "Wood");
	ASSERT_NE(op, engine::construction::kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));

	std::vector<NavInputPolygon> polys;
	std::vector<DoorPortal>		 doors;
	extractWalls(cw, ConstructionRegistry::Get(), polys, doors);

	int solid = 0;
	for (const auto& p : polys) {
		if (p.provenanceId == static_cast<std::int64_t>(id)) {
			++solid;
		}
	}
	EXPECT_EQ(solid, 1); // band stays whole
	ASSERT_EQ(doors.size(), 1u);
	EXPECT_EQ(doors[0].clearWidthMm, 0); // window tagged not-pathable
}

// ---------------------------------------------------------------------------
// Border
// ---------------------------------------------------------------------------

TEST(NavInputBuilder, Border_CcwRectangleUnblocked) {
	NavInputPolygon b = borderRing({0, 0}, {10000, 8000});
	EXPECT_FALSE(b.blocked);
	EXPECT_EQ(b.provenanceId, kProvenanceBorder);
	ASSERT_EQ(b.ring.size(), 4u);
	EXPECT_TRUE(isCcw(b.ring));
	EXPECT_NEAR(areaSqMeters(b.ring), 80.0, 1e-6);
}

// ---------------------------------------------------------------------------
// End-to-end: extract -> build -> path query
// ---------------------------------------------------------------------------

class NavEndToEndTest : public ::testing::Test {
  protected:
	void SetUp() override {
		ConstructionRegistry::Get().clear();
		ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder()));
	}
	void TearDown() override { ConstructionRegistry::Get().clear(); }

	// A 4x3 m room of built walls with the south wall hosting an opening at t=0.5.
	// `pathable` chooses Door vs Window (window => no passage).
	NavMeshInput buildRoom(bool withDoor, bool pathableOpening) {
		ConstructionWorld cw;
		SegmentId south = buildWall(cw, {0, 0}, {4000, 0});
		buildWall(cw, {4000, 0}, {4000, 3000});
		buildWall(cw, {4000, 3000}, {0, 3000});
		buildWall(cw, {0, 3000}, {0, 0});
		if (withDoor) {
			OpeningId op = cw.addOpening(south, 0.5F, pathableOpening ? "Door" : "Window", "Wood");
			EXPECT_NE(op, engine::construction::kInvalidOpening);
			EXPECT_TRUE(cw.setOpeningState(op, FoundationState::Built));
		}

		NavMeshInput input;
		input.polygons.push_back(borderRing({-2000, -2000}, {6000, 5000}));
		extractWalls(cw, ConstructionRegistry::Get(), input.polygons, input.doors);
		return input;
	}
};

TEST_F(NavEndToEndTest, BuildsNonEmptyMeshAndDoorPathSucceeds) {
	NavMeshInput input = buildRoom(/*withDoor=*/true, /*pathableOpening=*/true);
	NavMesh mesh = geometry::nav::buildNavMesh(input);
	ASSERT_FALSE(mesh.triangles.empty());

	// Outside the room (below the south wall) to inside (room center). Agent radius
	// 0.3 m fits the 0.9 m door.
	const Vec2i64 outside{2000, -1000};
	const Vec2i64 inside{2000, 1500};
	geometry::nav::PathResult path = geometry::nav::pathThrough(mesh, outside, inside, 300);
	EXPECT_TRUE(path.reachable);
}

TEST_F(NavEndToEndTest, WindowBlocksPath) {
	NavMeshInput input = buildRoom(/*withDoor=*/true, /*pathableOpening=*/false);
	NavMesh mesh = geometry::nav::buildNavMesh(input);
	ASSERT_FALSE(mesh.triangles.empty());

	const Vec2i64 outside{2000, -1000};
	const Vec2i64 inside{2000, 1500};
	geometry::nav::PathResult path = geometry::nav::pathThrough(mesh, outside, inside, 300);
	EXPECT_FALSE(path.reachable); // a window is solid wall to pathing
}

TEST_F(NavEndToEndTest, NoOpeningBlocksPath) {
	NavMeshInput input = buildRoom(/*withDoor=*/false, /*pathableOpening=*/false);
	NavMesh mesh = geometry::nav::buildNavMesh(input);
	ASSERT_FALSE(mesh.triangles.empty());

	const Vec2i64 outside{2000, -1000};
	const Vec2i64 inside{2000, 1500};
	geometry::nav::PathResult path = geometry::nav::pathThrough(mesh, outside, inside, 300);
	EXPECT_FALSE(path.reachable);
}
