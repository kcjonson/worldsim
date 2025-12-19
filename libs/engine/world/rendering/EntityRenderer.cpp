#include "EntityRenderer.h"

#include "assets/AssetRegistry.h"
#include "world/chunk/ChunkCoordinate.h"

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
		// Release all per-chunk cached GPU resources
		for (auto& [coord, cache] : m_chunkInstanceCache) {
			for (auto& [defName, meshData] : cache.meshes) {
				if (meshData.vao != 0) {
					glDeleteVertexArrays(1, &meshData.vao);
				}
				if (meshData.instanceVBO != 0) {
					glDeleteBuffers(1, &meshData.instanceVBO);
				}
			}
		}
		m_chunkInstanceCache.clear();

		// Release all shared GPU mesh handles
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
			auto [insertedIt, _] = m_meshHandles.emplace(defName, handle);
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
		// --- Phase 1: Build cache for any uncached chunks ---
		// This happens once per chunk, then the cache is reused every frame.
		for (const auto& coord : processedChunks) {
			if (m_chunkInstanceCache.find(coord) == m_chunkInstanceCache.end()) {
				buildChunkCache(executor, coord);
			}
		}

		// --- Phase 2: Render static entities from cached per-chunk VAOs ---
		// This is the fast path: just bind VAO and draw, no CPU→GPU upload.
		renderCachedChunks(processedChunks, camera, viewportWidth, viewportHeight);

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

		// --- Phase 4: Cache eviction for chunks that are no longer visible ---
		// Release GPU resources for chunks that weren't rendered this frame.
		// Collect coordinates to evict first (can't modify map while iterating)
		std::vector<ChunkCoordinate> toEvict;
		for (const auto& [coord, cache] : m_chunkInstanceCache) {
			if (processedChunks.find(coord) == processedChunks.end()) {
				toEvict.push_back(coord);
			}
		}
		for (const auto& coord : toEvict) {
			releaseChunkCache(coord);
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

	// --- Per-Chunk Instance Caching ---

	void EntityRenderer::buildChunkCache(const assets::PlacementExecutor& executor, const ChunkCoordinate& coord) {
		// Get chunk index from executor
		const auto* index = executor.getChunkIndex(coord);
		if (index == nullptr) {
			return;
		}

		// Get chunk world bounds and query ALL entities in this chunk (no frustum culling)
		WorldPosition chunkOrigin = coord.origin();
		float		  chunkSize = static_cast<float>(kChunkSize) * kTileSize;
		float		  minX = chunkOrigin.x;
		float		  minY = chunkOrigin.y;
		float		  maxX = chunkOrigin.x + chunkSize;
		float		  maxY = chunkOrigin.y + chunkSize;
		auto		  allEntities = index->queryRect(minX, minY, maxX, maxY);
		if (allEntities.empty()) {
			return;
		}

		ChunkInstanceCache cache;
		uint32_t		   actualEntityCount = 0;

		// Group entities by mesh type
		std::unordered_map<std::string, std::vector<Renderer::InstanceData>> instancesByMesh;

		for (const auto* entity : allEntities) {
			// Ensure mesh handle exists (we need the shared VBO/IBO)
			const auto* templateMesh = getTemplate(entity->defName);
			if (templateMesh == nullptr) {
				continue;
			}
			auto& handle = getOrCreateMeshHandle(entity->defName, templateMesh);
			if (!handle.isValid()) {
				continue;
			}

			// Create instance data
			Renderer::InstanceData instance(
				Foundation::Vec2(entity->position.x, entity->position.y), entity->rotation, entity->scale, entity->colorTint
			);
			instancesByMesh[entity->defName].push_back(instance);
			actualEntityCount++;
		}

		// Create per-chunk VAOs for each mesh type
		for (auto& [defName, instances] : instancesByMesh) {
			if (instances.empty()) {
				continue;
			}

			auto handleIt = m_meshHandles.find(defName);
			if (handleIt == m_meshHandles.end() || !handleIt->second.isValid()) {
				continue;
			}
			const auto& sharedHandle = handleIt->second;

			CachedMeshData meshData;
			meshData.instanceCount = static_cast<uint32_t>(instances.size());
			meshData.indexCount = sharedHandle.indexCount;

			// Create new VAO for this chunk's instances
			glGenVertexArrays(1, &meshData.vao);
			glBindVertexArray(meshData.vao);

			// Bind SHARED mesh VBO and set up mesh attributes (same as in BatchRenderer).
			// Attribute locations 1, 3, 4, 5 are skipped because they're used by UberVertex
			// for text/shape data (texCoord, data1, data2, clipBounds) which instanced
			// entities don't need. The uber shader ignores them when u_instanced = 1.
			glBindBuffer(GL_ARRAY_BUFFER, sharedHandle.meshVBO);

			// Location 0: position (vec2) - from InstancedMeshVertex
			glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, kInstancedMeshVertexStride, reinterpret_cast<void*>(0));

			// Location 2: color (vec4) - from InstancedMeshVertex
			glEnableVertexAttribArray(2);
			glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kInstancedMeshVertexStride, reinterpret_cast<void*>(kInstancedMeshColorOffset));

			// Bind SHARED mesh IBO
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sharedHandle.meshIBO);

			// Create NEW instance VBO for this chunk (with actual data, GL_STATIC_DRAW)
			glGenBuffers(1, &meshData.instanceVBO);
			glBindBuffer(GL_ARRAY_BUFFER, meshData.instanceVBO);
			glBufferData(
				GL_ARRAY_BUFFER,
				static_cast<GLsizeiptr>(instances.size() * sizeof(Renderer::InstanceData)),
				instances.data(),
				GL_STATIC_DRAW // Key: static - data won't change
			);

			// Set up instance attributes with divisor = 1
			// Location 6: instanceData1 (worldPos.xy, rotation, scale)
			glEnableVertexAttribArray(6);
			glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Renderer::InstanceData), reinterpret_cast<void*>(0));
			glVertexAttribDivisor(6, 1);

			// Location 7: instanceData2 (colorTint.rgba)
			glEnableVertexAttribArray(7);
			glVertexAttribPointer(
				7,
				4,
				GL_FLOAT,
				GL_FALSE,
				sizeof(Renderer::InstanceData),
				reinterpret_cast<void*>(offsetof(Renderer::InstanceData, colorTint))
			);
			glVertexAttribDivisor(7, 1);

			glBindVertexArray(0);

			cache.meshes[defName] = meshData;
		}

		cache.totalEntityCount = actualEntityCount;
		m_chunkInstanceCache[coord] = std::move(cache);
	}

	void EntityRenderer::releaseChunkCache(const ChunkCoordinate& coord) {
		auto it = m_chunkInstanceCache.find(coord);
		if (it == m_chunkInstanceCache.end()) {
			return;
		}

		// Release GPU resources
		for (auto& [defName, meshData] : it->second.meshes) {
			if (meshData.vao != 0) {
				glDeleteVertexArrays(1, &meshData.vao);
			}
			if (meshData.instanceVBO != 0) {
				glDeleteBuffers(1, &meshData.instanceVBO);
			}
		}

		m_chunkInstanceCache.erase(it);
	}

	void EntityRenderer::renderCachedChunks(
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

		// Flush any pending batched geometry before drawing instanced entities
		batchRenderer->flush();

		// Set viewport on BatchRenderer
		batchRenderer->setViewport(viewportWidth, viewportHeight);

		// Save GL state before modifying
		GLboolean blendEnabled = glIsEnabled(GL_BLEND);
		GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
		GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);

		// Enable blending for transparency
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		// Use the shader (same as drawInstanced)
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

		// Set instancing uniforms
		glUniform1i(m_uniformLocations.instanced, 1);
		glUniform2f(m_uniformLocations.cameraPosition, camera.position().x, camera.position().y);
		glUniform1f(m_uniformLocations.cameraZoom, camera.zoom());
		glUniform1f(m_uniformLocations.pixelsPerMeter, m_pixelsPerMeter);
		glUniform2f(m_uniformLocations.viewportSize, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));

		// Draw each cached chunk
		for (const auto& coord : processedChunks) {
			auto cacheIt = m_chunkInstanceCache.find(coord);
			if (cacheIt == m_chunkInstanceCache.end()) {
				continue; // Not cached yet - will be built next frame
			}

			const auto& cache = cacheIt->second;
			m_lastEntityCount += cache.totalEntityCount;

			// Draw each mesh type in this chunk
			for (const auto& [defName, meshData] : cache.meshes) {
				if (meshData.instanceCount == 0) {
					continue;
				}

				// Bind cached VAO and draw - NO UPLOAD needed!
				glBindVertexArray(meshData.vao);
				glDrawElementsInstanced(
					GL_TRIANGLES,
					static_cast<GLsizei>(meshData.indexCount),
					GL_UNSIGNED_SHORT,
					nullptr,
					static_cast<GLsizei>(meshData.instanceCount)
				);
			}
		}

		glBindVertexArray(0);

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

} // namespace engine::world
