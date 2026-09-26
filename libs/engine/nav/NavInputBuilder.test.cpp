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
#include <predicates/Predicates.h>

#include <world/Biome.h>
#include <world/BiomeWeights.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/ChunkSampleResult.h>
#include <world/chunk/IWorldSampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
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
// Water (terrain rings, D9)
// ---------------------------------------------------------------------------

namespace {

	using engine::world::ChunkTerrainPolygons;
	using engine::world::TerrainRing;
	using engine::world::TerrainRingKind;

	constexpr std::int64_t kM = 1000; // mm per meter

	Vec2i64 meters(double x, double y) {
		return {std::llround(x * 1000.0), std::llround(y * 1000.0)};
	}

	// Axis-aligned ring in meters, CCW (an outer boundary) or CW (a Waterline island).
	Ring box(double x0, double y0, double x1, double y1, bool clockwise = false) {
		Ring r = {meters(x0, y0), meters(x1, y0), meters(x1, y1), meters(x0, y1)};
		if (clockwise) {
			std::reverse(r.begin(), r.end());
		}
		return r;
	}

	TerrainRing terrainRing(Ring ring, TerrainRingKind kind, bool blocksMovement = true) {
		TerrainRing t;
		t.ring			 = std::move(ring);
		t.kind			 = kind;
		t.blocksMovement = blocksMovement;
		t.holeCapable	 = kind == TerrainRingKind::Waterline;
		return t;
	}

	// Hand-built chunk ring sets standing in for ready chunks; a coordinate with no
	// entry reads as a missing / not-ready chunk.
	struct FakeTerrain {
		std::unordered_map<engine::world::ChunkCoordinate, ChunkTerrainPolygons> chunks;

		ChunkTerrainPolygons& at(engine::world::ChunkCoordinate coord) {
			ChunkTerrainPolygons& p = chunks[coord];
			p.version				= std::max<std::uint32_t>(p.version, 1);
			return p;
		}

		[[nodiscard]] TerrainPolygonsLookup lookup() const {
			return [this](engine::world::ChunkCoordinate coord) -> const ChunkTerrainPolygons* {
				auto it = chunks.find(coord);
				return it == chunks.end() ? nullptr : &it->second;
			};
		}
	};

	std::vector<NavInputPolygon> waterFor(Vec2i64 centerMm, std::int64_t radiusMm, const FakeTerrain& terrain) {
		std::vector<NavInputPolygon> out;
		appendWaterObstacles(centerMm, radiusMm, terrain.lookup(), out);
		return out;
	}

	NavMesh meshFor(Vec2i64 centerMm, std::int64_t radiusMm, const FakeTerrain& terrain) {
		NavMeshInput input;
		input.polygons.push_back(
			borderRing({centerMm.x - radiusMm, centerMm.y - radiusMm}, {centerMm.x + radiusMm, centerMm.y + radiusMm}));
		appendWaterObstacles(centerMm, radiusMm, terrain.lookup(), input.polygons);
		return geometry::nav::buildNavMesh(input);
	}

	// Walkable = on a triangle that isn't common-knowledge terrain (water).
	bool walkableAt(const NavMesh& mesh, Vec2i64 p) {
		const std::int32_t tri = geometry::nav::locateTriangle(mesh, p);
		return tri >= 0 && geometry::nav::terrainTraversable(mesh.triangles[static_cast<std::size_t>(tri)]);
	}

	Vec2i64 triCentroid(const NavMesh& m, const geometry::nav::NavTriangle& t) {
		return {(m.vertices[t.v[0]].x + m.vertices[t.v[1]].x + m.vertices[t.v[2]].x) / 3,
				(m.vertices[t.v[0]].y + m.vertices[t.v[1]].y + m.vertices[t.v[2]].y) / 3};
	}

	bool strictlyInside(Vec2i64 p, const Ring& r) {
		return geometry::pointInPolygon(p, r) == geometry::PointInPolygon::Inside;
	}

	double pathLengthM(const geometry::nav::PathResult& path) {
		double len = 0.0;
		for (std::size_t i = 1; i < path.points.size(); ++i) {
			const double dx = static_cast<double>(path.points[i].x - path.points[i - 1].x);
			const double dy = static_cast<double>(path.points[i].y - path.points[i - 1].y);
			len += std::sqrt(dx * dx + dy * dy);
		}
		return len / 1000.0;
	}

} // namespace

TEST(NavInputBuilder, Water_EmitsBlockingRingsWithProvenanceAndHoleCapability) {
	FakeTerrain			  terrain;
	ChunkTerrainPolygons& c = terrain.at({0, 0});
	c.navRings.push_back(terrainRing(box(100, 100, 200, 200), TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(box(140, 140, 160, 160, /*clockwise=*/true), TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(box(120, 80, 124, 100), TerrainRingKind::Channel));

	const std::vector<NavInputPolygon> water = waterFor(meters(150, 150), 80 * kM, terrain);
	ASSERT_EQ(water.size(), 3u);
	for (const NavInputPolygon& p : water) {
		EXPECT_TRUE(p.blocked);
		EXPECT_EQ(p.provenanceId, kProvenanceWater);
		EXPECT_EQ(p.openingId, geometry::nav::kNoOpening);
	}
	EXPECT_TRUE(water[0].holeCapable);
	EXPECT_TRUE(isCcw(water[0].ring));
	EXPECT_TRUE(water[1].holeCapable);
	EXPECT_TRUE(isCw(water[1].ring)) << "a Waterline island stays a CW hole";
	EXPECT_FALSE(water[2].holeCapable) << "a Channel ring is solid";
}

TEST(NavInputBuilder, Water_LakeWithIsland_IslandWalkableLakeBlocked) {
	FakeTerrain			  terrain;
	ChunkTerrainPolygons& c = terrain.at({0, 0});
	c.navRings.push_back(terrainRing(box(100, 100, 200, 200), TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(box(140, 140, 160, 160, true), TerrainRingKind::Waterline));

	const NavMesh mesh = meshFor(meters(150, 150), 70 * kM, terrain);
	ASSERT_FALSE(mesh.triangles.empty());
	EXPECT_TRUE(walkableAt(mesh, meters(150, 150))) << "the island is land (even depth)";
	EXPECT_FALSE(walkableAt(mesh, meters(120, 150))) << "open lake water blocks";
	EXPECT_TRUE(walkableAt(mesh, meters(90, 150))) << "the shore outside the lake is land";
	EXPECT_FALSE(geometry::nav::pathThrough(mesh, meters(150, 150), meters(90, 150), 300).reachable)
		<< "the island is cut off by water";
}

// The lake covers the whole area (water exiting every side): the ring is clipped to
// the area rect, and only the island inside it is land.
TEST(NavInputBuilder, Water_LakeCoveringArea_OnlyIslandWalkable) {
	FakeTerrain			  terrain;
	ChunkTerrainPolygons& c = terrain.at({0, 0});
	c.navRings.push_back(terrainRing(box(0, 0, 512, 512), TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(box(240, 240, 270, 270, true), TerrainRingKind::Waterline));

	const Vec2i64	   center = meters(256, 256);
	const std::int64_t radius = 40 * kM;
	for (const NavInputPolygon& p : waterFor(center, radius, terrain)) {
		for (const Vec2i64& v : p.ring) {
			EXPECT_GE(v.x, center.x - radius);
			EXPECT_LE(v.x, center.x + radius);
			EXPECT_GE(v.y, center.y - radius);
			EXPECT_LE(v.y, center.y + radius);
		}
	}
	const NavMesh mesh = meshFor(center, radius, terrain);
	EXPECT_TRUE(walkableAt(mesh, meters(255, 255)));
	EXPECT_FALSE(walkableAt(mesh, meters(230, 255)));
	EXPECT_FALSE(walkableAt(mesh, meters(280, 280)));
}

// Two chunks' clipped navRings meet along x = 512 m with bit-identical border
// corners, and each side splits the border edge at its own extra vertex. The
// arrangement must merge the coincident constraint edges: no floor inside the lake
// (no gap), and the triangles tile the area exactly (no sliver, no overlap).
TEST(NavInputBuilder, Water_LakeAcrossChunkBorder_NoGapNoSliver) {
	FakeTerrain terrain;
	terrain.at({0, 0}).navRings.push_back(terrainRing(
		{meters(470, 230), meters(512, 230), meters(512, 250), meters(512, 290), meters(470, 290)}, TerrainRingKind::Waterline));
	terrain.at({1, 0}).navRings.push_back(terrainRing(
		{meters(512, 230), meters(550, 230), meters(550, 290), meters(512, 290), meters(512, 271)}, TerrainRingKind::Waterline));

	const Vec2i64	   center = meters(512, 260);
	const std::int64_t radius = 64 * kM;
	const NavMesh	   mesh	  = meshFor(center, radius, terrain);
	ASSERT_FALSE(mesh.triangles.empty());

	const Ring lake	   = box(470, 230, 550, 290);
	double	   areaMm2 = 0.0;
	for (const geometry::nav::NavTriangle& t : mesh.triangles) {
		const Ring tri = {mesh.vertices[t.v[0]], mesh.vertices[t.v[1]], mesh.vertices[t.v[2]]};
		areaMm2 += signedArea2(tri) / 2.0;
		if (strictlyInside(triCentroid(mesh, t), lake)) {
			EXPECT_FALSE(geometry::nav::isFloorFace(t)) << "floor inside the lake: a gap at the chunk border";
		}
	}
	EXPECT_NEAR(areaMm2 / 1e6, 128.0 * 128.0, 1e-6) << "triangles must tile the area exactly";
	for (double y : {231.0, 250.0, 260.0, 271.0, 289.0}) {
		EXPECT_FALSE(walkableAt(mesh, meters(511.999, y)));
		EXPECT_FALSE(walkableAt(mesh, meters(512.001, y)));
	}
	EXPECT_TRUE(walkableAt(mesh, meters(512, 300)));
}

// Channel and Pond rings are solid and overlap each other (a confluence) and the
// Waterline rings (a mouth running into a lake, a channel crossing an island). An
// overlap must never flip parity into a walkable hole.
TEST(NavInputBuilder, Water_ConfluenceAndMouthOverlaps_NeverWalkable) {
	FakeTerrain			  terrain;
	ChunkTerrainPolygons& c		 = terrain.at({0, 0});
	const Ring			  lake	 = box(200, 200, 300, 300);
	const Ring			  island = box(230, 230, 270, 270);
	const Ring			  trunk	 = box(246, 170, 254, 290); // enters the lake from the south, crosses the island
	const Ring			  feeder = box(220, 180, 250, 186); // joins the trunk south of the lake: a confluence
	const Ring			  pond	 = box(290, 290, 310, 310); // overlaps the lake's corner
	c.navRings.push_back(terrainRing(lake, TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(box(230, 230, 270, 270, true), TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(trunk, TerrainRingKind::Channel));
	c.navRings.push_back(terrainRing(feeder, TerrainRingKind::Channel));
	c.navRings.push_back(terrainRing(pond, TerrainRingKind::Pond));

	const NavMesh mesh = meshFor(meters(250, 240), 80 * kM, terrain);
	ASSERT_FALSE(mesh.triangles.empty());
	int islandFloor = 0;
	for (const geometry::nav::NavTriangle& t : mesh.triangles) {
		const Vec2i64 p		  = triCentroid(mesh, t);
		const bool	  inSolid = strictlyInside(p, trunk) || strictlyInside(p, feeder) || strictlyInside(p, pond);
		const bool	  inLake  = strictlyInside(p, lake) && !strictlyInside(p, island);
		if (inSolid || inLake) {
			EXPECT_FALSE(geometry::nav::isFloorFace(t)) << "walkable hole at (" << p.x << ", " << p.y << ")";
		} else if (strictlyInside(p, island) && geometry::nav::isFloorFace(t)) {
			++islandFloor;
		}
	}
	EXPECT_GT(islandFloor, 0) << "the island beside the channel stays land";
	EXPECT_FALSE(walkableAt(mesh, meters(248, 183))) << "the confluence overlap blocks";
	EXPECT_FALSE(walkableAt(mesh, meters(250, 210))) << "the mouth overlap blocks";
	EXPECT_FALSE(walkableAt(mesh, meters(250, 250))) << "the channel across the island blocks";
	EXPECT_FALSE(walkableAt(mesh, meters(295, 295))) << "the pond over the lake corner blocks";
	EXPECT_TRUE(walkableAt(mesh, meters(240, 250)));
	EXPECT_TRUE(walkableAt(mesh, meters(230, 170)));
}

// A river narrows below kFordableWidthM: the wide piece blocks, the fordable piece
// is not emitted, and the two share a straight butt cut. The walkable side of the
// cut is reachable, and a colonist wades straight across the creek.
TEST(NavInputBuilder, Water_FordableCreek_WalkableAcrossBlockingPieceBlocks) {
	FakeTerrain			  terrain;
	ChunkTerrainPolygons& c = terrain.at({0, 0});
	const Ring wide = {meters(100, 95), meters(150, 95), meters(150, 99.5), meters(150, 100.5), meters(150, 105), meters(100, 105)};
	const Ring creek = {meters(150, 99.5), meters(200, 99.5), meters(200, 100.5), meters(150, 100.5)};
	c.navRings.push_back(terrainRing(wide, TerrainRingKind::Channel));
	c.navRings.push_back(terrainRing(creek, TerrainRingKind::Channel, /*blocksMovement=*/false));

	const Vec2i64					   center = meters(160, 100);
	const std::int64_t				   radius = 40 * kM;
	const std::vector<NavInputPolygon> water  = waterFor(center, radius, terrain);
	ASSERT_EQ(water.size(), 1u) << "only the blocking piece reaches nav";

	const NavMesh mesh = meshFor(center, radius, terrain);
	EXPECT_FALSE(walkableAt(mesh, meters(140, 100))) << "the wide river blocks";
	EXPECT_FALSE(walkableAt(mesh, meters(149.9, 100)));
	EXPECT_TRUE(walkableAt(mesh, meters(150.1, 100))) << "just past the butt cut is walkable";
	EXPECT_TRUE(walkableAt(mesh, meters(170, 100))) << "the creek bed is walkable";

	const geometry::nav::PathResult across = geometry::nav::pathThrough(mesh, meters(175, 94), meters(175, 106), 300);
	ASSERT_TRUE(across.reachable);
	EXPECT_LT(pathLengthM(across), 12.5) << "wading straight across, not around";

	// Across the wide piece the only way over is around its east end, through the creek.
	const geometry::nav::PathResult around = geometry::nav::pathThrough(mesh, meters(140, 90), meters(140, 110), 300);
	ASSERT_TRUE(around.reachable);
	EXPECT_GT(pathLengthM(around), 20.0) << "the blocking piece forces the detour to the ford";
}

// A neighbor that is missing or still generating contributes nothing: its side of
// the area reads as land, even where its (future) rings would put water.
TEST(NavInputBuilder, Water_MissingNeighborReadsAsLand) {
	FakeTerrain terrain;
	terrain.at({0, 0}).navRings.push_back(terrainRing(box(480, 200, 512, 300), TerrainRingKind::Waterline));

	const NavMesh mesh = meshFor(meters(512, 250), 64 * kM, terrain);
	EXPECT_FALSE(walkableAt(mesh, meters(500, 250)));
	EXPECT_TRUE(walkableAt(mesh, meters(530, 250))) << "chunk (1,0) is not ready: land";
}

TEST(NavInputBuilder, WaterSignature_FoldsEveryChunkVersion) {
	// The area straddles x = 512 m: its range is chunks (0,0) and (1,0).
	FakeTerrain terrain;
	terrain.at({0, 0});
	const Vec2i64		center	  = meters(512, 256);
	const std::int64_t	radius	  = 64 * kM;
	const std::uint64_t oneReady  = waterSignature(center, radius, terrain.lookup());
	EXPECT_EQ(waterSignature(center, radius, terrain.lookup()), oneReady);

	// The neighbor becoming ready registers.
	terrain.at({1, 0});
	const std::uint64_t base = waterSignature(center, radius, terrain.lookup());
	EXPECT_NE(base, oneReady);

	terrain.chunks[{0, 0}].version = 2;
	const std::uint64_t oneBumped  = waterSignature(center, radius, terrain.lookup());
	EXPECT_NE(oneBumped, base);

	// With (0,0) already at 2, (1,0) moving 1 -> 2 must still register (a max would not).
	terrain.chunks[{1, 0}].version = 2;
	const std::uint64_t bothBumped = waterSignature(center, radius, terrain.lookup());
	EXPECT_NE(bothBumped, oneBumped);

	// A chunk outside the range doesn't.
	terrain.at({0, -1}).version = 7;
	EXPECT_EQ(waterSignature(center, radius, terrain.lookup()), bothBumped);
}

TEST(NavInputBuilder, AreaChunkRange_CoversAreaPlusOneTile) {
	// [511, 513] m plus the 1 m margin reaches chunks 0 and 1 on x; y stays in chunk 0.
	AreaChunkRange r = areaChunkRange(meters(512, 256), 1 * kM);
	EXPECT_EQ(r.min.x, 0);
	EXPECT_EQ(r.max.x, 1);
	EXPECT_EQ(r.min.y, 0);
	EXPECT_EQ(r.max.y, 0);
	// Negative coordinates floor, not truncate.
	r = areaChunkRange(meters(-10, -600), 5 * kM);
	EXPECT_EQ(r.min.x, -1);
	EXPECT_EQ(r.max.x, -1);
	EXPECT_EQ(r.min.y, -2);
	EXPECT_EQ(r.max.y, -2);
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

TEST_F(NavFloraTest, RectEntity_EmitsPaddedQuad) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName = "Test_NavTree";
	tree.collision.type			   = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.1F, 0.1F};
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
	EXPECT_EQ(polys[0].ring.size(), 4u);
	EXPECT_TRUE(isCcw(polys[0].ring));

	// Centroid at the entity position (10,20) m -> (10000, 20000) mm.
	std::int64_t cx = 0;
	std::int64_t cy = 0;
	for (const Vec2i64& v : polys[0].ring) {
		cx += v.x;
		cy += v.y;
	}
	cx /= static_cast<std::int64_t>(polys[0].ring.size());
	cy /= static_cast<std::int64_t>(polys[0].ring.size());
	EXPECT_NEAR(static_cast<double>(cx), 10000.0, 1.0);
	EXPECT_NEAR(static_cast<double>(cy), 20000.0, 1.0);

	// Axis-aligned square: half-extent 0.1 m + 50 mm pad = 150 mm each axis.
	for (const Vec2i64& v : polys[0].ring) {
		EXPECT_EQ(std::llabs(v.x - cx), 150);
		EXPECT_EQ(std::llabs(v.y - cy), 150);
	}
}

// A rotated entity turns the rect into an oriented quad (OBB): the 4 corners are
// the rotated rect, not an axis-aligned box.
TEST_F(NavFloraTest, RectEntity_RotationProducesOrientedQuad) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName = "Test_NavRotTree";
	tree.collision.type			   = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.5F, 0.2F}; // non-square so rotation is visible
	reg.registerTestDefinition(tree);

	SpatialIndex index(4.0F);
	PlacedEntity e;
	e.defName  = "Test_NavRotTree";
	e.position = {10.0F, 20.0F};
	e.scale	   = 1.0F;
	e.rotation = 3.14159265358979323846F / 4.0F; // 45 degrees
	index.insert(e);

	std::vector<NavInputPolygon> polys = extractFloraObstacles(index, reg);
	ASSERT_EQ(polys.size(), 1u);
	ASSERT_EQ(polys[0].ring.size(), 4u);
	EXPECT_TRUE(isCcw(polys[0].ring));

	std::int64_t cx = 0;
	std::int64_t cy = 0;
	for (const Vec2i64& v : polys[0].ring) {
		cx += v.x;
		cy += v.y;
	}
	cx /= 4;
	cy /= 4;

	// Padded half-extents: hx = 0.55 m, hy = 0.25 m -> 550 mm, 250 mm. Under a 45
	// degree rotation the local axis-aligned corner offsets map to the rotated
	// frame; check each emitted corner matches the analytically rotated corner.
	const double				 padHx = 550.0;
	const double				 padHy = 250.0;
	const double				 c45   = std::cos(static_cast<double>(e.rotation));
	const double				 s45   = std::sin(static_cast<double>(e.rotation));
	const std::array<std::pair<double, double>, 4> local = {
		std::make_pair(-padHx, -padHy), {padHx, -padHy}, {padHx, padHy}, {-padHx, padHy}};

	// Not axis-aligned: a rotated non-square rect has corners off both axes.
	bool anyOffAxis = false;
	for (const Vec2i64& v : polys[0].ring) {
		if (std::llabs(v.x - cx) != 0 && std::llabs(v.y - cy) != 0) {
			anyOffAxis = true;
		}
	}
	EXPECT_TRUE(anyOffAxis) << "rotated rect should not be axis-aligned";

	// Each emitted corner equals some rotated local corner (order preserved).
	for (std::size_t i = 0; i < 4; ++i) {
		const double rx = local[i].first * c45 - local[i].second * s45;
		const double ry = local[i].first * s45 + local[i].second * c45;
		EXPECT_NEAR(static_cast<double>(polys[0].ring[i].x - cx), rx, 2.0);
		EXPECT_NEAR(static_cast<double>(polys[0].ring[i].y - cy), ry, 2.0);
	}
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
			return engine::world::makeUniformChunkSampleResult(BiomeWeights::single(b), 1.0F);
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

// includeFlora=false omits tree/rock entity obstacles (the terrain-only placement mesh): the same
// area that emits flora rings by default emits none when flora is excluded, while the walkable border
// ring is still produced. This is the seam NavigationSystem::isAreaBuildable builds on so a footprint
// over clearable entities reads as buildable while geography and walls still block.
TEST_F(NavFloraTest, Area_ExcludesFloraWhenNotRequested) {
	AssetRegistry&	reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName					 = "Test_ExcludeTree";
	tree.collision.type				 = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.2F, 0.2F};
	reg.registerTestDefinition(tree);

	auto			  mgr = readyChunks(std::make_unique<WaterRegionSampler>(std::vector<ChunkCoordinate>{}));
	PlacementExecutor placement(reg);

	AsyncChunkPlacementResult result;
	result.coord = engine::world::worldToChunk({256.0F, 256.0F});
	for (int i = 0; i < 4; ++i) {
		PlacedEntity e;
		e.defName  = "Test_ExcludeTree";
		e.position = {256.0F + static_cast<float>(i), 256.0F};
		result.spatialIndex.insert(e);
	}
	placement.storeChunkResult(std::move(result));

	ConstructionWorld  cw;
	const Vec2i64	   center{256000, 256000};
	const std::int64_t radius = 20000;

	const NavMeshInput withFlora   = buildInput(center, radius, *mgr, placement, reg, cw, ConstructionRegistry::Get(), true);
	const NavMeshInput terrainOnly = buildInput(center, radius, *mgr, placement, reg, cw, ConstructionRegistry::Get(), false);

	EXPECT_GT(countFlora(withFlora), 0) << "default build emits the in-area trees";
	EXPECT_EQ(countFlora(terrainOnly), 0) << "includeFlora=false emits no tree rings";
	EXPECT_NE(findBorder(terrainOnly), nullptr) << "the walkable border is still produced";
}

// The area scopes flora: trees are placed across several chunks, most far outside
// the area; the build emits rings ONLY for the in-area trees, bounded (not all).
TEST_F(NavFloraTest, Area_ScopesFloraToInAreaTrees) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName			   = "Test_AreaTree";
	tree.collision.type			   = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.2F, 0.2F};
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

// Real generation: chunks (0,0) and (1,0) are one lake, so each gives its navRings
// clipped to its own square, meeting along x = 512 m. The area straddles that seam
// deep inside the lake: every triangle is water, with no floor sliver at the border.
// An area over chunks that were never loaded gives no water at all.
TEST_F(NavFloraTest, Area_LakeAcrossChunkSeamHasNoGap) {
	std::vector<ChunkCoordinate> water = {{0, 0}, {1, 0}};
	auto						 mgr   = readyChunks(std::make_unique<WaterRegionSampler>(water));
	PlacementExecutor			 placement(AssetRegistry::Get());
	ConstructionWorld			 cw;

	const Vec2i64	   center{512000, 256000};
	const std::int64_t radius = 64000;
	NavMeshInput	   input  = buildInput(center, radius, *mgr, placement, AssetRegistry::Get(), cw, ConstructionRegistry::Get());

	int waterRings = 0;
	for (const NavInputPolygon& p : input.polygons) {
		if (p.provenanceId == kProvenanceWater) {
			++waterRings;
		}
	}
	EXPECT_GE(waterRings, 2) << "each chunk contributes its own clipped rings";

	const NavMesh mesh = geometry::nav::buildNavMesh(input);
	ASSERT_FALSE(mesh.triangles.empty());
	for (const geometry::nav::NavTriangle& t : mesh.triangles) {
		EXPECT_FALSE(geometry::nav::isFloorFace(t)) << "floor inside the lake at the chunk seam";
	}

	std::vector<NavInputPolygon> far;
	appendWaterObstacles({5 * 512000 + 256000, 256000}, radius, readyTerrainPolygons(*mgr), far);
	EXPECT_TRUE(far.empty()) << "an unloaded chunk reads as land";
}

// Two builds of an unchanged world produce byte-identical inputs (deterministic
// ordering: border, water, flora (chunks (x,y) sorted, entities (pos,defName)
// sorted), walls).
TEST_F(NavFloraTest, Area_DeterministicOrdering) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName			   = "Test_DetTree";
	tree.collision.type			   = CollisionShapeType::Rect;
	tree.collision.halfExtentsMeters = {0.2F, 0.2F};
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
