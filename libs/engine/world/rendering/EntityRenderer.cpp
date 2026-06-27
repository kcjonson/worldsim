#include "EntityRenderer.h"

namespace engine::world {

	EntityRenderer::EntityRenderer(float pixelsPerMeter)
		: m_pixelsPerMeter(pixelsPerMeter) {}

	EntityRenderer::~EntityRenderer() = default;

	void EntityRenderer::render(
		const assets::PlacementExecutor&		   executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const WorldCamera&						   camera,
		int										   viewportWidth,
		int										   viewportHeight
	) {
		if (m_useInstancing) {
			renderInstanced(executor, processedChunks, nullptr, camera, viewportWidth, viewportHeight);
		} else {
			RenderStats stats;
			batched.render(executor, processedChunks, nullptr, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, stats);
			m_lastEntityCount = stats.entities;
			m_lastDrawCallCount = stats.drawCalls;
			m_lastTriangleCount = stats.triangles;
		}
	}

	void EntityRenderer::render(
		const assets::PlacementExecutor&		   executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>&   dynamicEntities,
		const WorldCamera&						   camera,
		int										   viewportWidth,
		int										   viewportHeight
	) {
		if (m_useInstancing) {
			renderInstanced(executor, processedChunks, &dynamicEntities, camera, viewportWidth, viewportHeight);
		} else {
			RenderStats stats;
			batched.render(executor, processedChunks, &dynamicEntities, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, stats);
			m_lastEntityCount = stats.entities;
			m_lastDrawCallCount = stats.drawCalls;
			m_lastTriangleCount = stats.triangles;
		}
	}

	// --- GPU Instancing Path ---

	void EntityRenderer::renderInstanced(
		const assets::PlacementExecutor&		   executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>*   dynamicEntities,
		const WorldCamera&						   camera,
		int										   viewportWidth,
		int										   viewportHeight
	) {
		// Increment frame counter for LRU tracking
		frameCounter++;

		// Canonical per-frame metrics: reset once, then each sub-renderer adds to it.
		RenderStats stats;

		// --- Phase 1: Integrate baked meshes ---
		// Budgeted upload of worker-baked chunks (capped bytes per frame)
		baked.processPendingUploads();

		// Synchronous re-bake only for processed chunks whose baked mesh was
		// LRU-evicted and later revisited; new chunks arrive via the queue.
		for (const auto& coord : processedChunks) {
			if (baked.needsBake(coord)) {
				baked.buildBakedChunkMesh(executor, coord, frameCounter);
			}
		}

		// --- Phase 2: Render static entities from baked per-chunk meshes ---
		// Fast path: single glDrawElements per chunk, no instancing overhead.
		baked.renderBakedChunks(processedChunks, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, frameCounter, m_uniforms, stats);

		// --- Phase 2b: Render groundcover (grass) via GPU instancing ---
		// Groundcover entities are skipped by the baked path and drawn here as instanced
		// variant tufts, with a zoom LOD that fades to the grass tile texture when zoomed out.
		groundcover.render(executor, processedChunks, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, frameCounter, m_uniforms, stats);

		// --- Phase 3: Render dynamic entities (per-frame rebuild) ---
		instancedDynamic.renderDynamic(dynamicEntities, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, stats);

		// --- Phase 4: LRU cache eviction ---
		baked.evictLRU(processedChunks);
		groundcover.evictLRU(processedChunks);

		m_lastEntityCount = stats.entities;
		m_lastDrawCallCount = stats.drawCalls;
		m_lastTriangleCount = stats.triangles;
	}

	void EntityRenderer::uploadBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData) {
		baked.uploadBakedChunk(coord, std::move(cpuData), frameCounter);
	}

	void EntityRenderer::queueBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData) {
		baked.queueBakedChunk(coord, std::move(cpuData), frameCounter);
	}

} // namespace engine::world
