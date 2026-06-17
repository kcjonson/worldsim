#pragma once

// NavInputBuilder: turn live game data (water tiles, blocking flora, built walls
// and doors) into the geometry layer's navmesh input (geometry::nav::NavMeshInput).
// These are PURE functions: they read game state and emit tagged rings/portals in
// integer millimeters, no ECS, no async, no caching (the NavigationSystem that
// caches and rebuilds against version counters is a separate later step). The
// meters<->mm boundary is crossed only via NavCoords.h.
//
// A NavMeshInput needs at least one unblocked polygon (the walkable border) plus
// the blocked obstacle rings. Provenance ids tag each obstacle's origin so a later
// pass can attribute a triangle edge to its source: negative sentinels for the
// engine-synthesized obstacles (water/tree/border/junction), positive +segmentId
// for wall bands.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>

#include <nav/NavMesh.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace engine::nav {

	// Provenance sentinels. Walls use +segmentId (a positive int64), so all the
	// engine-synthesized obstacle classes take distinct negative ids. These are the
	// `provenanceId` written on each emitted NavInputPolygon.
	constexpr std::int64_t kProvenanceWater	   = -1;
	constexpr std::int64_t kProvenanceTree	   = -2;
	constexpr std::int64_t kProvenanceBorder   = -3;
	constexpr std::int64_t kProvenanceJunction = -4;

	// Pad added around a circle collision shape's radius before octagon-izing it, in
	// mm. A small inflation keeps the agent's disc clear of the trunk rather than
	// grazing it.
	constexpr std::int64_t kFloraCirclePadMm = 50;

	// --- Water -------------------------------------------------------------------

	// Marching-squares core, exposed for testing without a real Chunk. `isWater` is
	// queried over the tile grid [0, width) x [0, height); `originMm` is the world-mm
	// position of tile (0,0)'s corner. Emits one blocked NavInputPolygon per closed
	// water boundary loop, in world mm: outer boundaries CCW, holes (land islands)
	// CW. Loops are simplified (collinear collapse) and sub-tile slivers dropped.
	[[nodiscard]] std::vector<geometry::nav::NavInputPolygon>
	extractWaterObstacles(int width, int height, const std::function<bool(int, int)>& isWater, geometry::Vec2i64 originMm);

	// A tile is water if its surface is Water or its primary biome is a water biome.
	// Iterates the chunk's 512x512 tiles. Requires chunk.isReady().
	[[nodiscard]] std::vector<geometry::nav::NavInputPolygon> extractWaterObstacles(const world::Chunk& chunk);

	// --- Flora -------------------------------------------------------------------

	// One blocked ring per placed entity whose AssetDefinition has a blocking
	// collision shape, transformed into world mm by the entity's position (tiles),
	// rotation, and scale. Circle -> a padded octagon; Polygon -> its points
	// transformed. Entities with no collision shape (most flora) emit nothing.
	[[nodiscard]] std::vector<geometry::nav::NavInputPolygon>
	extractFloraObstacles(const assets::SpatialIndex& index, const assets::AssetRegistry& registry);

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
	// provenance=kProvenanceBorder. The one unblocked polygon every NavMeshInput
	// needs.
	[[nodiscard]] geometry::nav::NavInputPolygon borderRing(geometry::Vec2i64 minMm, geometry::Vec2i64 maxMm);

	// --- Assembler ---------------------------------------------------------------

	// Assemble a full NavMeshInput from a set of loaded chunks plus the construction
	// world: border derived from the chunks' combined bounds, then the water/flora/
	// wall extractors concatenated. Null chunks and not-ready chunks are skipped.
	// This is the straightforward synchronous assembler and the unit-test entry
	// point; the async caching service is a separate step.
	[[nodiscard]] geometry::nav::NavMeshInput buildInput(const std::vector<const world::Chunk*>& chunks,
														 const assets::PlacementExecutor& placement, const assets::AssetRegistry& assetReg,
														 const construction::ConstructionWorld& world, const assets::ConstructionRegistry& cfg);

} // namespace engine::nav
