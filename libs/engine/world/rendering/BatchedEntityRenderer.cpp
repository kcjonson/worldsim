#include "BatchedEntityRenderer.h"

#include "assets/AssetRegistry.h"
#include "world/chunk/ChunkCoordinate.h"

#include <cmath>
#include <primitives/Primitives.h>
#include <vector/Tessellator.h>

namespace engine::world {

	const renderer::TessellatedMesh* BatchedEntityRenderer::getTemplate(const std::string& defName) {
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

	void BatchedEntityRenderer::render(
		const assets::PlacementExecutor&		   executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>*   dynamicEntities,
		const WorldCamera&						   camera,
		int										   viewportWidth,
		int										   viewportHeight,
		float									   pixelsPerMeter,
		RenderStats&							   stats
	) {
		stats.entities = 0;
		stats.drawCalls = 0;
		stats.triangles = 0;

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
		float scale = pixelsPerMeter * zoom;
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
				stats.entities++;
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
				stats.entities++;
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

} // namespace engine::world
