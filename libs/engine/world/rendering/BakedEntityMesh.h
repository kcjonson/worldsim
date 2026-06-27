#pragma once

// CPU-side bake of placed entities into world-space sub-chunk vertex arrays.
// Runs on worker threads (as a continuation of async chunk placement) or on the
// render thread (re-bake of an evicted chunk); GL upload lives in
// EntityRenderer::uploadBakedChunk. Touches no GL state.

#include "assets/AssetRegistry.h"
#include "assets/placement/SpatialIndex.h"
#include "world/chunk/ChunkCoordinate.h"

#include <graphics/Color.h>
#include <math/Types.h>
#include <vector/Tessellator.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine::world {

/// Sub-chunk grid: 8x8 = 64 sub-regions per chunk, 64x64 tiles each.
/// Sub-regions are the granularity of view-frustum culling for baked entities.
inline constexpr int kSubChunkGridSize = 8;
inline constexpr int kSubChunkTileSize = kChunkSize / kSubChunkGridSize;
inline constexpr float kSubChunkWorldSize = static_cast<float>(kSubChunkTileSize) * kTileSize;
inline constexpr int kSubChunkCount = kSubChunkGridSize * kSubChunkGridSize;

/// Baked meshes are split into height buckets so short flora (grass) can stop
/// rendering as geometry once blades shrink below ~kImpostorCutoffPx on screen;
/// the rasterized grass tile texture carries the appearance at that distance.
/// Tall flora (bushes, trees) has no texture backup and always draws.
/// This keeps far-zoom triangle count independent of grass density.
inline constexpr int kFloraBucketCount = 2;
inline constexpr int kShortFloraBucket = 0;
inline constexpr int kTallFloraBucket = 1;
inline constexpr float kShortFloraMaxHeight = 1.0F; // meters; bucket split point
inline constexpr float kImpostorCutoffPx = 3.0F;	// on-screen height where short flora fades out

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

/// CPU-side meshes for one sub-region, ready for GL upload
struct BakedSubChunkCPUData {
	std::array<BakedMeshCPU, kFloraBucketCount> buckets;
	float										minX = 0, minY = 0, maxX = 0, maxY = 0; // World-space bounds for culling
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

/// Resolves an asset defName to its tessellated template mesh.
/// Must be safe to call from the thread running the bake.
using TemplateLookup = std::function<const renderer::TessellatedMesh*(const std::string&)>;

/// Transform placed entities into world-space sub-chunk vertex arrays.
[[nodiscard]] BakedChunkCPUData bakeChunkEntities(
	const std::vector<const assets::PlacedEntity*>& entities,
	ChunkCoordinate coord,
	const TemplateLookup& getTemplate
);

} // namespace engine::world
