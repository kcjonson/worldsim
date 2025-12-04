#include "EntityRenderer.h"

#include "assets/AssetRegistry.h"
#include "world/chunk/ChunkCoordinate.h"

#include <graphics/Color.h>
#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>
#include <cmath>

namespace engine::world {

EntityRenderer::EntityRenderer(float pixelsPerMeter) : m_pixelsPerMeter(pixelsPerMeter) {}

EntityRenderer::~EntityRenderer() {
	// Release all GPU mesh handles
	auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
	if (batchRenderer != nullptr) {
		for (auto& [defName, handle] : m_meshHandles) {
			batchRenderer->releaseInstancedMesh(handle);
		}
	}
	m_meshHandles.clear();
}

void EntityRenderer::render(const assets::PlacementExecutor& executor,
							const std::unordered_set<ChunkCoordinate>& processedChunks,
							const WorldCamera& camera,
							int viewportWidth,
							int viewportHeight) {
	if (m_useInstancing) {
		renderInstanced(executor, processedChunks, camera, viewportWidth, viewportHeight);
	} else {
		renderBatched(executor, processedChunks, camera, viewportWidth, viewportHeight);
	}
}

// --- GPU Instancing Path ---

Renderer::InstancedMeshHandle& EntityRenderer::getOrCreateMeshHandle(
	const std::string& defName,
	const renderer::TessellatedMesh* mesh
) {
	auto it = m_meshHandles.find(defName);
	if (it != m_meshHandles.end()) {
		return it->second;
	}

	// Upload mesh to GPU
	auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
	if (batchRenderer != nullptr && mesh != nullptr) {
		auto handle = batchRenderer->uploadInstancedMesh(*mesh, 20000);
		auto [insertedIt, _] = m_meshHandles.emplace(defName, handle);
		return insertedIt->second;
	}

	// Return invalid handle if we can't create one
	static Renderer::InstancedMeshHandle s_invalidHandle;
	return s_invalidHandle;
}

void EntityRenderer::renderInstanced(
	const assets::PlacementExecutor& executor,
	const std::unordered_set<ChunkCoordinate>& processedChunks,
	const WorldCamera& camera,
	int viewportWidth,
	int viewportHeight
) {
	m_lastEntityCount = 0;

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
	float minWorldX = camX - (viewWorldW * 0.5F) - kMargin;
	float maxWorldX = camX + (viewWorldW * 0.5F) + kMargin;
	float minWorldY = camY - (viewWorldH * 0.5F) - kMargin;
	float maxWorldY = camY + (viewWorldH * 0.5F) + kMargin;

	// Process each chunk, collect instances for visible entities
	for (const auto& coord : processedChunks) {
		const auto* index = executor.getChunkIndex(coord);
		if (index == nullptr) {
			continue;
		}

		// Query only entities within visible bounds (view frustum culling)
		auto visibleEntities = index->queryRect(minWorldX, minWorldY, maxWorldX, maxWorldY);

		for (const auto* entity : visibleEntities) {
			// Get or create mesh handle for this asset type
			const auto* templateMesh = getTemplate(entity->defName);
			if (templateMesh == nullptr) {
				continue;
			}

			// Ensure mesh handle exists
			auto& handle = getOrCreateMeshHandle(entity->defName, templateMesh);
			if (!handle.isValid()) {
				continue;
			}

			// Create instance data (world-space - GPU does the transform!)
			Renderer::InstanceData instance(
				Foundation::Vec2(entity->position.x, entity->position.y),
				entity->rotation,
				entity->scale,
				Foundation::Vec4(
					entity->colorTint.x,
					entity->colorTint.y,
					entity->colorTint.z,
					entity->colorTint.w
				)
			);

			// Add to batch for this mesh type
			m_instanceBatches[entity->defName].push_back(instance);
			m_lastEntityCount++;
		}
	}

	// Draw each batch with instanced rendering
	auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
	if (batchRenderer == nullptr) {
		return;
	}

	// CRITICAL: Flush any pending batched geometry (e.g., tiles) before drawing instanced entities.
	// Without this, tiles would be drawn AFTER instanced entities (at endFrame), making entities
	// appear behind the tiles. By flushing now, we ensure tiles render first.
	batchRenderer->flush();

	// Set viewport on BatchRenderer (it needs this for projection)
	batchRenderer->setViewport(viewportWidth, viewportHeight);

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
			handleIt->second,
			instances.data(),
			static_cast<uint32_t>(instances.size()),
			cameraPos,
			zoom,
			m_pixelsPerMeter
		);
	}
}

// --- CPU Batching Path (Original Implementation) ---

void EntityRenderer::renderBatched(const assets::PlacementExecutor& executor,
							const std::unordered_set<ChunkCoordinate>& processedChunks,
							const WorldCamera& camera,
							int viewportWidth,
							int viewportHeight) {
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
	float minWorldX = camX - (viewWorldW * 0.5F) - kMargin;
	float maxWorldX = camX + (viewWorldW * 0.5F) + kMargin;
	float minWorldY = camY - (viewWorldH * 0.5F) - kMargin;
	float maxWorldY = camY + (viewWorldH * 0.5F) + kMargin;

	uint16_t vertexIndex = 0;

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
			bool hasMeshColors = templateMesh->hasColors();

			// Check if we need rotation
			constexpr float kRotationEpsilon = 0.0001F;
			bool noRotation = std::abs(entity->rotation) < kRotationEpsilon;

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
						m_colors.emplace_back(
							entity->colorTint.r,
							entity->colorTint.g,
							entity->colorTint.b,
							entity->colorTint.a
						);
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
						m_colors.emplace_back(
							entity->colorTint.r,
							entity->colorTint.g,
							entity->colorTint.b,
							entity->colorTint.a
						);
					}
				}
			}

			// Add indices
			for (const auto& idx : templateMesh->indices) {
				m_indices.push_back(vertexIndex + idx);
			}

			vertexIndex += static_cast<uint16_t>(templateMesh->vertices.size());
			m_lastEntityCount++;
		}
	}

	// Single draw call for all entities
	if (!m_indices.empty()) {
		Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
			.vertices = m_vertices.data(),
			.indices = m_indices.data(),
			.vertexCount = m_vertices.size(),
			.indexCount = m_indices.size(),
			.colors = m_colors.data()
		});
	}
}

const renderer::TessellatedMesh* EntityRenderer::getTemplate(const std::string& defName) {
	// Check cache first
	auto it = m_templateCache.find(defName);
	if (it != m_templateCache.end()) {
		return it->second;
	}

	// Get from registry and cache
	auto& registry = assets::AssetRegistry::Get();
	const auto* mesh = registry.getTemplate(defName);
	m_templateCache[defName] = mesh;
	return mesh;
}

}  // namespace engine::world
