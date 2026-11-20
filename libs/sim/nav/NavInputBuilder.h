#pragma once

// NavInputBuilder: turn live game data (a mover's mesh limit, a chunk's terrain water rings, a placed
// entity's collider, built walls and doors) into the tagged rings and door portals the
// geometry layer's nav meshes take (geometry::nav::NavInputPolygon, DoorPortal). These are
// PURE functions: they read game state and emit rings in integer millimeters, no ECS, no
// caching (NavigationSystem keeps the meshes and patches them). The meters<->mm boundary is
// crossed only via NavCoords.h.
//
// Provenance ids tag each obstacle's origin so a triangle edge can be attributed to its
// source: negative sentinels for the engine-synthesized obstacles (water/placed obstacle/border),
// positive +segmentId for wall bands and junctionTag for wall junctions.

#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygons.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>

#include "PlacedObstacle.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace nav {

	// Provenance sentinels. Walls use +segmentId (a positive int64), so all the
	// engine-synthesized obstacle classes take distinct negative ids. These are the
	// `provenanceId` written on each emitted NavInputPolygon.
	constexpr std::int64_t kProvenanceWater	   = -1;
	constexpr std::int64_t kProvenancePlaced	   = -2;
	constexpr std::int64_t kProvenanceBorder   = -3;
	// A fordable channel: water a mover wades through. Its ring is cover-only, so it blocks no
	// movement, and a face's cover says the ground there is a stream bed, which building refuses.
	constexpr std::int64_t kProvenanceFord	   = -5;

	// A wall junction's provenance: the construction vertex its walls meet at, offset above every
	// segment id. Its ring's blocker group lists those walls, so belief sees it with any of them.
	constexpr std::int64_t kJunctionTagBase = std::int64_t{1} << 62;
	[[nodiscard]] constexpr std::int64_t junctionTag(construction::VertexId vertex) {
		return kJunctionTagBase + static_cast<std::int64_t>(vertex);
	}

	// --- Ground ------------------------------------------------------------------
	//
	// Ground is the union of mesh limits; water hangs over it per chunk. Water is the
	// chunk's terrain polygon navRings, never tiles (terrain-polygons D1, D9), already clipped
	// to the chunk's 512 m extent, so two neighbours' water meets along bit-identical border
	// edges and never overlaps.

	// A mover's mesh limit: geometry::nav::limitPolygon round `centreMm` holding the disc of
	// `rangeMm`, blocked=false, provenance=kProvenanceBorder. The same centre and range give the
	// same ring on every machine.
	[[nodiscard]] geometry::nav::NavInputPolygon meshLimit(const geometry::Vec2i64& centreMm, std::int64_t rangeMm);

	// Appends every navRing in stored order, orientation kept (a Waterline island stays a CW
	// hole): one with blocksMovement as a blocked ring with kProvenanceWater and the ring's
	// holeCapable, a fordable channel piece (blocksMovement false) as a cover-only ring with
	// kProvenanceFord.
	void appendChunkWater(const world::ChunkTerrainPolygons& polygons, std::vector<geometry::nav::NavInputPolygon>& out);

	// Whether triangle `tri` lies in a fordable channel: ground a mover wades across and nothing
	// is built on. Read from the triangle's cover, so a placed obstacle or a wall over it changes
	// nothing.
	[[nodiscard]] bool faceInFord(const geometry::nav::PatchableMesh& mesh, std::int32_t tri);

	// --- Placed obstacles --------------------------------------------------------

	// One blocked ring for a single placed entity whose AssetDefinition has a
	// blocking collision shape, transformed from its def's pivot frame into world mm by
	// the entity's position (tiles), rotation, and scale, as its mesh is drawn. Rect -> a
	// padded oriented quad; Polygon -> its points transformed. Returns nullopt when the entity has no blocking collision
	// (most placed entities) or the resulting ring is degenerate.
	[[nodiscard]] std::optional<geometry::nav::NavInputPolygon>
	placedObstacleRing(const assets::PlacedEntity& entity, const assets::AssetRegistry& registry);

	// The outline placedObstacleRing puts in the mesh, written into `out` (cleared first, its storage
	// reused): false when there is none. What a clear-ground test reads, so the two never differ.
	[[nodiscard]] bool placedObstacleOutline(const assets::PlacedEntity& entity, const assets::AssetRegistry& registry, geometry::Ring& out);

	// The same from the collider already looked up for the entity's age class.
	[[nodiscard]] bool placedObstacleOutline(const assets::PlacedEntity& entity, const assets::CollisionShape& collision, geometry::Ring& out);

	// How far the outline of `collision` can reach from the entity's position at scale 1: the shape's
	// reach plus the pad's corner, the rule placedObstacleReachMeters takes over every def. What a
	// caller holding positions tests before building the outline.
	[[nodiscard]] float placedObstacleLocalReachMeters(const assets::CollisionShape& collision);

	// --- Walls / doors -----------------------------------------------------------

	// Over the BUILT wall segments: resolve band offsetting + junction trimming per
	// connected wall graph (so corners tile), then append each band in segment order and
	// then each junction ring to outPolys (band provenance = +segmentId; a junction's is
	// junctionTag of the vertex its walls meet at, with those walls as its blocker group, so a
	// belief knowing any of them sees it). A graph the offsetter rejects gives
	// its segments' raw bands and no junctions, and leaves every other graph alone.
	// For each BUILT pathable opening (a door) on an accepted graph, the hosting segment's
	// trimmed band comes as geometry::wallSpans cuts it at its doors' gaps
	// (construction::openingGaps): door spans tagged with their opening, solid spans with
	// none. Each door also emits a DoorPortal with its two jamb points and the opening's
	// clear width. A non-pathable opening (a window) leaves its band solid and emits a
	// DoorPortal with clearWidthMm == 0 so a later vision pass can still find it.
	// Blueprints (state != Built) contribute nothing.
	//
	// `outJunctions`, when given, gets each junction as its ring's index in outPolys and every wall
	// that meets there.
	struct WallJunction {
		std::size_t							 polygon = 0;
		std::vector<construction::SegmentId> walls;
	};
	void extractWalls(const construction::ConstructionWorld& world, const assets::ConstructionRegistry& registry,
					  std::vector<geometry::nav::NavInputPolygon>& outPolys, std::vector<geometry::nav::DoorPortal>& outDoors,
					  std::vector<WallJunction>* outJunctions = nullptr);

	// --- Border ------------------------------------------------------------------

	// The walkable bounds: a CCW rectangle ring [minMm, maxMm], blocked=false,
	// provenance=kProvenanceBorder.
	[[nodiscard]] geometry::nav::NavInputPolygon borderRing(geometry::Vec2i64 minMm, geometry::Vec2i64 maxMm);

} // namespace nav
