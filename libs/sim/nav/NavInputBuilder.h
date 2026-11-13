#pragma once

// NavInputBuilder: turn live game data (a mover's visibility limit, a chunk's terrain water rings, a placed
// entity's collider, built walls and doors) into the tagged rings and door portals the
// geometry layer's nav meshes take (geometry::nav::NavInputPolygon, DoorPortal). These are
// PURE functions: they read game state and emit rings in integer millimeters, no ECS, no
// caching (NavigationSystem keeps the meshes and patches them). The meters<->mm boundary is
// crossed only via NavCoords.h.
//
// Provenance ids tag each obstacle's origin so a triangle edge can be attributed to its
// source: negative sentinels for the engine-synthesized obstacles (water/placed obstacle/border/junction),
// positive +segmentId for wall bands.

#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygons.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <nav/NavMesh.h>

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
	constexpr std::int64_t kProvenanceJunction = -4;

	// --- Ground ------------------------------------------------------------------
	//
	// Ground is the union of visibility limits; water hangs over it per chunk. Water is the
	// chunk's terrain polygon navRings, never tiles (terrain-polygons D1, D9), already clipped
	// to the chunk's 512 m extent, so two neighbours' water meets along bit-identical border
	// edges and never overlaps.

	// A mover's visibility limit: geometry::nav::limitPolygon round `centreMm` holding the disc of
	// `rangeMm`, blocked=false, provenance=kProvenanceBorder. The same centre and range give the
	// same ring on every machine.
	[[nodiscard]] geometry::nav::NavInputPolygon visibilityLimit(const geometry::Vec2i64& centreMm, std::int64_t rangeMm);

	// Appends every navRing with blocksMovement as a blocked ring with kProvenanceWater and
	// the ring's holeCapable, orientation kept (a Waterline island stays a CW hole), in
	// stored order. Fordable channel pieces (blocksMovement false) are skipped.
	void appendChunkWater(const world::ChunkTerrainPolygons& polygons, std::vector<geometry::nav::NavInputPolygon>& out);

	// --- Placed obstacles --------------------------------------------------------

	// One blocked ring for a single placed entity whose AssetDefinition has a
	// blocking collision shape, transformed into world mm by the entity's position
	// (tiles), rotation, and scale. Rect -> a padded oriented quad; Polygon -> its
	// points transformed. Returns nullopt when the entity has no blocking collision
	// (most placed entities) or the resulting ring is degenerate.
	[[nodiscard]] std::optional<geometry::nav::NavInputPolygon>
	placedObstacleRing(const assets::PlacedEntity& entity, const assets::AssetRegistry& registry);

	// The outline placedObstacleRing puts in the mesh, written into `out` (cleared first, its storage
	// reused): false when there is none. What a clear-ground test reads, so the two never differ.
	[[nodiscard]] bool placedObstacleOutline(const assets::PlacedEntity& entity, const assets::AssetRegistry& registry, geometry::Ring& out);

	// --- Walls / doors -----------------------------------------------------------

	// Over the BUILT wall segments: resolve band offsetting + junction trimming once
	// (so corners tile), then append each band and junction ring to outPolys
	// (band provenance = +segmentId, junction provenance = kProvenanceJunction).
	// For each BUILT pathable opening (a door) the hosting segment's solid band is
	// replaced by the two flanking rings around the door gap, and a DoorPortal is
	// emitted with the gap's two jamb points and the opening's clear width. A
	// non-pathable opening (a window) leaves its band solid and emits a DoorPortal
	// with clearWidthMm == 0 so a later vision pass can still find it. Blueprints
	// (state != Built) contribute nothing.
	void extractWalls(const construction::ConstructionWorld& world, const assets::ConstructionRegistry& registry,
					  std::vector<geometry::nav::NavInputPolygon>& outPolys, std::vector<geometry::nav::DoorPortal>& outDoors);

	// --- Border ------------------------------------------------------------------

	// The walkable bounds: a CCW rectangle ring [minMm, maxMm], blocked=false,
	// provenance=kProvenanceBorder.
	[[nodiscard]] geometry::nav::NavInputPolygon borderRing(geometry::Vec2i64 minMm, geometry::Vec2i64 maxMm);

} // namespace nav
