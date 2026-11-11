#pragma once

// NavInputBuilder: turn live game data (a chunk's square and terrain water rings, a placed
// entity's collider, built walls and doors) into the tagged rings and door portals the
// geometry layer's nav meshes take (geometry::nav::NavInputPolygon, DoorPortal). These are
// PURE functions: they read game state and emit rings in integer millimeters, no ECS, no
// caching (NavigationSystem keeps the meshes and patches them). The meters<->mm boundary is
// crossed only via NavCoords.h.
//
// Provenance ids tag each obstacle's origin so a triangle edge can be attributed to its
// source: negative sentinels for the engine-synthesized obstacles (water/tree/border/junction),
// positive +segmentId for wall bands.

#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygons.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <nav/NavMesh.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace nav {

	// Provenance sentinels. Walls use +segmentId (a positive int64), so all the
	// engine-synthesized obstacle classes take distinct negative ids. These are the
	// `provenanceId` written on each emitted NavInputPolygon.
	constexpr std::int64_t kProvenanceWater	   = -1;
	constexpr std::int64_t kProvenanceTree	   = -2;
	constexpr std::int64_t kProvenanceBorder   = -3;
	constexpr std::int64_t kProvenanceJunction = -4;

	// Pad added outward around a flora collider before emitting its ring, in mm. A
	// small inflation keeps the agent clear of the trunk rather than grazing it.
	constexpr std::int64_t kFloraColliderPadMm = 50;

	// --- Ground ------------------------------------------------------------------
	//
	// A chunk's ground is its square (the walkable bounds ring) and its water. Water is the
	// chunk's terrain polygon navRings, never tiles (terrain-polygons D1, D9), already clipped
	// to the chunk's 512 m square, so two neighbours' water meets along bit-identical border
	// edges and never overlaps.

	// The chunk's square: a CCW ring, blocked=false, provenance=kProvenanceBorder.
	[[nodiscard]] geometry::nav::NavInputPolygon chunkSquare(world::ChunkCoordinate coord);

	// Appends every navRing with blocksMovement as a blocked ring with kProvenanceWater and
	// the ring's holeCapable, orientation kept (a Waterline island stays a CW hole), in
	// stored order. Fordable channel pieces (blocksMovement false) are skipped.
	void appendChunkWater(const world::ChunkTerrainPolygons& polygons, std::vector<geometry::nav::NavInputPolygon>& out);

	// --- Flora -------------------------------------------------------------------

	// One blocked ring for a single placed entity whose AssetDefinition has a
	// blocking collision shape, transformed into world mm by the entity's position
	// (tiles), rotation, and scale. Rect -> a padded oriented quad; Polygon -> its
	// points transformed. Returns nullopt when the entity has no blocking collision
	// (most flora) or the resulting ring is degenerate.
	[[nodiscard]] std::optional<geometry::nav::NavInputPolygon>
	floraRingFor(const assets::PlacedEntity& entity, const assets::AssetRegistry& registry);

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
