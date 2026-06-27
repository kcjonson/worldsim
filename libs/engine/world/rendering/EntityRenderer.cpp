#include "EntityRenderer.h"

#include "assets/AssetRegistry.h"
#include "world/chunk/ChunkCoordinate.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <graphics/Color.h>
#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

namespace engine::world {

	// Maximum instances per mesh type for GPU instancing
	// Set high enough to handle extreme zoom-out scenarios (observed 34k+ entities)
	constexpr uint32_t kMaxInstancesPerMesh = 50000;

	// VAO attribute constants for InstancedMeshVertex (matches BatchRenderer setup)
	// sizeof(InstancedMeshVertex) = sizeof(Vec2) + sizeof(Color) = 8 + 16 = 24 bytes
	constexpr GLsizei kInstancedMeshVertexStride = 24;
	constexpr size_t  kInstancedMeshColorOffset = 8; // offsetof(InstancedMeshVertex, color)

	EntityRenderer::EntityRenderer(float pixelsPerMeter)
		: m_pixelsPerMeter(pixelsPerMeter) {}

	EntityRenderer::~EntityRenderer() {
		// Per-chunk baked GPU resources are automatically released by RAII wrappers
		// when caches are destroyed (GLVertexArray/GLBuffer destructors)
		m_bakedChunkCache.clear();

		// Release all shared GPU mesh handles (these use BatchRenderer's management)
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer != nullptr) {
			for (auto& [defName, handle] : m_meshHandles) {
				batchRenderer->releaseInstancedMesh(handle);
			}
		}
		m_meshHandles.clear();
	}

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
			renderBatched(executor, processedChunks, nullptr, camera, viewportWidth, viewportHeight);
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
			renderBatched(executor, processedChunks, &dynamicEntities, camera, viewportWidth, viewportHeight);
		}
	}

	// --- GPU Instancing Path ---

	void EntityRenderer::initUniformLocations(GLuint shaderProgram) {
		if (m_uniformLocations.initialized) {
			return;
		}
		m_uniformLocations.projection = glGetUniformLocation(shaderProgram, "u_projection");
		m_uniformLocations.transform = glGetUniformLocation(shaderProgram, "u_transform");
		m_uniformLocations.instanced = glGetUniformLocation(shaderProgram, "u_instanced");
		m_uniformLocations.cameraPosition = glGetUniformLocation(shaderProgram, "u_cameraPosition");
		m_uniformLocations.cameraZoom = glGetUniformLocation(shaderProgram, "u_cameraZoom");
		m_uniformLocations.pixelsPerMeter = glGetUniformLocation(shaderProgram, "u_pixelsPerMeter");
		m_uniformLocations.viewportSize = glGetUniformLocation(shaderProgram, "u_viewportSize");
		m_uniformLocations.bakedAlpha = glGetUniformLocation(shaderProgram, "u_bakedAlpha");
		m_uniformLocations.grassMode = glGetUniformLocation(shaderProgram, "u_grassMode");
		m_uniformLocations.grassOpenness = glGetUniformLocation(shaderProgram, "u_grassOpenness");
		m_uniformLocations.grassReach = glGetUniformLocation(shaderProgram, "u_grassReach");
		m_uniformLocations.cursorWorld = glGetUniformLocation(shaderProgram, "u_cursorWorld");
		m_uniformLocations.cursorRadius = glGetUniformLocation(shaderProgram, "u_cursorRadius");
		m_uniformLocations.cursorStrength = glGetUniformLocation(shaderProgram, "u_cursorStrength");
		m_uniformLocations.initialized = true;
	}

	Renderer::InstancedMeshHandle&
	EntityRenderer::getOrCreateMeshHandle(const std::string& defName, const renderer::TessellatedMesh* mesh) {
		auto it = m_meshHandles.find(defName);
		if (it != m_meshHandles.end()) {
			return it->second;
		}

		// Upload mesh to GPU
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer != nullptr && mesh != nullptr) {
			auto handle = batchRenderer->uploadInstancedMesh(*mesh, kMaxInstancesPerMesh);
			auto [insertedIt, _] = m_meshHandles.emplace(defName, std::move(handle));
			return insertedIt->second;
		}

		// Insert an invalid handle to avoid repeated lookup failures
		auto [insertedIt, _] = m_meshHandles.emplace(defName, Renderer::InstancedMeshHandle{});
		return insertedIt->second;
	}

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

		// --- Phase 1: Integrate baked meshes ---
		// Budgeted upload of worker-baked chunks (capped bytes per frame)
		processPendingUploads(kUploadBudgetBytesPerFrame);

		// Synchronous re-bake only for processed chunks whose baked mesh was
		// LRU-evicted and later revisited; new chunks arrive via the queue.
		for (const auto& coord : processedChunks) {
			if (m_bakedChunkCache.find(coord) == m_bakedChunkCache.end()) {
				buildBakedChunkMesh(executor, coord);
			}
		}

		// --- Phase 2: Render static entities from baked per-chunk meshes ---
		// Fast path: single glDrawElements per chunk, no instancing overhead.
		renderBakedChunks(processedChunks, camera, viewportWidth, viewportHeight);

		// --- Phase 2b: Render groundcover (grass) via GPU instancing ---
		// Groundcover entities are skipped by the baked path and drawn here as instanced
		// variant tufts, with a zoom LOD that fades to the grass tile texture when zoomed out.
		renderGroundcover(executor, processedChunks, camera, viewportWidth, viewportHeight);

		// --- Phase 3: Render dynamic entities (per-frame rebuild) ---
		// Dynamic entities (from ECS) change position each frame, so we rebuild them.
		// GL state note: BatchRenderer::drawInstanced() sets up its own GL state internally,
		// so we don't need to carry state from renderCachedChunks() here.
		if (dynamicEntities != nullptr && !dynamicEntities->empty()) {
			// Clear per-frame instance batches (keep capacity for reuse)
			for (auto& [defName, batch] : m_instanceBatches) {
				batch.clear();
			}

			float zoom = camera.zoom();
			float camX = camera.position().x;
			float camY = camera.position().y;

			// Calculate visible world bounds (in tiles/meters)
			float scale = m_pixelsPerMeter * zoom;
			float viewWorldW = static_cast<float>(viewportWidth) / scale;
			float viewWorldH = static_cast<float>(viewportHeight) / scale;

			// Visible world bounds with small margin for entities on edges
			constexpr float kMargin = 2.0F;
			float			minWorldX = camX - (viewWorldW * 0.5F) - kMargin;
			float			maxWorldX = camX + (viewWorldW * 0.5F) + kMargin;
			float			minWorldY = camY - (viewWorldH * 0.5F) - kMargin;
			float			maxWorldY = camY + (viewWorldH * 0.5F) + kMargin;

			for (const auto& entity : *dynamicEntities) {
				// Frustum culling for dynamic entities
				if (entity.position.x < minWorldX || entity.position.x > maxWorldX || entity.position.y < minWorldY ||
					entity.position.y > maxWorldY) {
					continue;
				}

				// Get or create mesh handle for this asset type
				const auto* templateMesh = getTemplate(entity.defName);
				if (templateMesh == nullptr) {
					continue;
				}

				// Ensure mesh handle exists
				auto& handle = getOrCreateMeshHandle(entity.defName, templateMesh);
				if (!handle.isValid()) {
					continue;
				}

				// Create instance data (world-space - GPU does the transform!)
				Renderer::InstanceData instance(
					Foundation::Vec2(entity.position.x, entity.position.y), entity.rotation, entity.scale, entity.colorTint
				);

				// Add to batch for this mesh type
				m_instanceBatches[entity.defName].push_back(instance);
				m_lastEntityCount++;
			}

			// Draw dynamic entities with per-frame upload
			auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
			if (batchRenderer != nullptr) {
				Foundation::Vec2 cameraPos(camX, camY);

				for (auto& [defName, instances] : m_instanceBatches) {
					if (instances.empty()) {
						continue;
					}

					auto handleIt = m_meshHandles.find(defName);
					if (handleIt == m_meshHandles.end() || !handleIt->second.isValid()) {
						continue;
					}

					// Stats note: drawInstanced increments BatchRenderer's own counters,
					// so these draws are NOT added to m_lastDrawCallCount (avoids double count)
					batchRenderer->drawInstanced(
						handleIt->second, instances.data(), static_cast<uint32_t>(instances.size()), cameraPos, zoom, m_pixelsPerMeter
					);
				}
			}
		}

		// --- Phase 4: LRU cache eviction ---
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

	// --- CPU Batching Path (Original Implementation) ---

	void EntityRenderer::renderBatched(
		const assets::PlacementExecutor&		   executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>*   dynamicEntities,
		const WorldCamera&						   camera,
		int										   viewportWidth,
		int										   viewportHeight
	) {
		m_lastEntityCount = 0;
		m_lastDrawCallCount = 0;
		m_lastTriangleCount = 0;

		// Clear per-frame buffers (keep capacity for reuse)
		m_vertices.clear();
		m_colors.clear();
		m_indices.clear();

		// Reserve capacity based on typical entity counts to avoid reallocations
		// ~10k entities × ~15 vertices each = ~150k vertices typical
		constexpr size_t kExpectedVertices = 150000;
		constexpr size_t kExpectedIndices = 200000;
		if (m_vertices.capacity() < kExpectedVertices) {
			m_vertices.reserve(kExpectedVertices);
			m_colors.reserve(kExpectedVertices);
			m_indices.reserve(kExpectedIndices);
		}

		float halfViewW = static_cast<float>(viewportWidth) * 0.5F;
		float halfViewH = static_cast<float>(viewportHeight) * 0.5F;
		float zoom = camera.zoom();
		float scale = m_pixelsPerMeter * zoom;
		float camX = camera.position().x;
		float camY = camera.position().y;

		// Calculate visible world bounds (in tiles/meters)
		float viewWorldW = static_cast<float>(viewportWidth) / scale;
		float viewWorldH = static_cast<float>(viewportHeight) / scale;

		// Visible world bounds with small margin for entities on edges
		constexpr float kMargin = 2.0F;
		float			minWorldX = camX - (viewWorldW * 0.5F) - kMargin;
		float			maxWorldX = camX + (viewWorldW * 0.5F) + kMargin;
		float			minWorldY = camY - (viewWorldH * 0.5F) - kMargin;
		float			maxWorldY = camY + (viewWorldH * 0.5F) + kMargin;

		uint32_t vertexIndex = 0;

		// Process each chunk, query only visible entities
		for (const auto& coord : processedChunks) {
			const auto* index = executor.getChunkIndex(coord);
			if (index == nullptr) {
				continue;
			}

			// Query only entities within visible bounds (view frustum culling)
			auto visibleEntities = index->queryRect(minWorldX, minWorldY, maxWorldX, maxWorldY);

			for (const auto* entity : visibleEntities) {
				const auto* templateMesh = getTemplate(entity->defName);
				if (templateMesh == nullptr) {
					continue;
				}

				// Pre-compute entity transform factors
				float entityScale = entity->scale;
				float posX = entity->position.x;
				float posY = entity->position.y;
				bool  hasMeshColors = templateMesh->hasColors();

				// Check if we need rotation
				constexpr float kRotationEpsilon = 0.0001F;
				bool			noRotation = std::abs(entity->rotation) < kRotationEpsilon;

				if (noRotation) {
					// Optimized path: scale + translate only
					for (size_t i = 0; i < templateMesh->vertices.size(); ++i) {
						const auto& v = templateMesh->vertices[i];

						// Transform mesh vertex to world position, then to screen
						float worldX = v.x * entityScale + posX;
						float worldY = v.y * entityScale + posY;
						float screenX = (worldX - camX) * scale + halfViewW;
						float screenY = (worldY - camY) * scale + halfViewH;

						m_vertices.emplace_back(screenX, screenY);

						// Handle colors
						if (hasMeshColors) {
							const auto& meshColor = templateMesh->colors[i];
							m_colors.emplace_back(
								meshColor.r * entity->colorTint.r,
								meshColor.g * entity->colorTint.g,
								meshColor.b * entity->colorTint.b,
								meshColor.a * entity->colorTint.a
							);
						} else {
							m_colors.emplace_back(entity->colorTint.r, entity->colorTint.g, entity->colorTint.b, entity->colorTint.a);
						}
					}
				} else {
					// Full transform with rotation
					float cosR = std::cos(entity->rotation);
					float sinR = std::sin(entity->rotation);

					for (size_t i = 0; i < templateMesh->vertices.size(); ++i) {
						const auto& v = templateMesh->vertices[i];

						// Scale
						float sx = v.x * entityScale;
						float sy = v.y * entityScale;

						// Rotate + translate to world, then to screen
						float worldX = sx * cosR - sy * sinR + posX;
						float worldY = sx * sinR + sy * cosR + posY;
						float screenX = (worldX - camX) * scale + halfViewW;
						float screenY = (worldY - camY) * scale + halfViewH;

						m_vertices.emplace_back(screenX, screenY);

						// Handle colors
						if (hasMeshColors) {
							const auto& meshColor = templateMesh->colors[i];
							m_colors.emplace_back(
								meshColor.r * entity->colorTint.r,
								meshColor.g * entity->colorTint.g,
								meshColor.b * entity->colorTint.b,
								meshColor.a * entity->colorTint.a
							);
						} else {
							m_colors.emplace_back(entity->colorTint.r, entity->colorTint.g, entity->colorTint.b, entity->colorTint.a);
						}
					}
				}

				// Add indices (offset by current vertex count)
				for (const auto& idx : templateMesh->indices) {
					m_indices.push_back(static_cast<uint16_t>(vertexIndex + idx));
				}

				vertexIndex += static_cast<uint32_t>(templateMesh->vertices.size());
				m_lastEntityCount++;
			}
		}

		// Process dynamic entities (from ECS)
		if (dynamicEntities != nullptr) {
			for (const auto& entity : *dynamicEntities) {
				// Frustum culling for dynamic entities
				if (entity.position.x < minWorldX || entity.position.x > maxWorldX || entity.position.y < minWorldY ||
					entity.position.y > maxWorldY) {
					continue;
				}

				const auto* templateMesh = getTemplate(entity.defName);
				if (templateMesh == nullptr) {
					continue;
				}

				// Pre-compute entity transform factors
				float entityScale = entity.scale;
				float posX = entity.position.x;
				float posY = entity.position.y;
				bool  hasMeshColors = templateMesh->hasColors();

				// Check if we need rotation
				constexpr float kRotationEpsilon = 0.0001F;
				bool			noRotation = std::abs(entity.rotation) < kRotationEpsilon;

				if (noRotation) {
					// Optimized path: scale + translate only
					for (size_t i = 0; i < templateMesh->vertices.size(); ++i) {
						const auto& v = templateMesh->vertices[i];

						// Transform mesh vertex to world position, then to screen
						float worldX = v.x * entityScale + posX;
						float worldY = v.y * entityScale + posY;
						float screenX = (worldX - camX) * scale + halfViewW;
						float screenY = (worldY - camY) * scale + halfViewH;

						m_vertices.emplace_back(screenX, screenY);

						// Handle colors
						if (hasMeshColors) {
							const auto& meshColor = templateMesh->colors[i];
							m_colors.emplace_back(
								meshColor.r * entity.colorTint.r,
								meshColor.g * entity.colorTint.g,
								meshColor.b * entity.colorTint.b,
								meshColor.a * entity.colorTint.a
							);
						} else {
							m_colors.emplace_back(entity.colorTint.r, entity.colorTint.g, entity.colorTint.b, entity.colorTint.a);
						}
					}
				} else {
					// Full transform with rotation
					float cosR = std::cos(entity.rotation);
					float sinR = std::sin(entity.rotation);

					for (size_t i = 0; i < templateMesh->vertices.size(); ++i) {
						const auto& v = templateMesh->vertices[i];

						// Scale
						float sx = v.x * entityScale;
						float sy = v.y * entityScale;

						// Rotate + translate to world, then to screen
						float worldX = sx * cosR - sy * sinR + posX;
						float worldY = sx * sinR + sy * cosR + posY;
						float screenX = (worldX - camX) * scale + halfViewW;
						float screenY = (worldY - camY) * scale + halfViewH;

						m_vertices.emplace_back(screenX, screenY);

						// Handle colors
						if (hasMeshColors) {
							const auto& meshColor = templateMesh->colors[i];
							m_colors.emplace_back(
								meshColor.r * entity.colorTint.r,
								meshColor.g * entity.colorTint.g,
								meshColor.b * entity.colorTint.b,
								meshColor.a * entity.colorTint.a
							);
						} else {
							m_colors.emplace_back(entity.colorTint.r, entity.colorTint.g, entity.colorTint.b, entity.colorTint.a);
						}
					}
				}

				// Add indices (offset by current vertex count)
				for (const auto& idx : templateMesh->indices) {
					m_indices.push_back(static_cast<uint16_t>(vertexIndex + idx));
				}

				vertexIndex += static_cast<uint32_t>(templateMesh->vertices.size());
				m_lastEntityCount++;
			}
		}

		// Single draw call for all entities
		if (!m_indices.empty()) {
			Renderer::Primitives::drawTriangles(
				Renderer::Primitives::TrianglesArgs{
					.vertices = m_vertices.data(),
					.indices = m_indices.data(),
					.vertexCount = m_vertices.size(),
					.indexCount = m_indices.size(),
					.colors = m_colors.data()
				}
			);
		}
	}

	const renderer::TessellatedMesh* EntityRenderer::getTemplate(const std::string& defName) {
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

	// --- Baked Static Mesh Implementation with Sub-Chunk Culling ---
	// CPU bake (vertex transform) lives in BakedEntityMesh.cpp so it can run on
	// worker threads; this file owns only the GL upload and rendering.

	void EntityRenderer::buildBakedChunkMesh(const assets::PlacementExecutor& executor, const ChunkCoordinate& coord) {
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
		uploadBakedChunk(coord, std::move(cpuData));
	}

	size_t EntityRenderer::uploadSubChunk(BakedChunkData& bakedData, BakedSubChunkCPUData& cpu, int subIndex) {
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

	void EntityRenderer::uploadBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData) {
		BakedChunkData bakedData;
		bakedData.lastAccessFrame = frameCounter;
		bakedData.totalEntityCount = cpuData.totalEntityCount;

		for (int subIndex = 0; subIndex < kSubChunkCount; ++subIndex) {
			uploadSubChunk(bakedData, cpuData.subChunks[subIndex], subIndex);
		}

		m_bakedChunkCache[coord] = std::move(bakedData);
	}

	void EntityRenderer::queueBakedChunk(const ChunkCoordinate& coord, BakedChunkCPUData&& cpuData) {
		// Create the cache entry up front (bounds valid, buffers filled over the
		// next few frames); renderBakedChunks skips sub-chunks with indexCount 0
		BakedChunkData bakedData;
		bakedData.lastAccessFrame = frameCounter;
		bakedData.totalEntityCount = cpuData.totalEntityCount;
		m_bakedChunkCache[coord] = std::move(bakedData);

		m_pendingUploads.push_back(PendingUpload{coord, std::move(cpuData), 0});
	}

	void EntityRenderer::processPendingUploads(size_t budgetBytes) {
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

	void EntityRenderer::releaseBakedChunkCache(const ChunkCoordinate& coord) {
		// RAII wrappers automatically release GPU resources when destroyed
		m_bakedChunkCache.erase(coord);
	}

	void EntityRenderer::renderBakedChunks(
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const WorldCamera&						   camera,
		int										   viewportWidth,
		int										   viewportHeight
	) {
		m_lastEntityCount = 0;
		m_lastDrawCallCount = 0;
		m_lastTriangleCount = 0;

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
		float scale = m_pixelsPerMeter * zoom;
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
		initUniformLocations(shaderProgram);

		// Set up projection matrix
		Foundation::Mat4 projection =
			glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);
		glUniformMatrix4fv(m_uniformLocations.projection, 1, GL_FALSE, glm::value_ptr(projection));

		// Identity transform
		Foundation::Mat4 identity(1.0F);
		glUniformMatrix4fv(m_uniformLocations.transform, 1, GL_FALSE, glm::value_ptr(identity));

		// Set baked world-space mode (u_instanced = 2)
		glUniform1i(m_uniformLocations.instanced, 2);
		glUniform2f(m_uniformLocations.cameraPosition, camX, camY);
		glUniform1f(m_uniformLocations.cameraZoom, zoom);
		glUniform1f(m_uniformLocations.pixelsPerMeter, m_pixelsPerMeter);
		glUniform2f(m_uniformLocations.viewportSize, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));

		// Far-zoom impostor handoff: pixels of on-screen height per meter of
		// entity height; short flora fades out below kImpostorCutoffPx because
		// the grass tile texture carries the appearance at that distance
		float pixelsPerWorldMeter = m_pixelsPerMeter * zoom;
		float currentAlpha = 1.0F;
		if (m_uniformLocations.bakedAlpha >= 0) {
			glUniform1f(m_uniformLocations.bakedAlpha, currentAlpha);
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
					if (alpha != currentAlpha && m_uniformLocations.bakedAlpha >= 0) {
						glUniform1f(m_uniformLocations.bakedAlpha, alpha);
						currentAlpha = alpha;
					}

					m_lastEntityCount += bucket.entityCount;
					m_lastDrawCallCount++;
					m_lastTriangleCount += bucket.indexCount / 3;
					bucket.vao.bind();
					glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(bucket.indexCount), GL_UNSIGNED_INT, nullptr);
				}
			}
		}

		// Reset alpha so the dynamic instanced path (same shader branch) is unaffected
		if (m_uniformLocations.bakedAlpha >= 0 && currentAlpha != 1.0F) {
			glUniform1f(m_uniformLocations.bakedAlpha, 1.0F);
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

// --- Groundcover (grass) GPU-instanced path ---

namespace {
	// Tunable groundcover constants.
	constexpr float	   kGroundcoverHeightM = 0.5F;	  // blade height used for the zoom LOD
	constexpr float	   kGroundcoverLodCutoffPx = 6.0F; // fade out below this on-screen height (px)
	constexpr float	   kGroundcoverReachM = 0.55F;	  // max tuft reach (deform; flat at rest)
	constexpr uint32_t kGroundcoverMaxInstancesPerVariant = 100000;
} // namespace

const std::vector<Renderer::InstancedMeshHandle>& EntityRenderer::ensureGroundcoverVariants(const std::string& defName) {
	auto it = m_groundcoverHandles.find(defName);
	if (it != m_groundcoverHandles.end()) {
		return it->second;
	}

	// Generate the asset's variant tufts via the asset system (one buildMesh per seed) so the
	// look lives entirely in the asset (grass.lua + params), not here.
	std::vector<Renderer::InstancedMeshHandle> handles;
	auto*									   batchRenderer = Renderer::Primitives::getBatchRenderer();
	auto&									   registry = assets::AssetRegistry::Get();
	const auto*								   def = registry.getDefinition(defName);
	if (batchRenderer != nullptr && def != nullptr) {
		const uint32_t variantCount = std::max<uint32_t>(1, def->variantCount);
		handles.reserve(variantCount);
		for (uint32_t seed = 0; seed < variantCount; ++seed) {
			renderer::TessellatedMesh mesh;
			if (registry.buildMesh(defName, seed, mesh) && !mesh.vertices.empty()) {
				handles.push_back(batchRenderer->uploadInstancedMesh(mesh, kGroundcoverMaxInstancesPerVariant));
			} else {
				handles.emplace_back(); // invalid placeholder keeps variant indices aligned
			}
		}
	}
	auto [insIt, _] = m_groundcoverHandles.emplace(defName, std::move(handles));
	return insIt->second;
}

void EntityRenderer::buildGroundcoverChunk(
	const assets::PlacementExecutor& executor, const ChunkCoordinate& coord, GroundcoverChunkCache& cache
) {
	cache.byDef.clear();
	cache.built = true;

	WorldPosition	chunkOrigin = coord.origin();
	constexpr float kChunkWorldSize = static_cast<float>(kChunkSize) * kTileSize;
	cache.minX = chunkOrigin.x;
	cache.minY = chunkOrigin.y;
	cache.maxX = chunkOrigin.x + kChunkWorldSize;
	cache.maxY = chunkOrigin.y + kChunkWorldSize;

	const auto* index = executor.getChunkIndex(coord);
	if (index == nullptr) {
		return;
	}

	auto& registry = assets::AssetRegistry::Get();
	auto  entities = index->queryRect(cache.minX, cache.minY, cache.maxX, cache.maxY);
	for (const auto* e : entities) {
		const auto* def = registry.getDefinition(e->defName);
		if (def == nullptr || def->role != assets::AssetRole::Groundcover) {
			continue;
		}
		const uint32_t variantCount = std::max<uint32_t>(1, def->variantCount);
		auto&		   buckets = cache.byDef[e->defName];
		if (buckets.size() != variantCount) {
			buckets.resize(variantCount);
		}
		// Deterministic variant from quantized world position (chunk re-builds identically).
		auto	 hx = static_cast<uint32_t>(static_cast<int32_t>(std::floor(e->position.x * 4.0F)));
		auto	 hy = static_cast<uint32_t>(static_cast<int32_t>(std::floor(e->position.y * 4.0F)));
		uint32_t v = ((hx * 73856093u) ^ (hy * 19349663u)) % variantCount;
		buckets[v].emplace_back(
			Foundation::Vec2(e->position.x, e->position.y), e->rotation, e->scale, Foundation::Color(e->colorTint)
		);
	}
}

void EntityRenderer::renderGroundcover(
	const assets::PlacementExecutor&		   executor,
	const std::unordered_set<ChunkCoordinate>& processedChunks,
	const WorldCamera&						   camera,
	int										   viewportWidth,
	int										   viewportHeight
) {
	auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
	if (batchRenderer == nullptr) {
		return;
	}

	const float zoom = camera.zoom();
	const float ppm = m_pixelsPerMeter;

	// Zoom LOD: a tuft is kGroundcoverHeightM tall; below kGroundcoverLodCutoffPx on screen
	// we skip the geometry entirely and let the grass tile texture carry it. Tunable.
	const float bladeScreenPx = kGroundcoverHeightM * ppm * zoom;
	const float lodAlpha = std::clamp((bladeScreenPx - kGroundcoverLodCutoffPx) / kGroundcoverLodCutoffPx, 0.0F, 1.0F);
	if (lodAlpha <= 0.0F) {
		return;
	}

	const float camX = camera.position().x;
	const float camY = camera.position().y;
	const float scale = ppm * zoom;
	const float viewWorldW = static_cast<float>(viewportWidth) / scale;
	const float viewWorldH = static_cast<float>(viewportHeight) / scale;
	constexpr float kMargin = 2.0F;
	const float		visMinX = camX - (viewWorldW * 0.5F) - kMargin;
	const float		visMaxX = camX + (viewWorldW * 0.5F) + kMargin;
	const float		visMinY = camY - (viewWorldH * 0.5F) - kMargin;
	const float		visMaxY = camY + (viewWorldH * 0.5F) + kMargin;

	GLuint shaderProgram = batchRenderer->getShaderProgram();
	glUseProgram(shaderProgram);
	initUniformLocations(shaderProgram);
	if (m_uniformLocations.grassMode >= 0) {
		glUniform1i(m_uniformLocations.grassMode, 1);
	}
	if (m_uniformLocations.grassOpenness >= 0) {
		glUniform1f(m_uniformLocations.grassOpenness, 1.0F);
	}
	if (m_uniformLocations.grassReach >= 0) {
		glUniform1f(m_uniformLocations.grassReach, kGroundcoverReachM);
	}
	if (m_uniformLocations.cursorRadius >= 0) {
		glUniform1f(m_uniformLocations.cursorRadius, 0.0F); // no interaction deform yet
	}
	if (m_uniformLocations.bakedAlpha >= 0) {
		glUniform1f(m_uniformLocations.bakedAlpha, lodAlpha);
	}

	const Foundation::Vec2 cameraPos(camX, camY);
	for (const auto& coord : processedChunks) {
		auto& cache = m_groundcoverChunkCache[coord];
		cache.lastAccessFrame = frameCounter;
		if (!cache.built) {
			buildGroundcoverChunk(executor, coord, cache);
		}
		if (cache.maxX < visMinX || cache.minX > visMaxX || cache.maxY < visMinY || cache.minY > visMaxY) {
			continue;
		}
		for (auto& [defName, buckets] : cache.byDef) {
			const auto&	 handles = ensureGroundcoverVariants(defName);
			const size_t count = std::min(buckets.size(), handles.size());
			for (size_t v = 0; v < count; ++v) {
				auto& inst = buckets[v];
				if (inst.empty() || !handles[v].isValid()) {
					continue;
				}
				batchRenderer->drawInstanced(
					handles[v], inst.data(), static_cast<uint32_t>(inst.size()), cameraPos, zoom, ppm
				);
				m_lastEntityCount += static_cast<uint32_t>(inst.size());
			}
		}
	}

	// Restore normal (non-grass) instancing state for the rest of the frame.
	if (m_uniformLocations.grassMode >= 0) {
		glUniform1i(m_uniformLocations.grassMode, 0);
	}
	if (m_uniformLocations.bakedAlpha >= 0) {
		glUniform1f(m_uniformLocations.bakedAlpha, 1.0F);
	}
}

} // namespace engine::world
