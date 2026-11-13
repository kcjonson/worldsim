#include "EntityRenderer.h"

namespace worldview {

	EntityRenderer::EntityRenderer(float pixelsPerMeter)
		: m_pixelsPerMeter(pixelsPerMeter) {}

	EntityRenderer::~EntityRenderer() = default;

	void EntityRenderer::render(
		const assets::PlacementExecutor&				  executor,
		const std::unordered_set<world::ChunkCoordinate>& processedChunks,
		const world::WorldCamera&						  camera,
		int												  viewportWidth,
		int												  viewportHeight
	) {
		renderFrame(executor, processedChunks, nullptr, camera, viewportWidth, viewportHeight);
	}

	void EntityRenderer::render(
		const assets::PlacementExecutor&				  executor,
		const std::unordered_set<world::ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>&		  dynamicEntities,
		const world::WorldCamera&						  camera,
		int												  viewportWidth,
		int												  viewportHeight
	) {
		renderFrame(executor, processedChunks, &dynamicEntities, camera, viewportWidth, viewportHeight);
	}

	void EntityRenderer::renderFrame(
		const assets::PlacementExecutor&				  executor,
		const std::unordered_set<world::ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>*		  dynamicEntities,
		const world::WorldCamera&						  camera,
		int												  viewportWidth,
		int												  viewportHeight
	) {
		// Increment frame counter for LRU tracking, then freeze the per-frame
		// inputs into one context shared by every render path.
		frameCounter++;
		assets::RenderContext ctx{
			executor, processedChunks, dynamicEntities, camera, viewportWidth, viewportHeight, m_pixelsPerMeter, frameCounter
		};

		// Canonical per-frame metrics: reset once, then each sub-renderer adds to it.
		RenderStats stats;

		// Integrate baked meshes: budgeted upload of worker-baked chunks (capped
		// bytes per frame), then a synchronous re-bake only for processed chunks
		// whose baked mesh was LRU-evicted and later revisited (new chunks arrive
		// via the queue), and of the sub-regions a baked static was removed from.
		baked.processPendingUploads();
		for (const auto& coord : processedChunks) {
			if (baked.needsBake(coord)) {
				baked.buildBakedChunkMesh(executor, coord, frameCounter);
			} else {
				baked.rebakeEditedSubChunks(executor, coord);
			}
		}

		// Background: baked SHORT flora only (tall occluders are gathered live
		// below). Single glDrawElements per chunk sub-region, no instancing overhead.
		baked.renderBakedChunks(ctx, stats);

		// Groundcover (grass) via GPU instancing. Skipped by the baked path and
		// drawn here as instanced variant tufts, with a zoom LOD that fades to the
		// grass tile texture when zoomed out.
		groundcover.render(ctx, stats);

		// Y-sorted upright stream: visible tall static occluders merged with the
		// dynamic ECS entities, stable-sorted ascending by ground-contact anchorY,
		// then emitted in that order so submission order == depth order (larger
		// anchorY drawn later / in front).
		uprightGather.gather(ctx, m_sortedUprights);
		instancedDynamic.emitSorted(m_sortedUprights, ctx, stats);

		// LRU cache eviction.
		baked.evictLRU(processedChunks);
		groundcover.evictLRU(processedChunks);

		m_lastEntityCount = stats.entities;
		m_lastDrawCallCount = stats.drawCalls;
		m_lastTriangleCount = stats.triangles;
		m_lastUprightDrawCallCount = stats.uprightDrawCalls;
		m_lastUprightTriangleCount = stats.uprightTriangles;
		m_lastUprightOverlapSplits = stats.uprightOverlapSplits;
	}

	// A worker bake arrives with each placement of a chunk, the first and every reload: the baked
	// mesh is replaced by it, and the groundcover built from the index before it is dropped.
	void EntityRenderer::uploadBakedChunk(const world::ChunkCoordinate& coord, assets::BakedChunkCPUData&& cpuData) {
		baked.uploadBakedChunk(coord, std::move(cpuData), frameCounter);
		groundcover.forgetChunk(coord);
	}

	void EntityRenderer::queueBakedChunk(const world::ChunkCoordinate& coord, assets::BakedChunkCPUData&& cpuData) {
		baked.queueBakedChunk(coord, std::move(cpuData), frameCounter);
		groundcover.forgetChunk(coord);
	}

} // namespace worldview
