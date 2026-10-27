#include "BakedChunkRenderer.h"

#include "world/chunk/ChunkCoordinate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <primitives/Primitives.h>

namespace engine::world {

	// --- Baked Static Mesh Implementation with Sub-Chunk Culling ---
	// CPU bake (vertex transform) lives in BakedEntityMesh.cpp so it can run on
	// worker threads; this file owns only the GL upload and rendering.

	void BakedChunkRenderer::buildBakedChunkMesh(
		const assets::PlacementExecutor& executor, const ChunkCoordinate& coord, uint64_t frameCounter
	) {
		const auto* index = executor.getChunkIndex(coord);
		if (index == nullptr) {
			return;
		}

		WorldPosition chunkOrigin = coord.origin();
		auto entities = index->queryRect(
			chunkOrigin.x, chunkOrigin.y, chunkOrigin.x + kChunkWorldSize, chunkOrigin.y + kChunkWorldSize
		);

		auto cpuData = bakeChunkEntities(entities, coord, [this](const std::string& defName) -> const renderer::TessellatedMesh* {
			if (isGroundcoverDef(defName)) {
				return nullptr; // groundcover renders via the instanced path, not baking
			}
			return m_templateCache.get(defName);
		});
		uploadBakedChunk(coord, std::move(cpuData), frameCounter);
	}

	size_t BakedChunkRenderer::uploadSubChunk(BakedChunkData& bakedData, BakedSubChunkCPUData& cpu, int subIndex) {
		auto&  subChunk = bakedData.subChunks[subIndex];
		size_t bytesUploaded = 0;

		subChunk.minX = cpu.minX;
		subChunk.minY = cpu.minY;
		subChunk.maxX = cpu.maxX;
		subChunk.maxY = cpu.maxY;

		auto& cpuMesh = cpu.floraMesh;
		auto& gpuMesh = subChunk.floraMesh;

		gpuMesh.entityCount = cpuMesh.entityCount;
		gpuMesh.maxEntityHeight = cpuMesh.maxEntityHeight;

		if (cpuMesh.vertices.empty()) {
			gpuMesh.indexCount = 0;
			return bytesUploaded;
		}
		gpuMesh.indexCount = static_cast<uint32_t>(cpuMesh.indices.size());

		gpuMesh.vao = Renderer::GLVertexArray::create();
		gpuMesh.vao.bind();

		gpuMesh.vertexVBO = Renderer::GLBuffer(
			GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.vertices.size() * sizeof(BakedVertex)), cpuMesh.vertices.data(),
			GL_STATIC_DRAW
		);

		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BakedVertex), reinterpret_cast<void*>(0));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BakedVertex), reinterpret_cast<void*>(offsetof(BakedVertex, color)));

		gpuMesh.indexIBO = Renderer::GLBuffer(
			GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuMesh.indices.size() * sizeof(uint32_t)), cpuMesh.indices.data(),
			GL_STATIC_DRAW
		);

		Renderer::GLVertexArray::unbind();

		bytesUploaded += cpuMesh.vertices.size() * sizeof(BakedVertex) + cpuMesh.indices.size() * sizeof(uint32_t);

		// Release CPU-side arrays as they're consumed
		cpuMesh.vertices = {};
		cpuMesh.indices = {};

		return bytesUploaded;
	}

	void BakedChunkRenderer::uploadBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData, uint64_t frameCounter) {
		BakedChunkData bakedData;
		bakedData.lastAccessFrame = frameCounter;
		bakedData.totalEntityCount = cpuData.totalEntityCount;

		for (int subIndex = 0; subIndex < kSubChunkCount; ++subIndex) {
			uploadSubChunk(bakedData, cpuData.subChunks[subIndex], subIndex);
		}

		m_bakedChunkCache[coord] = std::move(bakedData);
	}

	void BakedChunkRenderer::queueBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData, uint64_t frameCounter) {
		// Create the cache entry up front (bounds valid, buffers filled over the
		// next few frames); renderBakedChunks skips sub-chunks with indexCount 0
		BakedChunkData bakedData;
		bakedData.lastAccessFrame = frameCounter;
		bakedData.totalEntityCount = cpuData.totalEntityCount;
		m_bakedChunkCache[coord] = std::move(bakedData);

		m_pendingUploads.push_back(PendingUpload{coord, std::move(cpuData), 0});
	}

	void BakedChunkRenderer::processPendingUploads() {
		const size_t budgetBytes = kUploadBudgetBytesPerFrame;
		size_t bytesUploaded = 0;
		while (bytesUploaded < budgetBytes && !m_pendingUploads.empty()) {
			auto& pending = m_pendingUploads.front();
			auto  cacheIt = m_bakedChunkCache.find(pending.coord);
			if (cacheIt == m_bakedChunkCache.end()) {
				// Cache entry evicted before upload finished; drop the bake
				m_pendingUploads.erase(m_pendingUploads.begin());
				continue;
			}

			while (bytesUploaded < budgetBytes && pending.nextSubChunk < kSubChunkCount) {
				// Defer a sub-chunk that would blow the remaining budget, unless
				// nothing was uploaded yet this frame (avoids starving oversized
				// sub-chunks; worst-case overshoot is then one sub-chunk)
				auto&		cpu = pending.cpuData.subChunks[pending.nextSubChunk];
				const auto& mesh = cpu.floraMesh;
				size_t		estimatedBytes = mesh.vertices.size() * sizeof(BakedVertex) + mesh.indices.size() * sizeof(uint32_t);
				if (bytesUploaded > 0 && bytesUploaded + estimatedBytes > budgetBytes) {
					return;
				}

				bytesUploaded += uploadSubChunk(cacheIt->second, cpu, pending.nextSubChunk);
				pending.nextSubChunk++;
			}

			if (pending.nextSubChunk >= kSubChunkCount) {
				m_pendingUploads.erase(m_pendingUploads.begin());
			}
		}
	}

	void BakedChunkRenderer::releaseBakedChunkCache(const ChunkCoordinate& coord) {
		// RAII wrappers automatically release GPU resources when destroyed
		m_bakedChunkCache.erase(coord);
	}

	void BakedChunkRenderer::renderBakedChunks(const RenderContext& ctx, RenderStats& stats) {
		// Flush any pending batched geometry before drawing baked entities
		Renderer::Primitives::flush();

		// The baked pass projects from the Primitives viewport
		Renderer::Primitives::setViewport(ctx.viewportWidth, ctx.viewportHeight);

		// Calculate visible world bounds for sub-chunk culling
		float zoom = ctx.camera.zoom();
		float camX = ctx.camera.position().x;
		float camY = ctx.camera.position().y;
		VisibleBounds vis = computeVisibleBounds(ctx.camera, ctx.viewportWidth, ctx.viewportHeight, ctx.pixelsPerMeter);

		// The base shader in baked world-space mode, blending on; the VAO draws below are ours
		Renderer::Primitives::beginBakedPass({.position = {camX, camY}, .zoom = zoom, .pixelsPerMeter = ctx.pixelsPerMeter});

		// Far-zoom impostor handoff: pixels of on-screen height per meter of
		// entity height; short flora fades out below kImpostorCutoffPx because
		// the grass tile texture carries the appearance at that distance
		float pixelsPerWorldMeter = ctx.pixelsPerMeter * zoom;
		float currentAlpha = 1.0F;

		// Draw visible sub-chunks from each cached chunk
		for (const auto& coord : ctx.processedChunks) {
			auto cacheIt = m_bakedChunkCache.find(coord);
			if (cacheIt == m_bakedChunkCache.end()) {
				continue; // Not cached yet - will be built next frame
			}

			auto& cache = cacheIt->second;
			cache.lastAccessFrame = ctx.frameCounter; // Update LRU timestamp

			// Check each sub-chunk for visibility
			for (const auto& subChunk : cache.subChunks) {
				// AABB intersection test: is sub-chunk visible?
				if (subChunk.maxX < vis.minX || subChunk.minX > vis.maxX || subChunk.maxY < vis.minY || subChunk.minY > vis.maxY) {
					continue; // Sub-chunk is completely off-screen
				}

				const auto& mesh = subChunk.floraMesh;
				if (mesh.indexCount == 0) {
					continue; // Empty sub-region
				}

				// Cutoff with a fade band from kImpostorCutoffPx to 2x that: short
				// flora hands off to the grass tile texture at that zoom.
				float screenHeightPx = mesh.maxEntityHeight * pixelsPerWorldMeter;
				float alpha = std::clamp((screenHeightPx - kImpostorCutoffPx) / kImpostorCutoffPx, 0.0F, 1.0F);
				if (alpha <= 0.0F) {
					continue;
				}
				if (alpha != currentAlpha) {
					Renderer::Primitives::setBakedAlpha(alpha);
					currentAlpha = alpha;
				}

				stats.entities += mesh.entityCount;
				stats.drawCalls++;
				stats.triangles += mesh.indexCount / 3;
				mesh.vao.bind();
				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indexCount), GL_UNSIGNED_INT, nullptr);
			}
		}

		// Alpha back to 1 for the dynamic instanced path (same shader branch), VAO
		// unbound, blending off: the state every batch draw leaves behind
		Renderer::Primitives::endBakedPass();
	}

	void BakedChunkRenderer::evictLRU(const std::unordered_set<ChunkCoordinate>& processedChunks) {
		// Keep recently-used chunks cached even when not visible, to avoid re-uploading
		// when panning back and forth. Only evict oldest when cache exceeds threshold.
		if (m_bakedChunkCache.size() > kMaxCachedChunks) {
			// Collect all chunks with their last access frame
			std::vector<std::pair<ChunkCoordinate, uint64_t>> chunksByAge;
			chunksByAge.reserve(m_bakedChunkCache.size());
			for (const auto& [coord, cache] : m_bakedChunkCache) {
				// Don't evict currently visible chunks
				if (processedChunks.find(coord) == processedChunks.end()) {
					chunksByAge.emplace_back(coord, cache.lastAccessFrame);
				}
			}

			// Sort by age (oldest first = lowest frame number)
			std::sort(chunksByAge.begin(), chunksByAge.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

			// Evict oldest chunks to get back under limit
			size_t toEvictCount = std::min(chunksByAge.size(), kEvictionBatchSize);
			for (size_t i = 0; i < toEvictCount; ++i) {
				releaseBakedChunkCache(chunksByAge[i].first);
			}
		}
	}

} // namespace engine::world
