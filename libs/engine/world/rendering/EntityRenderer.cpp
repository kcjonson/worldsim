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

		// --- Phase 1: Build baked mesh for any uncached chunks ---
		// This happens once per chunk, then the baked mesh is reused every frame.
		for (const auto& coord : processedChunks) {
			if (m_bakedChunkCache.find(coord) == m_bakedChunkCache.end()) {
				buildBakedChunkMesh(executor, coord);
			}
		}

		// --- Phase 2: Render static entities from baked per-chunk meshes ---
		// Fast path: single glDrawElements per chunk, no instancing overhead.
		renderBakedChunks(processedChunks, camera, viewportWidth, viewportHeight);

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

	// Per-vertex data: position (Vec2) + color (Color) = 24 bytes
	struct BakedVertex {
		Foundation::Vec2  position; // World-space position
		Foundation::Color color;	// Pre-tinted color
	};
	static_assert(sizeof(BakedVertex) == 24, "BakedVertex must be 24 bytes");

	void EntityRenderer::buildBakedChunkMesh(const assets::PlacementExecutor& executor, const ChunkCoordinate& coord) {
		// Get chunk index from executor
		const auto* index = executor.getChunkIndex(coord);
		if (index == nullptr) {
			return;
		}

		WorldPosition  chunkOrigin = coord.origin();
		BakedChunkData bakedData;
		bakedData.lastAccessFrame = frameCounter;
		uint32_t totalEntityCount = 0;

		// Build each sub-chunk separately for view frustum culling
		for (int subY = 0; subY < kSubChunkGridSize; ++subY) {
			for (int subX = 0; subX < kSubChunkGridSize; ++subX) {
				int	  subIndex = subY * kSubChunkGridSize + subX;
				auto& subChunk = bakedData.subChunks[subIndex];

				// Calculate sub-chunk world bounds
				float subMinX = chunkOrigin.x + static_cast<float>(subX) * kSubChunkWorldSize;
				float subMinY = chunkOrigin.y + static_cast<float>(subY) * kSubChunkWorldSize;
				float subMaxX = subMinX + kSubChunkWorldSize;
				float subMaxY = subMinY + kSubChunkWorldSize;

				// Store bounds for culling
				subChunk.minX = subMinX;
				subChunk.minY = subMinY;
				subChunk.maxX = subMaxX;
				subChunk.maxY = subMaxY;

				// Query entities in this sub-region
				auto entities = index->queryRect(subMinX, subMinY, subMaxX, subMaxY);
				if (entities.empty()) {
					subChunk.indexCount = 0;
					subChunk.entityCount = 0;
					continue;
				}

				// Build vertex/index data for this sub-chunk
				std::vector<BakedVertex> vertices;
				std::vector<uint32_t>	 indices;
				vertices.reserve(entities.size() * 8);
				indices.reserve(entities.size() * 12);

				uint32_t vertexOffset = 0;

				for (const auto* entity : entities) {
					const auto* templateMesh = getTemplate(entity->defName);
					if (templateMesh == nullptr) {
						continue;
					}

					// Pre-compute transform
					float entityScale = entity->scale;
					float posX = entity->position.x;
					float posY = entity->position.y;
					bool  hasMeshColors = templateMesh->hasColors();

					// Check if we need rotation
					constexpr float kRotationEpsilon = 0.0001F;
					bool			noRotation = std::abs(entity->rotation) < kRotationEpsilon;

					float cosR = 1.0F;
					float sinR = 0.0F;
					if (!noRotation) {
						cosR = std::cos(entity->rotation);
						sinR = std::sin(entity->rotation);
					}

					// Transform and add vertices
					for (size_t i = 0; i < templateMesh->vertices.size(); ++i) {
						const auto& v = templateMesh->vertices[i];
						BakedVertex baked;

						// Scale
						float sx = v.x * entityScale;
						float sy = v.y * entityScale;

						// Rotate + translate to world position
						if (noRotation) {
							baked.position.x = sx + posX;
							baked.position.y = sy + posY;
						} else {
							baked.position.x = sx * cosR - sy * sinR + posX;
							baked.position.y = sx * sinR + sy * cosR + posY;
						}

						// Apply color tint
						if (hasMeshColors) {
							const auto& meshColor = templateMesh->colors[i];
							baked.color = Foundation::Color(
								meshColor.r * entity->colorTint.r,
								meshColor.g * entity->colorTint.g,
								meshColor.b * entity->colorTint.b,
								meshColor.a * entity->colorTint.a
							);
						} else {
							baked.color = Foundation::Color(entity->colorTint);
						}

						vertices.push_back(baked);
					}

					// Add indices (offset by current vertex count)
					for (const auto& idx : templateMesh->indices) {
						indices.push_back(vertexOffset + idx);
					}

					vertexOffset += static_cast<uint32_t>(templateMesh->vertices.size());
					subChunk.entityCount++;
				}

				if (vertices.empty()) {
					subChunk.indexCount = 0;
					continue;
				}

				// Create GPU resources for this sub-chunk
				subChunk.indexCount = static_cast<uint32_t>(indices.size());
				totalEntityCount += subChunk.entityCount;

				// Create VAO
				subChunk.vao = Renderer::GLVertexArray::create();
				subChunk.vao.bind();

				// Create and upload vertex buffer
				subChunk.vertexVBO = Renderer::GLBuffer(
					GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(BakedVertex)), vertices.data(), GL_STATIC_DRAW
				);

				// Set up vertex attributes
				glEnableVertexAttribArray(0);
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(BakedVertex), reinterpret_cast<void*>(0));
				glEnableVertexAttribArray(2);
				glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(BakedVertex), reinterpret_cast<void*>(offsetof(BakedVertex, color)));

				// Create and upload index buffer
				subChunk.indexIBO = Renderer::GLBuffer(
					GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW
				);

				Renderer::GLVertexArray::unbind();
			}
		}

		bakedData.totalEntityCount = totalEntityCount;
		m_bakedChunkCache[coord] = std::move(bakedData);
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
				if (subChunk.indexCount == 0) {
					continue; // Empty sub-chunk
				}

				// AABB intersection test: is sub-chunk visible?
				if (subChunk.maxX < visMinX || subChunk.minX > visMaxX || subChunk.maxY < visMinY || subChunk.minY > visMaxY) {
					continue; // Sub-chunk is completely off-screen
				}

				// Sub-chunk is visible - draw it
				m_lastEntityCount += subChunk.entityCount;
				subChunk.vao.bind();
				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(subChunk.indexCount), GL_UNSIGNED_INT, nullptr);
			}
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

} // namespace engine::world
