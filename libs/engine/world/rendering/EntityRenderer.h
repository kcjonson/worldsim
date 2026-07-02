#pragma once

// EntityRenderer - Renders placed entities on top of chunk tiles.
// Batches all visible entities into a single draw call per frame.
// Uses view frustum culling for efficiency.
//
// Supports two rendering paths:
// 1. GPU Instancing (default): One draw call per mesh type, transforms on GPU
// 2. CPU Batching (fallback): All entities in one draw call, transforms on CPU
//
// This class is a thin orchestrator: it owns shared state (pixelsPerMeter, the
// frame counter, the cached uniform locations, and the per-frame metrics) and
// delegates each render path to a dedicated sub-renderer.

#include "assets/placement/PlacementExecutor.h"
#include "world/camera/WorldCamera.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/rendering/BakedChunkRenderer.h"
#include "world/rendering/BakedEntityMesh.h"
#include "world/rendering/BatchedEntityRenderer.h"
#include "world/rendering/GroundcoverRenderer.h"
#include "world/rendering/InstancedEntityRenderer.h"
#include "world/rendering/InstancingUniforms.h"
#include "world/rendering/RenderContext.h"
#include "world/rendering/WorldDepthSort.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace engine::world {

/// Renders entities placed by the PlacementExecutor.
/// Groups entities by asset type and batches them for efficient rendering.
class EntityRenderer {
  public:
	/// Create an entity renderer
	/// @param pixelsPerMeter Scale factor for world-to-screen conversion
	explicit EntityRenderer(float pixelsPerMeter = 16.0F);

	/// Destructor - releases GPU resources
	~EntityRenderer();

	// Non-copyable, non-movable (owns GPU resources that require explicit release)
	EntityRenderer(const EntityRenderer&) = delete;
	EntityRenderer& operator=(const EntityRenderer&) = delete;
	EntityRenderer(EntityRenderer&&) = delete;
	EntityRenderer& operator=(EntityRenderer&&) = delete;

	/// Release a chunk's baked render mesh so it re-bakes (e.g. after entities
	/// were removed from the placement index). Delegates to the baked sub-renderer.
	void releaseBakedChunkCache(const ChunkCoordinate& coord) { baked.releaseBakedChunkCache(coord); }

	/// Render entities from processed chunks
	/// @param executor PlacementExecutor containing entity data
	/// @param processedChunks Set of chunk coordinates that have been processed
	/// @param camera Camera for coordinate transforms
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	void render(const assets::PlacementExecutor& executor,
				const std::unordered_set<ChunkCoordinate>& processedChunks,
				const WorldCamera& camera,
				int viewportWidth,
				int viewportHeight);

	/// Render entities from processed chunks plus additional dynamic entities
	/// @param executor PlacementExecutor containing entity data
	/// @param processedChunks Set of chunk coordinates that have been processed
	/// @param dynamicEntities Additional entities to render (e.g., from ECS)
	/// @param camera Camera for coordinate transforms
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	void render(const assets::PlacementExecutor& executor,
				const std::unordered_set<ChunkCoordinate>& processedChunks,
				const std::vector<assets::PlacedEntity>& dynamicEntities,
				const WorldCamera& camera,
				int viewportWidth,
				int viewportHeight);

	/// Set pixels per meter (zoom level)
	void setPixelsPerMeter(float pixelsPerMeter) { m_pixelsPerMeter = pixelsPerMeter; }
	[[nodiscard]] float pixelsPerMeter() const { return m_pixelsPerMeter; }

	/// Get number of entities rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastEntityCount() const { return m_lastEntityCount; }

	/// Draw call / triangle counts from last frame (raw GL draws bypass the
	/// BatchRenderer stats, so the metrics system reads them from here)
	[[nodiscard]] uint32_t lastDrawCallCount() const { return m_lastDrawCallCount; }
	[[nodiscard]] uint32_t lastTriangleCount() const { return m_lastTriangleCount; }

	/// Upload a CPU-baked chunk mesh (from a worker thread bake) to the GPU.
	/// Call on the render thread; replaces any existing baked data for the chunk.
	void uploadBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData);

	/// Queue a CPU-baked chunk mesh for budgeted upload (a few sub-chunks per
	/// frame, processed inside render). Avoids multi-ms frame spikes when
	/// several chunk bakes complete at once while scrolling.
	void queueBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData);

	/// Enable/disable GPU instancing (for A/B testing and fallback)
	void setInstancingEnabled(bool enabled) { m_useInstancing = enabled; }
	[[nodiscard]] bool isInstancingEnabled() const { return m_useInstancing; }

  private:
	float m_pixelsPerMeter = 16.0F;
	uint32_t m_lastEntityCount = 0;
	uint32_t m_lastDrawCallCount = 0;
	uint32_t m_lastTriangleCount = 0;
	uint64_t frameCounter = 0;  // Incremented each render call (for LRU tracking)

	// --- Instancing Mode ---
	bool m_useInstancing = true;

	/// Shared cached uniform locations for the baked + groundcover paths.
	InstancingUniforms m_uniforms;

	// --- Path sub-renderers (owned by value) ---
	BakedChunkRenderer		 baked;
	GroundcoverRenderer		 groundcover;
	InstancedEntityRenderer	 instancedDynamic;
	BatchedEntityRenderer	 batched;

	// Gathers + sorts the visible upright stream (tall statics + dynamic ECS) each
	// frame; m_sortedUprights is reused to keep its capacity across frames.
	WorldDepthGather		   uprightGather;
	std::vector<DepthSortItem> m_sortedUprights;

	/// Render using GPU instancing path
	void renderInstanced(
		const assets::PlacementExecutor& executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>* dynamicEntities,
		const WorldCamera& camera,
		int viewportWidth,
		int viewportHeight
	);
};

}  // namespace engine::world
