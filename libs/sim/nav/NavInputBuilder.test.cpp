#include "NavInputBuilder.h"

#include "NavCoords.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>
#include <nav/PathQuery.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <world/Biome.h>
#include <world/BiomeWeights.h>
#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/ChunkSampleResult.h>
#include <world/chunk/IWorldSampler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

using namespace nav;
using assets::AssetDefinition;
using assets::AssetRegistry;
using assets::CollisionShapeType;
using assets::ConstructionRegistry;
using assets::PlacedEntity;
using assets::SpatialIndex;
using construction::ConstructionWorld;
using construction::FoundationState;
using construction::kInvalidFoundation;
using construction::OpeningId;
using construction::SegmentCommitResult;
using construction::SegmentId;
using geometry::Ring;
using geometry::Vec2i64;
using geometry::nav::DoorPortal;
using geometry::nav::NavInputPolygon;
using geometry::nav::NavMesh;
using geometry::nav::NavMeshInput;

namespace {

	// Project root from __FILE__: this file lives at <root>/libs/sim/nav/.
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
// Ground: chunk squares and their water (terrain rings, D9)
// ---------------------------------------------------------------------------

namespace {

	using world::ChunkTerrainPolygons;
	using world::TerrainRing;
	using world::TerrainRingKind;

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

	// Hand-built chunk ring sets standing in for ready chunks.
	struct FakeTerrain {
		std::map<std::pair<std::int32_t, std::int32_t>, ChunkTerrainPolygons> chunks;

		ChunkTerrainPolygons& at(world::ChunkCoordinate coord) { return chunks[{coord.x, coord.y}]; }
	};

	// Each chunk's ground (its square and water) in one mesh, as NavigationSystem adds it.
	geometry::nav::PatchableMesh meshFor(const FakeTerrain& terrain) {
		geometry::nav::PatchableMesh mesh({.maxBoundsEdgeMm = 64000});
		geometry::nav::PatchEdits	 edits;
		for (const auto& [coord, polygons] : terrain.chunks) {
			edits.adds.push_back(chunkSquare({coord.first, coord.second}));
			appendChunkWater(polygons, edits.adds);
		}
		mesh.applyPatch(edits);
		mesh.rebuildForests();
		EXPECT_EQ(mesh.validate(), "");
		return mesh;
	}

	// Walkable = on a triangle that isn't common-knowledge terrain (water).
	bool walkableAt(const geometry::nav::PatchableMesh& mesh, Vec2i64 p) {
		const std::int32_t tri = mesh.locate(p);
		return tri >= 0 && geometry::nav::terrainTraversable(mesh.triangles()[static_cast<std::size_t>(tri)]);
	}

	geometry::nav::PathResult pathOn(const geometry::nav::PatchableMesh& mesh, Vec2i64 a, Vec2i64 b) {
		return geometry::nav::pathThrough(mesh.view(), a, b, 300);
	}

	// Every live triangle, as its corners and its tags.
	template <typename Visit> void forEachLive(const geometry::nav::PatchableMesh& mesh, Visit visit) {
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (mesh.triangleLive(t)) {
				const geometry::nav::NavTriangle& tri = mesh.triangles()[static_cast<std::size_t>(t)];
				visit(Ring{mesh.vertices()[tri.v[0]], mesh.vertices()[tri.v[1]], mesh.vertices()[tri.v[2]]}, tri);
			}
		}
	}

	Vec2i64 centroid(const Ring& tri) {
		return {(tri[0].x + tri[1].x + tri[2].x) / 3, (tri[0].y + tri[1].y + tri[2].y) / 3};
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

TEST(NavInputBuilder, ChunkSquare_IsTheChunksBoundsUnblocked) {
	const NavInputPolygon square = chunkSquare({1, -2});
	EXPECT_FALSE(square.blocked);
	EXPECT_EQ(square.provenanceId, kProvenanceBorder);
	EXPECT_TRUE(isCcw(square.ring));
	EXPECT_EQ(*std::min_element(square.ring.begin(), square.ring.end()), meters(512, -1024));
	EXPECT_EQ(*std::max_element(square.ring.begin(), square.ring.end()), meters(1024, -512));
}

TEST(NavInputBuilder, Water_EmitsBlockingRingsWithProvenanceAndHoleCapability) {
	ChunkTerrainPolygons c;
	c.navRings.push_back(terrainRing(box(100, 100, 200, 200), TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(box(140, 140, 160, 160, /*clockwise=*/true), TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(box(120, 80, 124, 100), TerrainRingKind::Channel));

	std::vector<NavInputPolygon> water;
	appendChunkWater(c, water);
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

	const geometry::nav::PatchableMesh mesh = meshFor(terrain);
	EXPECT_TRUE(walkableAt(mesh, meters(150, 150))) << "the island is land (even depth)";
	EXPECT_FALSE(walkableAt(mesh, meters(120, 150))) << "open lake water blocks";
	EXPECT_TRUE(walkableAt(mesh, meters(90, 150))) << "the shore outside the lake is land";
	EXPECT_FALSE(pathOn(mesh, meters(150, 150), meters(90, 150)).reachable) << "the island is cut off by water";
}

// The lake covers the whole chunk: only the island inside it is land.
TEST(NavInputBuilder, Water_LakeCoveringChunk_OnlyIslandWalkable) {
	FakeTerrain			  terrain;
	ChunkTerrainPolygons& c = terrain.at({0, 0});
	c.navRings.push_back(terrainRing(box(0, 0, 512, 512), TerrainRingKind::Waterline));
	c.navRings.push_back(terrainRing(box(240, 240, 270, 270, true), TerrainRingKind::Waterline));

	const geometry::nav::PatchableMesh mesh = meshFor(terrain);
	EXPECT_TRUE(walkableAt(mesh, meters(255, 255)));
	EXPECT_FALSE(walkableAt(mesh, meters(230, 255)));
	EXPECT_FALSE(walkableAt(mesh, meters(280, 280)));
	EXPECT_FALSE(walkableAt(mesh, meters(1, 1)));
}

// Two chunks' navRings meet along x = 512 m with bit-identical border corners, each side
// splitting the border edge at its own extra vertex, and each chunk's square splitting it every
// 16 m. In one mesh the coincident edges merge: no floor inside the lake (no gap), and the
// triangles tile both chunks exactly (no sliver, no overlap).
TEST(NavInputBuilder, Water_LakeAcrossChunkBorder_NoGapNoSliver) {
	FakeTerrain terrain;
	terrain.at({0, 0}).navRings.push_back(
		terrainRing({meters(470, 230), meters(512, 230), meters(512, 250), meters(512, 290), meters(470, 290)}, TerrainRingKind::Waterline));
	terrain.at({1, 0}).navRings.push_back(
		terrainRing({meters(512, 230), meters(550, 230), meters(550, 290), meters(512, 290), meters(512, 271)}, TerrainRingKind::Waterline));

	const geometry::nav::PatchableMesh mesh	   = meshFor(terrain);
	const Ring						   lake	   = box(470, 230, 550, 290);
	double							   areaMm2 = 0.0;
	forEachLive(mesh, [&](const Ring& tri, const geometry::nav::NavTriangle& t) {
		areaMm2 += signedArea2(tri) / 2.0;
		if (strictlyInside(centroid(tri), lake)) {
			EXPECT_FALSE(geometry::nav::isFloorFace(t)) << "floor inside the lake: a gap at the chunk border";
		}
	});
	EXPECT_NEAR(areaMm2 / 1e6, 2.0 * 512.0 * 512.0, 1e-3) << "triangles must tile both chunks exactly";
	for (double y : {231.0, 250.0, 260.0, 271.0, 289.0}) {
		EXPECT_FALSE(walkableAt(mesh, meters(511.999, y)));
		EXPECT_FALSE(walkableAt(mesh, meters(512.0, y)));
		EXPECT_FALSE(walkableAt(mesh, meters(512.001, y)));
	}
	EXPECT_TRUE(walkableAt(mesh, meters(512, 300)));
	EXPECT_TRUE(pathOn(mesh, meters(500, 310), meters(530, 310)).reachable) << "land crosses the border";
}

// Channel and Pond rings are solid and overlap each other (a confluence) and the Waterline rings
// (a mouth running into a lake, a channel crossing an island). An overlap must never flip parity
// into a walkable hole.
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

	const geometry::nav::PatchableMesh mesh		   = meshFor(terrain);
	int								   islandFloor = 0;
	forEachLive(mesh, [&](const Ring& tri, const geometry::nav::NavTriangle& t) {
		const Vec2i64 p		  = centroid(tri);
		const bool	  inSolid = strictlyInside(p, trunk) || strictlyInside(p, feeder) || strictlyInside(p, pond);
		const bool	  inLake  = strictlyInside(p, lake) && !strictlyInside(p, island);
		if (inSolid || inLake) {
			EXPECT_FALSE(geometry::nav::isFloorFace(t)) << "walkable hole at (" << p.x << ", " << p.y << ")";
		} else if (strictlyInside(p, island) && geometry::nav::isFloorFace(t)) {
			++islandFloor;
		}
	});
	EXPECT_GT(islandFloor, 0) << "the island beside the channel stays land";
	EXPECT_FALSE(walkableAt(mesh, meters(248, 183))) << "the confluence overlap blocks";
	EXPECT_FALSE(walkableAt(mesh, meters(250, 210))) << "the mouth overlap blocks";
	EXPECT_FALSE(walkableAt(mesh, meters(250, 250))) << "the channel across the island blocks";
	EXPECT_FALSE(walkableAt(mesh, meters(295, 295))) << "the pond over the lake corner blocks";
	EXPECT_TRUE(walkableAt(mesh, meters(240, 250)));
	EXPECT_TRUE(walkableAt(mesh, meters(230, 170)));
}

// A river narrows below kFordableWidthM: the wide piece blocks, the fordable piece is not
// emitted, and the two share a straight butt cut. A colonist wades straight across the creek.
TEST(NavInputBuilder, Water_FordableCreek_WalkableAcrossBlockingPieceBlocks) {
	FakeTerrain			  terrain;
	ChunkTerrainPolygons& c = terrain.at({0, 0});
	const Ring wide = {meters(100, 95), meters(150, 95), meters(150, 99.5), meters(150, 100.5), meters(150, 105), meters(100, 105)};
	const Ring creek = {meters(150, 99.5), meters(200, 99.5), meters(200, 100.5), meters(150, 100.5)};
	c.navRings.push_back(terrainRing(wide, TerrainRingKind::Channel));
	c.navRings.push_back(terrainRing(creek, TerrainRingKind::Channel, /*blocksMovement=*/false));

	std::vector<NavInputPolygon> water;
	appendChunkWater(c, water);
	ASSERT_EQ(water.size(), 1u) << "only the blocking piece reaches nav";

	const geometry::nav::PatchableMesh mesh = meshFor(terrain);
	EXPECT_FALSE(walkableAt(mesh, meters(140, 100))) << "the wide river blocks";
	EXPECT_FALSE(walkableAt(mesh, meters(149.9, 100)));
	EXPECT_TRUE(walkableAt(mesh, meters(150.1, 100))) << "just past the butt cut is walkable";
	EXPECT_TRUE(walkableAt(mesh, meters(170, 100))) << "the creek bed is walkable";

	const geometry::nav::PathResult across = pathOn(mesh, meters(175, 94), meters(175, 106));
	ASSERT_TRUE(across.reachable);
	EXPECT_LT(pathLengthM(across), 12.5) << "wading straight across, not around";

	const geometry::nav::PathResult around = pathOn(mesh, meters(140, 90), meters(140, 110));
	ASSERT_TRUE(around.reachable);
	EXPECT_GT(pathLengthM(around), 20.0) << "the blocking piece forces the detour to the ford";
}

// A neighbour that was never loaded has no ground: no triangle there, water or land.
TEST(NavInputBuilder, Water_MissingNeighbourHasNoGround) {
	FakeTerrain terrain;
	terrain.at({0, 0}).navRings.push_back(terrainRing(box(480, 200, 512, 300), TerrainRingKind::Waterline));

	const geometry::nav::PatchableMesh mesh = meshFor(terrain);
	EXPECT_FALSE(walkableAt(mesh, meters(500, 250)));
	EXPECT_LT(mesh.locate(meters(530, 250)), 0) << "chunk (1,0) was never meshed";
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

namespace {
	// Every blocking ring the index's entities give, in its stable order.
	std::vector<NavInputPolygon> placedObstaclesOf(const SpatialIndex& index, const AssetRegistry& registry) {
		std::vector<NavInputPolygon> out;
		for (const PlacedEntity& e : index.allEntities()) {
			if (std::optional<NavInputPolygon> ring = placedObstacleRing(e, registry)) {
				out.push_back(std::move(*ring));
			}
		}
		return out;
	}
} // namespace

TEST_F(NavFloraTest, RectEntity_EmitsPaddedQuad) {
	AssetRegistry& reg = AssetRegistry::Get();
	AssetDefinition tree;
	tree.defName = "Test_NavTree";
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.1F, 0.1F}});
	reg.registerTestDefinition(tree);

	SpatialIndex index(4.0F);
	PlacedEntity e;
	e.defName = "Test_NavTree";
	e.position = {10.0F, 20.0F};
	e.scale = 1.0F;
	index.insert(e);

	std::vector<NavInputPolygon> polys = placedObstaclesOf(index, reg);
	ASSERT_EQ(polys.size(), 1u);
	EXPECT_TRUE(polys[0].blocked);
	EXPECT_EQ(polys[0].provenanceId, kProvenancePlaced);
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
	tree.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.5F, 0.2F}}); // non-square so rotation is visible
	reg.registerTestDefinition(tree);

	SpatialIndex index(4.0F);
	PlacedEntity e;
	e.defName  = "Test_NavRotTree";
	e.position = {10.0F, 20.0F};
	e.scale	   = 1.0F;
	e.rotation = 3.14159265358979323846F / 4.0F; // 45 degrees
	index.insert(e);

	std::vector<NavInputPolygon> polys = placedObstaclesOf(index, reg);
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

	EXPECT_TRUE(placedObstaclesOf(index, reg).empty());
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
	ASSERT_NE(op, construction::kInvalidOpening);
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
	ASSERT_NE(op, construction::kInvalidOpening);
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
			EXPECT_NE(op, construction::kInvalidOpening);
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
// Real chunk generation across a seam
// ---------------------------------------------------------------------------

namespace {

	using world::Biome;
	using world::BiomeWeights;
	using world::ChunkCoordinate;
	using world::ChunkManager;
	using world::ChunkSampleResult;
	using world::IWorldSampler;
	using world::WorldPosition;

	// All tiles Lake (water) inside a chosen set of chunks, land (TemperateGrassland) elsewhere.
	class WaterRegionSampler : public IWorldSampler {
	  public:
		explicit WaterRegionSampler(std::vector<ChunkCoordinate> waterChunks)
			: m_waterChunks(std::move(waterChunks)) {}

		[[nodiscard]] ChunkSampleResult sampleChunk(ChunkCoordinate coord) const override {
			const bool water = std::find(m_waterChunks.begin(), m_waterChunks.end(), coord) != m_waterChunks.end();
			return world::makeUniformChunkSampleResult(BiomeWeights::single(water ? Biome::Lake : Biome::TemperateGrassland), 1.0F);
		}

		[[nodiscard]] float	   sampleElevation(WorldPosition) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return 99u; }

	  private:
		std::vector<ChunkCoordinate> m_waterChunks;
	};

} // namespace

// Real generation: chunks (0,0) and (1,0) are one lake, so each gives its navRings clipped to its
// own square, meeting along x = 512 m. Their ground in one mesh has no floor sliver at the seam.
TEST(NavInputBuilder, Water_GeneratedLakeAcrossChunkSeamHasNoGap) {
	auto mgr = std::make_unique<ChunkManager>(std::make_unique<WaterRegionSampler>(std::vector<ChunkCoordinate>{{0, 0}, {1, 0}}));
	mgr->setLoadRadius(1);
	mgr->setUnloadRadius(3);
	mgr->update({512.0F, 256.0F});
	mgr->finishPendingGeneration();

	FakeTerrain terrain;
	int			waterRings = 0;
	for (const ChunkCoordinate coord : {ChunkCoordinate{0, 0}, ChunkCoordinate{1, 0}}) {
		const world::Chunk* chunk = mgr->getChunk(coord);
		ASSERT_NE(chunk, nullptr);
		ASSERT_TRUE(chunk->isReady());
		terrain.at(coord) = chunk->terrainPolygons();
		std::vector<NavInputPolygon> water;
		appendChunkWater(chunk->terrainPolygons(), water);
		waterRings += static_cast<int>(water.size());
	}
	EXPECT_GE(waterRings, 2) << "each chunk contributes its own clipped rings";

	const geometry::nav::PatchableMesh mesh = meshFor(terrain);
	const Ring						   seam = box(448, 192, 576, 320);
	forEachLive(mesh, [&](const Ring& tri, const geometry::nav::NavTriangle& t) {
		if (strictlyInside(centroid(tri), seam)) {
			EXPECT_FALSE(geometry::nav::isFloorFace(t)) << "floor inside the lake at the chunk seam";
		}
	});
}
