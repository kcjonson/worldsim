#include "NavInputBuilder.h"

#include "NavCoords.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <nav/NavMesh.h>
#include <nav/PathQuery.h>
#include <polygon/Polygon.h>

#include <world/Biome.h>
#include <world/BiomeWeights.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/ChunkSampleResult.h>
#include <world/chunk/IWorldSampler.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
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

// Flora tests register into the global AssetRegistry singleton; clear it before
// and after each so they neither inherit nor leak definitions across the suite.
class NavFloraTest : public ::testing::Test {
  protected:
	void SetUp() override { AssetRegistry::Get().clearDefinitions(); }
	void TearDown() override { AssetRegistry::Get().clearDefinitions(); }
};

TEST_F(NavFloraTest, CircleEntity_EmitsOctagon) {
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

TEST_F(NavFloraTest, NoCollision_EmitsNothing) {
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
	SegmentId a = buildWall(cw, {0, 0}, {4000, 0});
	SegmentId b = buildWall(cw, {4000, 0}, {4000, 3000});

	std::vector<NavInputPolygon> polys;
	std::vector<DoorPortal>		 doors;
	extractWalls(cw, ConstructionRegistry::Get(), polys, doors);

	// Two trimmed wall bands plus one junction polygon. For belief, the junction is
	// now tagged with a representative INCIDENT segment id (the smaller of its two
	// walls), NOT the old always-block kProvenanceJunction sentinel -- so a junction
	// is gated by knowing one of its walls, consistent with the wall rule. Every
	// polygon therefore carries a positive segment id; identify the junction as the
	// smallest-area piece.
	ASSERT_EQ(polys.size(), 3u);
	double total = 0.0;
	double smallestArea = 1e30;
	std::int64_t junctionProv = 0;
	for (const auto& p : polys) {
		EXPECT_TRUE(p.blocked);
		EXPECT_GT(p.provenanceId, 0) << "bands and junction all carry a wall segment id";
		EXPECT_EQ(p.openingId, geometry::nav::kNoOpening);
		const double area = areaSqMeters(p.ring);
		total += area;
		if (area < smallestArea) {
			smallestArea = area;
			junctionProv = p.provenanceId;
		}
	}
	const std::int64_t smallerSeg = std::min(static_cast<std::int64_t>(a), static_cast<std::int64_t>(b));
	EXPECT_EQ(junctionProv, smallerSeg) << "junction is tagged with the smaller incident segment id";
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

TEST_F(NavWallTest, Door_EmitsFullFootprintWithTaggedDoorSpan) {
	ConstructionWorld cw;
	SegmentId id = buildWall(cw, {0, 0}, {4000, 0});
	OpeningId op = cw.addOpening(id, 0.5F, "Door", "Wood");
	ASSERT_NE(op, engine::construction::kInvalidOpening);
	ASSERT_TRUE(cw.setOpeningState(op, FoundationState::Built));

	std::vector<NavInputPolygon> polys;
	std::vector<DoorPortal>		 doors;
	extractWalls(cw, ConstructionRegistry::Get(), polys, doors);

	// The gap is no longer physically cut: the wall is now its FULL band footprint,
	// split into two SOLID flank spans (openingId == kNoOpening) and one PATHABLE door
	// span (openingId == op). Belief gating happens at query time, not here. The
	// solid spans block when the wall is known; the door span passes in truth and when
	// the agent knows the opening.
	int solid = 0;
	int doorSpans = 0;
	for (const auto& p : polys) {
		if (p.provenanceId != static_cast<std::int64_t>(id)) {
			continue;
		}
		if (p.openingId == static_cast<std::int64_t>(op)) {
			++doorSpans;
		} else {
			EXPECT_EQ(p.openingId, geometry::nav::kNoOpening);
			++solid;
		}
	}
	EXPECT_EQ(solid, 2) << "two solid flanks flank the door";
	EXPECT_EQ(doorSpans, 1) << "one door-span sub-polygon tagged with the opening";

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

// ---------------------------------------------------------------------------
// Area-scoped buildInput (Phase A: simulation-area nav build)
// ---------------------------------------------------------------------------

namespace {

	using engine::assets::AsyncChunkPlacementResult;
	using engine::assets::PlacementExecutor;
	using engine::world::Biome;
	using engine::world::BiomeWeights;
	using engine::world::ChunkCoordinate;
	using engine::world::ChunkManager;
	using engine::world::ChunkSampleResult;
	using engine::world::IWorldSampler;
	using engine::world::kChunkSize;
	using engine::world::WorldPosition;

	// A sampler whose tiles are all Lake (water) inside a chosen set of chunk
	// coordinates and land (TemperateGrassland) everywhere else. Drives the area
	// build's water predicate deterministically without leaning on noise.
	class WaterRegionSampler : public IWorldSampler {
	  public:
		explicit WaterRegionSampler(std::vector<ChunkCoordinate> waterChunks)
			: m_waterChunks(std::move(waterChunks)) {}

		[[nodiscard]] ChunkSampleResult sampleChunk(ChunkCoordinate coord) const override {
			const Biome b = isWaterChunk(coord) ? Biome::Lake : Biome::TemperateGrassland;
			ChunkSampleResult r;
			for (auto& cb : r.cornerBiomes) {
				cb = BiomeWeights::single(b);
			}
			r.cornerElevations = {1.0F, 1.0F, 1.0F, 1.0F};
			r.computeSectorGrid();
			return r;
		}

		[[nodiscard]] float	   sampleElevation(WorldPosition) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return 99u; }

	  private:
		[[nodiscard]] bool isWaterChunk(ChunkCoordinate coord) const {
			for (const ChunkCoordinate& c : m_waterChunks) {
				if (c == coord) {
					return true;
				}
			}
			return false;
		}
		std::vector<ChunkCoordinate> m_waterChunks;
	};

	// Stand up a ChunkManager with the given sampler, load+finish a generous radius
	// around the origin so every chunk the area touches is ready.
	std::unique_ptr<ChunkManager> readyChunks(std::unique_ptr<IWorldSampler> sampler, int loadRadius = 3) {
		auto mgr = std::make_unique<ChunkManager>(std::move(sampler));
		mgr->setLoadRadius(loadRadius);
		mgr->setUnloadRadius(loadRadius + 2);
		mgr->update({0.0F, 0.0F});
		mgr->finishPendingGeneration();
		return mgr;
	}

	// Count the blocked flora rings (provenance tree) in an input.
	int countFlora(const NavMeshInput& input) {
		int n = 0;
		for (const NavInputPolygon& p : input.polygons) {
			if (p.provenanceId == kProvenanceTree) {
				++n;
			}
		}
		return n;
	}

	// The single unblocked border ring.
	const NavInputPolygon* findBorder(const NavMeshInput& input) {
		for (const NavInputPolygon& p : input.polygons) {
			if (!p.blocked && p.provenanceId == kProvenanceBorder) {
				return &p;
			}
		}
		return nullptr;
	}

} // namespace

// The border ring exactly matches the requested area AABB.
TEST_F(NavFloraTest, Area_BorderEqualsAreaBounds) {
	auto mgr = readyChunks(std::make_unique<WaterRegionSampler>(std::vector<ChunkCoordinate>{}));
	PlacementExecutor placement(AssetRegistry::Get());
	ConstructionWorld cw;

	const Vec2i64	   center{100000, 100000};
	const std::int64_t radius = 60000;
	NavMeshInput	   input  = buildInput(center, radius, *mgr, placement, AssetRegistry::Get(), cw,
										   ConstructionRegistry::Get());

	const NavInputPolygon* border = findBorder(input);
	ASSERT_NE(border, nullptr);
	ASSERT_EQ(border->ring.size(), 4u);
	std::int64_t minX = border->ring[0].x;
	std::int64_t minY = border->ring[0].y;
	std::int64_t maxX = border->ring[0].x;
	std::int64_t maxY = border->ring[0].y;
	for (const Vec2i64& v : border->ring) {
		minX = std::min(minX, v.x);
		minY = std::min(minY, v.y);
		maxX = std::max(maxX, v.x);
		maxY = std::max(maxY, v.y);
	}
	EXPECT_EQ(minX, center.x - radius);
	EXPECT_EQ(minY, center.y - radius);
	EXPECT_EQ(maxX, center.x + radius);
	EXPECT_EQ(maxY, center.y + radius);
}

// The area scopes flora: trees are placed across several chunks, most far outside
// the area; the build emits rings ONLY for the in-area trees, bounded (not all).
TEST_F(NavFloraTest, Area_ScopesFloraToInAreaTrees) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName			   = "Test_AreaTree";
	tree.collision.type		   = CollisionShapeType::Circle;
	tree.collision.radiusMeters = 0.2F;
	reg.registerTestDefinition(tree);

	auto mgr = readyChunks(std::make_unique<WaterRegionSampler>(std::vector<ChunkCoordinate>{}));
	PlacementExecutor placement(reg);

	// Lay a grid of trees over chunks (0,0) and (1,0): one every 8 m across a wide
	// span (covers x in [0, 1024) m, y in [0, 512) m). The area is a small box, so
	// only a handful fall inside it. Trees are split into their owning chunk's index.
	int totalTrees = 0;
	std::unordered_map<ChunkCoordinate, AsyncChunkPlacementResult> byChunk;
	for (int ty = 0; ty < 512; ty += 8) {
		for (int tx = 0; tx < 1024; tx += 8) {
			const WorldPosition wp{static_cast<float>(tx), static_cast<float>(ty)};
			const ChunkCoordinate coord = engine::world::worldToChunk(wp);
			AsyncChunkPlacementResult& r = byChunk[coord];
			r.coord = coord;
			PlacedEntity e;
			e.defName  = "Test_AreaTree";
			e.position = {wp.x, wp.y};
			r.spatialIndex.insert(e);
			++totalTrees;
		}
	}
	for (auto& [coord, result] : byChunk) {
		placement.storeChunkResult(std::move(result));
	}
	ASSERT_GT(totalTrees, 1000) << "scenario must place many trees so scoping is meaningful";

	ConstructionWorld cw;
	// Small area centered at (256, 256) m: a 20 m half-extent box [236, 276] m on each
	// axis -> a 40 m square holds ~ (40/8)^2 = 25 trees, far below the total.
	const Vec2i64	   center{256000, 256000};
	const std::int64_t radius = 20000;
	NavMeshInput	   input  = buildInput(center, radius, *mgr, placement, reg, cw, ConstructionRegistry::Get());

	const int flora = countFlora(input);
	EXPECT_GT(flora, 0) << "in-area trees must be emitted";
	EXPECT_LT(flora, totalTrees / 4) << "the area must NOT ingest all loaded trees";

	// Every emitted flora ring centroid sits within a tile of the area (queryRect can
	// return entities slightly outside its bounds since cells are 4 m, but never far).
	for (const NavInputPolygon& p : input.polygons) {
		if (p.provenanceId != kProvenanceTree) {
			continue;
		}
		std::int64_t cx = 0;
		std::int64_t cy = 0;
		for (const Vec2i64& v : p.ring) {
			cx += v.x;
			cy += v.y;
		}
		cx /= static_cast<std::int64_t>(p.ring.size());
		cy /= static_cast<std::int64_t>(p.ring.size());
		EXPECT_GE(cx, center.x - radius - 8000);
		EXPECT_LE(cx, center.x + radius + 8000);
		EXPECT_GE(cy, center.y - radius - 8000);
		EXPECT_LE(cy, center.y + radius + 8000);
	}
}

// Water spanning a chunk seam comes out as ONE continuous loop, not two stitched at
// the boundary. Chunks (0,0) and (1,0) are fully water, the surrounding land closes
// the loop; the area straddles the x=512 m seam.
TEST_F(NavFloraTest, Area_WaterSpansChunkSeamAsOneLoop) {
	std::vector<ChunkCoordinate> water = {{0, 0}, {1, 0}};
	auto mgr = readyChunks(std::make_unique<WaterRegionSampler>(water));
	PlacementExecutor placement(AssetRegistry::Get());
	ConstructionWorld cw;

	// Center on the seam (x = 512 m) with a 200 m half-extent so the box reaches into
	// both water chunks and the land chunks above/below, so the water loop closes.
	const Vec2i64	   center{512000, 256000};
	const std::int64_t radius = 200000;
	NavMeshInput	   input  = buildInput(center, radius, *mgr, placement, AssetRegistry::Get(), cw,
										   ConstructionRegistry::Get());

	int waterLoops = 0;
	for (const NavInputPolygon& p : input.polygons) {
		if (p.provenanceId == kProvenanceWater) {
			++waterLoops;
		}
	}
	// One continuous water boundary across the seam (no per-chunk fragments). The
	// two-water-chunk block is a single rectangle of water bounded by land, so the
	// marching-squares pass yields exactly one outer loop.
	EXPECT_EQ(waterLoops, 1) << "water across the chunk seam must be a single loop";
}

// Two builds of an unchanged world produce byte-identical inputs (deterministic
// ordering: border, water, flora (chunks (x,y) sorted, entities (pos,defName)
// sorted), walls).
TEST_F(NavFloraTest, Area_DeterministicOrdering) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName			   = "Test_DetTree";
	tree.collision.type		   = CollisionShapeType::Circle;
	tree.collision.radiusMeters = 0.2F;
	reg.registerTestDefinition(tree);

	std::vector<ChunkCoordinate> water = {{0, 0}};
	auto mgr = readyChunks(std::make_unique<WaterRegionSampler>(water));
	PlacementExecutor placement(reg);

	std::unordered_map<ChunkCoordinate, AsyncChunkPlacementResult> byChunk;
	for (int ty = 100; ty < 400; ty += 10) {
		for (int tx = 100; tx < 400; tx += 10) {
			const WorldPosition wp{static_cast<float>(tx), static_cast<float>(ty)};
			const ChunkCoordinate coord = engine::world::worldToChunk(wp);
			AsyncChunkPlacementResult& r = byChunk[coord];
			r.coord = coord;
			PlacedEntity e;
			e.defName  = "Test_DetTree";
			e.position = {wp.x, wp.y};
			r.spatialIndex.insert(e);
		}
	}
	for (auto& [coord, result] : byChunk) {
		placement.storeChunkResult(std::move(result));
	}

	ConstructionWorld cw;
	const Vec2i64	   center{256000, 256000};
	const std::int64_t radius = 200000;
	NavMeshInput a = buildInput(center, radius, *mgr, placement, reg, cw, ConstructionRegistry::Get());
	NavMeshInput b = buildInput(center, radius, *mgr, placement, reg, cw, ConstructionRegistry::Get());

	ASSERT_EQ(a.polygons.size(), b.polygons.size());
	for (std::size_t i = 0; i < a.polygons.size(); ++i) {
		EXPECT_EQ(a.polygons[i].blocked, b.polygons[i].blocked);
		EXPECT_EQ(a.polygons[i].provenanceId, b.polygons[i].provenanceId);
		ASSERT_EQ(a.polygons[i].ring.size(), b.polygons[i].ring.size()) << "ring " << i;
		for (std::size_t k = 0; k < a.polygons[i].ring.size(); ++k) {
			EXPECT_EQ(a.polygons[i].ring[k].x, b.polygons[i].ring[k].x);
			EXPECT_EQ(a.polygons[i].ring[k].y, b.polygons[i].ring[k].y);
		}
	}
}

// Walls entirely outside the area are dropped; a wall crossing into the area is kept.
TEST_F(NavWallTest, Area_DropsWallsOutsideArea) {
	auto mgr = readyChunks(std::make_unique<WaterRegionSampler>(std::vector<ChunkCoordinate>{}));
	PlacementExecutor placement(AssetRegistry::Get());

	ConstructionWorld cw;
	// One wall inside the area, one far outside it.
	buildWall(cw, {250000, 256000}, {262000, 256000}); // inside
	buildWall(cw, {800000, 800000}, {812000, 800000}); // far outside

	const Vec2i64	   center{256000, 256000};
	const std::int64_t radius = 20000;
	NavMeshInput	   input  = buildInput(center, radius, *mgr, placement, AssetRegistry::Get(), cw,
										   ConstructionRegistry::Get());

	const Vec2i64 minMm{center.x - radius, center.y - radius};
	const Vec2i64 maxMm{center.x + radius, center.y + radius};
	for (const NavInputPolygon& p : input.polygons) {
		if (p.provenanceId <= 0) {
			continue; // skip border/water/flora; walls carry positive segment ids
		}
		// Every kept wall ring must have at least one vertex within (or straddling) the
		// area on each axis -- i.e. it is not entirely on one outside side.
		bool allLeft = true, allRight = true, allBelow = true, allAbove = true;
		for (const Vec2i64& v : p.ring) {
			allLeft	 = allLeft && (v.x < minMm.x);
			allRight = allRight && (v.x > maxMm.x);
			allBelow = allBelow && (v.y < minMm.y);
			allAbove = allAbove && (v.y > maxMm.y);
		}
		EXPECT_FALSE(allLeft || allRight || allBelow || allAbove) << "an out-of-area wall ring was kept";
	}
}
