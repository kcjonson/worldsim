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
			RenderStats	  stats;
			RenderContext ctx{executor, processedChunks, nullptr, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, frameCounter};
			batched.render(ctx, m_uniforms, stats);
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
			RenderStats	  stats;
			RenderContext ctx{
				executor, processedChunks, &dynamicEntities, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, frameCounter
			};
			batched.render(ctx, m_uniforms, stats);
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
		// Increment frame counter for LRU tracking, then freeze the per-frame
		// inputs into one context shared by every render path.
		frameCounter++;
		RenderContext ctx{
			executor, processedChunks, dynamicEntities, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, frameCounter
		};

		// Canonical per-frame metrics: reset once, then each sub-renderer adds to it.
		RenderStats stats;

		// Integrate baked meshes: budgeted upload of worker-baked chunks (capped
		// bytes per frame), then a synchronous re-bake only for processed chunks
		// whose baked mesh was LRU-evicted and later revisited (new chunks arrive
		// via the queue).
		baked.processPendingUploads();
		for (const auto& coord : processedChunks) {
			if (baked.needsBake(coord)) {
				baked.buildBakedChunkMesh(executor, coord, frameCounter);
			}
		}

		// Background: baked SHORT flora only (tall occluders are gathered live
		// below). Single glDrawElements per chunk sub-region, no instancing overhead.
		baked.renderBakedChunks(ctx, m_uniforms, stats);

		// Groundcover (grass) via GPU instancing. Skipped by the baked path and
		// drawn here as instanced variant tufts, with a zoom LOD that fades to the
		// grass tile texture when zoomed out.
		groundcover.render(ctx, m_uniforms, stats);

		// Y-sorted upright stream: visible tall static occluders merged with the
		// dynamic ECS entities, stable-sorted ascending by ground-contact anchorY,
		// then emitted in that order so submission order == depth order (larger
		// anchorY drawn later / in front).
		uprightGather.gather(ctx, m_sortedUprights, /*backgroundOnFastPath=*/true);
		instancedDynamic.emitSorted(m_sortedUprights, ctx, stats);

		// LRU cache eviction.
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
