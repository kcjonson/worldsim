#pragma once

// CPU-side bake of placed entities into world-space sub-chunk vertex arrays.
// Runs on worker threads (as a continuation of async chunk placement) or on the
// render thread (re-bake of an evicted chunk, or of one sub-region a short static
// was removed from); GL upload lives in BakedChunkRenderer. Touches no GL state.

#include "assets/AssetRegistry.h"
#include "assets/placement/FloraRouting.h"
#include "assets/placement/SpatialIndex.h"
#include "assets/placement/SubChunkGrid.h"
#include "world/chunk/ChunkCoordinate.h"

#include <graphics/Color.h>
#include <math/Types.h>
#include <vector/Tessellator.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace assets {

/// Only statics of a def that routes short (FloraRouting.h) are baked. They can stop
/// rendering as geometry once blades shrink below ~kImpostorCutoffPx on screen; the
/// rasterized grass tile texture carries the appearance at that distance, keeping
/// far-zoom triangle count independent of grass density. Tall occluders
/// (PlacedEntity::isTallOccluder) are excluded from the bake and drawn live in the
/// Y-sorted upright stream (WorldDepthSort) so they interleave with colonists by
/// ground-contact Y.
inline constexpr float kImpostorCutoffPx = 3.0F; // on-screen height where short flora fades out

/// Per-vertex data: position (Vec2) + color (Color) = 24 bytes
struct BakedVertex {
	Foundation::Vec2  position; // World-space position
	Foundation::Color color;	// Pre-tinted color
};
static_assert(sizeof(BakedVertex) == 24, "BakedVertex must be 24 bytes");

/// CPU-side mesh for one height bucket of one sub-region
struct BakedMeshCPU {
	std::vector<BakedVertex> vertices;
	std::vector<uint32_t>	 indices;
	uint32_t				 entityCount = 0;
	float					 maxEntityHeight = 0.0F; // tallest entity (meters), drives the zoom cutoff
};

/// CPU-side mesh for one sub-region, ready for GL upload
struct BakedSubChunkCPUData {
	BakedMeshCPU floraMesh;										  // Baked short flora for this sub-region
	float		 minX = 0, minY = 0, maxX = 0, maxY = 0; // World-space bounds for culling
};

/// CPU-side meshes for a whole chunk
struct BakedChunkCPUData {
	std::array<BakedSubChunkCPUData, kSubChunkCount> subChunks;
	uint32_t										 totalEntityCount = 0;
};

/// True if `defName` is a groundcover asset. Groundcover is skipped by the baked
/// path (it renders via the instanced GroundcoverRenderer), so the bake-time
/// template lookups in both BakedChunkRenderer and AsyncChunkProcessor return
/// null for it. Shared so that skip rule lives in one place.
[[nodiscard]] inline bool isGroundcoverDef(const std::string& defName) {
	const auto* def = assets::AssetRegistry::Get().getDefinition(defName);
	return def != nullptr && def->role == assets::AssetRole::Groundcover;
}

/// Resolves a placed entity to the template mesh it draws (its templateId), or null to
/// leave it out of the bake. Must be safe to call from the thread running the bake.
using TemplateLookup = std::function<const renderer::TessellatedMesh*(const assets::PlacedEntity&)>;

/// Transform placed entities into world-space sub-chunk vertex arrays.
[[nodiscard]] BakedChunkCPUData bakeChunkEntities(
	const std::vector<const assets::PlacedEntity*>& entities,
	world::ChunkCoordinate coord,
	const TemplateLookup& getTemplate
);

/// One sub-region of a chunk baked again from what its index holds now: the re-bake after a
/// short static in it was removed (PlacementExecutor::staticRemovals), a sixty-fourth of the chunk's.
[[nodiscard]] BakedSubChunkCPUData bakeSubChunkEntities(
	const assets::SpatialIndex& index,
	world::ChunkCoordinate coord,
	int subIndex,
	const TemplateLookup& getTemplate
);

} // namespace assets
