#pragma once

// BakedChunkRenderer - Baked static-entity path with sub-chunk culling.
// Entity vertices are pre-transformed to world space once per chunk (on a
// worker thread via AsyncChunkProcessor, or on the render thread when an
// evicted chunk is revisited). Sub-regions are culled against the viewport.
// CPU bake types/constants live in BakedEntityMesh.h.

#include "assets/placement/PlacementExecutor.h"
#include "gl/GLBuffer.h"
#include "gl/GLVertexArray.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/rendering/BakedEntityMesh.h"
#include "world/rendering/InstancingUniforms.h"
#include "world/rendering/RenderContext.h"
#include "world/rendering/TemplateMeshCache.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <GL/glew.h>

namespace engine::world {

/// Renders static placed entities using baked per-chunk meshes (glDrawElements,
/// no instancing). Owns the baked GPU cache, the budgeted upload queue, and the
/// LRU eviction policy.
class BakedChunkRenderer {
  public:
	/// Re-bake a chunk from the executor's spatial index on the render thread.
	/// Only used when a still-loaded chunk's baked mesh was LRU-evicted; new
	/// chunks arrive pre-baked via uploadBakedChunk.
	void buildBakedChunkMesh(const assets::PlacementExecutor& executor, const ChunkCoordinate& coord, uint64_t frameCounter);

	/// Release a chunk's baked mesh GPU resources so it re-bakes on next render
	/// (used when the placement index changed, e.g. entities were cleared).
	void releaseBakedChunkCache(const ChunkCoordinate& coord);

	/// Upload a CPU-baked chunk mesh (from a worker thread bake) to the GPU.
	/// Call on the render thread; replaces any existing baked data for the chunk.
	void uploadBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData, uint64_t frameCounter);

	/// Queue a CPU-baked chunk mesh for budgeted upload (a few sub-chunks per
	/// frame, processed inside render). Avoids multi-ms frame spikes when
	/// several chunk bakes complete at once while scrolling.
	void queueBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData, uint64_t frameCounter);

	/// Upload pending sub-chunks until the per-frame byte budget is exhausted (render thread)
	void processPendingUploads();

	/// True if the chunk has no baked cache entry (needs a synchronous re-bake).
	[[nodiscard]] bool needsBake(const ChunkCoordinate& coord) const {
		return m_bakedChunkCache.find(coord) == m_bakedChunkCache.end();
	}

	/// Render static entities using baked per-chunk meshes (glDrawElements, no instancing).
	void renderBakedChunks(const RenderContext& ctx, InstancingUniforms& uniforms, RenderStats& stats);

	/// LRU cache eviction. Keep recently-used chunks cached even when not visible,
	/// to avoid re-uploading when panning back and forth. Only evict oldest when
	/// cache exceeds threshold.
	void evictLRU(const std::unordered_set<ChunkCoordinate>& processedChunks);

  private:
	// --- LRU Cache Configuration ---
	// Keep recently-used chunks cached even when not visible, to avoid re-uploading
	// when panning back and forth. Only evict oldest when cache exceeds threshold.
	static constexpr size_t kMaxCachedChunks = 64;     // Max chunks to keep in cache
	static constexpr size_t kEvictionBatchSize = 8;    // How many to evict when over limit

	/// Max bytes of baked vertex/index data uploaded per frame. A byte budget
	/// (rather than a sub-chunk count) keeps the per-frame cost flat as flora
	/// density grows; 2MB is well under 1ms of PCIe transfer.
	static constexpr size_t kUploadBudgetBytesPerFrame = 2 * 1024 * 1024;

	/// GPU resources for one height bucket of one sub-region.
	struct BakedMeshGPU {
		Renderer::GLVertexArray vao;     // VAO with baked vertex data
		Renderer::GLBuffer vertexVBO;    // Pre-transformed vertices (world-space)
		Renderer::GLBuffer indexIBO;     // Combined indices
		uint32_t indexCount = 0;         // Total indices in IBO
		uint32_t entityCount = 0;        // For debugging/metrics
		float maxEntityHeight = 0.0F;    // Drives the far-zoom cutoff (short bucket)
	};

	/// GPU resources for a single sub-region's baked entity meshes.
	struct BakedSubChunkData {
		std::array<BakedMeshGPU, kFloraBucketCount> buckets;
		float minX = 0, minY = 0;        // World-space bounds for culling
		float maxX = 0, maxY = 0;
	};

	/// GPU resources for a chunk, subdivided into sub-regions.
	struct BakedChunkData {
		std::array<BakedSubChunkData, kSubChunkCount> subChunks;
		uint32_t totalEntityCount = 0;   // For debugging/metrics
		uint64_t lastAccessFrame = 0;    // For LRU eviction
	};

	/// Cache of baked per-chunk meshes.
	std::unordered_map<ChunkCoordinate, BakedChunkData> m_bakedChunkCache;

	/// CPU bakes waiting for budgeted GPU upload (FIFO; partially uploaded
	/// chunks track progress via nextSubChunk)
	struct PendingUpload {
		ChunkCoordinate coord;
		BakedChunkCPUData cpuData;
		int nextSubChunk = 0;
	};
	std::vector<PendingUpload> m_pendingUploads;

	// Memoized template meshes (keyed by defName)
	TemplateMeshCache m_templateCache;

	/// Upload a single sub-chunk's buffers into an existing cache entry.
	/// @return Bytes of vertex+index data uploaded
	size_t uploadSubChunk(BakedChunkData& bakedData, BakedSubChunkCPUData& cpu, int subIndex);
};

}  // namespace engine::world
