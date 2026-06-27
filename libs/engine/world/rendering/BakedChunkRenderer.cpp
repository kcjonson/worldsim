#include "BakedChunkRenderer.h"

#include "assets/AssetRegistry.h"
#include "world/chunk/ChunkCoordinate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>

namespace engine::world {

	// --- Baked Static Mesh Implementation with Sub-Chunk Culling ---
	// CPU bake (vertex transform) lives in BakedEntityMesh.cpp so it can run on
	// worker threads; this file owns only the GL upload and rendering.

	const renderer::TessellatedMesh* BakedChunkRenderer::getTemplate(const std::string& defName) {
		// Check cache first
		auto it = m_templateCache.find(defName);
		if (it != m_templateCache.end()) {
			return it->second;
		}

		// Get from registry and cache
		auto&		registry = assets::AssetRegistry::Get();
		const auto* mesh = registry.getTemplate(defName);
		m_templateCache[defName] = mesh;
		return mesh;
	}

	void BakedChunkRenderer::buildBakedChunkMesh(
		const assets::PlacementExecutor& executor, const ChunkCoordinate& coord, uint64_t frameCounter
	) {
		const auto* index = executor.getChunkIndex(coord);
		if (index == nullptr) {
			return;
		}

		WorldPosition chunkOrigin = coord.origin();
		constexpr float kChunkWorldSize = static_cast<float>(kChunkSize) * kTileSize;
		auto entities = index->queryRect(
			chunkOrigin.x, chunkOrigin.y, chunkOrigin.x + kChunkWorldSize, chunkOrigin.y + kChunkWorldSize
		);

		auto cpuData = bakeChunkEntities(entities, coord, [this](const std::string& defName) -> const renderer::TessellatedMesh* {
			const auto* def = assets::AssetRegistry::Get().getDefinition(defName);
			if (def != nullptr && def->role == assets::AssetRole::Groundcover) {
				return nullptr; // groundcover renders via the instanced path, not baking
			}
			return getTemplate(defName);
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

		for (int bucketIndex = 0; bucketIndex < kFloraBucketCount; ++bucketIndex) {
			auto& cpuBucket = cpu.buckets[bucketIndex];
			auto& gpuBucket = subChunk.buckets[bucketIndex];

			gpuBucket.entityCount = cpuBucket.entityCount;
			gpuBucket.maxEntityHeight = cpuBucket.maxEntityHeight;

			if (cpuBucket.vertices.empty()) {
				gpuBucket.indexCount = 0;
				continue;
			}
			gpuBucket.indexCount = static_cast<uint32_t>(cpuBucket.indices.size());

			gpuBucket.vao = Renderer::GLVertexArray::create();
			gpuBucket.vao.bind();

			gpuBucket.vertexVBO = Renderer::GLBuffer(
				GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuBucket.vertices.size() * sizeof(BakedVertex)), cpuBucket.vertices.data(),
				GL_STATIC_DRAW
			);

			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BakedVertex), reinterpret_cast<void*>(0));
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BakedVertex), reinterpret_cast<void*>(offsetof(BakedVertex, color)));

			gpuBucket.indexIBO = Renderer::GLBuffer(
				GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(cpuBucket.indices.size() * sizeof(uint32_t)), cpuBucket.indices.data(),
				GL_STATIC_DRAW
			);

			Renderer::GLVertexArray::unbind();

			bytesUploaded += cpuBucket.vertices.size() * sizeof(BakedVertex) + cpuBucket.indices.size() * sizeof(uint32_t);

			// Release CPU-side arrays as they're consumed
			cpuBucket.vertices = {};
			cpuBucket.indices = {};
		}

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
				auto&  cpu = pending.cpuData.subChunks[pending.nextSubChunk];
				size_t estimatedBytes = 0;
				for (const auto& bucket : cpu.buckets) {
					estimatedBytes += bucket.vertices.size() * sizeof(BakedVertex) + bucket.indices.size() * sizeof(uint32_t);
				}
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

	void BakedChunkRenderer::renderBakedChunks(
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const WorldCamera&						   camera,
		int										   viewportWidth,
		int										   viewportHeight,
		float									   pixelsPerMeter,
		uint64_t								   frameCounter,
		InstancingUniforms&						   uniforms,
		RenderStats&							   stats
	) {
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer == nullptr) {
			return;
		}

		// Flush any pending batched geometry before drawing baked entities
		batchRenderer->flush();

		// Set viewport on BatchRenderer
		batchRenderer->setViewport(viewportWidth, viewportHeight);

		// Calculate visible world bounds for sub-chunk culling
		float zoom = camera.zoom();
		float camX = camera.position().x;
		float camY = camera.position().y;
		float scale = pixelsPerMeter * zoom;
		float viewWorldW = static_cast<float>(viewportWidth) / scale;
		float viewWorldH = static_cast<float>(viewportHeight) / scale;

		// Visible world bounds with small margin for entities on edges
		constexpr float kMargin = 2.0F;
		float			visMinX = camX - (viewWorldW * 0.5F) - kMargin;
		float			visMaxX = camX + (viewWorldW * 0.5F) + kMargin;
		float			visMinY = camY - (viewWorldH * 0.5F) - kMargin;
		float			visMaxY = camY + (viewWorldH * 0.5F) + kMargin;

		// Save GL state before modifying
		GLboolean blendEnabled = glIsEnabled(GL_BLEND);
		GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
		GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);

		// Enable blending for transparency
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		// Use the shader
		GLuint shaderProgram = batchRenderer->getShaderProgram();
		glUseProgram(shaderProgram);

		// Initialize cached uniform locations on first use
		uniforms.init(shaderProgram);

		// Set up projection matrix
		Foundation::Mat4 projection =
			glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);
		glUniformMatrix4fv(uniforms.projection, 1, GL_FALSE, glm::value_ptr(projection));

		// Identity transform
		Foundation::Mat4 identity(1.0F);
		glUniformMatrix4fv(uniforms.transform, 1, GL_FALSE, glm::value_ptr(identity));

		// Set baked world-space mode (u_instanced = 2)
		glUniform1i(uniforms.instanced, 2);
		glUniform2f(uniforms.cameraPosition, camX, camY);
		glUniform1f(uniforms.cameraZoom, zoom);
		glUniform1f(uniforms.pixelsPerMeter, pixelsPerMeter);
		glUniform2f(uniforms.viewportSize, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));

		// Far-zoom impostor handoff: pixels of on-screen height per meter of
		// entity height; short flora fades out below kImpostorCutoffPx because
		// the grass tile texture carries the appearance at that distance
		float pixelsPerWorldMeter = pixelsPerMeter * zoom;
		float currentAlpha = 1.0F;
		if (uniforms.bakedAlpha >= 0) {
			glUniform1f(uniforms.bakedAlpha, currentAlpha);
		}

		// Draw visible sub-chunks from each cached chunk
		for (const auto& coord : processedChunks) {
			auto cacheIt = m_bakedChunkCache.find(coord);
			if (cacheIt == m_bakedChunkCache.end()) {
				continue; // Not cached yet - will be built next frame
			}

			auto& cache = cacheIt->second;
			cache.lastAccessFrame = frameCounter; // Update LRU timestamp

			// Check each sub-chunk for visibility
			for (const auto& subChunk : cache.subChunks) {
				// AABB intersection test: is sub-chunk visible?
				if (subChunk.maxX < visMinX || subChunk.minX > visMaxX || subChunk.maxY < visMinY || subChunk.minY > visMaxY) {
					continue; // Sub-chunk is completely off-screen
				}

				for (const auto& bucket : subChunk.buckets) {
					if (bucket.indexCount == 0) {
						continue; // Empty bucket
					}

					// Cutoff with a fade band from kImpostorCutoffPx to 2x that.
					// Tall flora's maxEntityHeight keeps it drawn far past the
					// zoom where grass hands off to the tile texture.
					float screenHeightPx = bucket.maxEntityHeight * pixelsPerWorldMeter;
					float alpha = std::clamp((screenHeightPx - kImpostorCutoffPx) / kImpostorCutoffPx, 0.0F, 1.0F);
					if (alpha <= 0.0F) {
						continue;
					}
					if (alpha != currentAlpha && uniforms.bakedAlpha >= 0) {
						glUniform1f(uniforms.bakedAlpha, alpha);
						currentAlpha = alpha;
					}

					stats.entities += bucket.entityCount;
					stats.drawCalls++;
					stats.triangles += bucket.indexCount / 3;
					bucket.vao.bind();
					glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(bucket.indexCount), GL_UNSIGNED_INT, nullptr);
				}
			}
		}

		// Reset alpha so the dynamic instanced path (same shader branch) is unaffected
		if (uniforms.bakedAlpha >= 0 && currentAlpha != 1.0F) {
			glUniform1f(uniforms.bakedAlpha, 1.0F);
		}

		Renderer::GLVertexArray::unbind();

		// Restore GL state
		if (blendEnabled == GL_TRUE) {
			glEnable(GL_BLEND);
		} else {
			glDisable(GL_BLEND);
		}
		if (depthTestEnabled == GL_TRUE) {
			glEnable(GL_DEPTH_TEST);
		} else {
			glDisable(GL_DEPTH_TEST);
		}
		if (cullFaceEnabled == GL_TRUE) {
			glEnable(GL_CULL_FACE);
		} else {
			glDisable(GL_CULL_FACE);
		}
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
