#include "EntityRenderer.h"

#include "assets/AssetRegistry.h"
#include "world/chunk/ChunkCoordinate.h"

#include <graphics/Color.h>
#include <primitives/Primitives.h>

namespace engine::world {

EntityRenderer::EntityRenderer(float pixelsPerMeter) : m_pixelsPerMeter(pixelsPerMeter) {}

void EntityRenderer::render(const assets::PlacementExecutor& executor,
							const std::unordered_set<ChunkCoordinate>& processedChunks,
							const WorldCamera& camera,
							int viewportWidth,
							int viewportHeight) {
	m_batcher.clear();

	float halfViewW = static_cast<float>(viewportWidth) * 0.5F;
	float halfViewH = static_cast<float>(viewportHeight) * 0.5F;
	float zoom = camera.zoom();

	// Calculate visible world bounds (in tiles/meters)
	// Screen coordinates → World coordinates
	float viewWorldW = static_cast<float>(viewportWidth) / (m_pixelsPerMeter * zoom);
	float viewWorldH = static_cast<float>(viewportHeight) / (m_pixelsPerMeter * zoom);

	// Visible world bounds with small margin for entities on edges
	constexpr float kMargin = 2.0F;  // tiles
	float minWorldX = camera.position().x - (viewWorldW * 0.5F) - kMargin;
	float maxWorldX = camera.position().x + (viewWorldW * 0.5F) + kMargin;
	float minWorldY = camera.position().y - (viewWorldH * 0.5F) - kMargin;
	float maxWorldY = camera.position().y + (viewWorldH * 0.5F) + kMargin;

	// Group entities by defName for batching
	std::unordered_map<std::string, std::vector<assets::SpawnedInstance>> instancesByType;

	for (const auto& coord : processedChunks) {
		const auto* index = executor.getChunkIndex(coord);
		if (index == nullptr) {
			continue;
		}

		// Query only entities within visible bounds (view frustum culling)
		auto visibleEntities = index->queryRect(minWorldX, minWorldY, maxWorldX, maxWorldY);
		for (const auto* entity : visibleEntities) {
			// Entity position is in tiles (= meters since kTileSize = 1.0)
			// Convert world position to screen position
			float worldX = entity->position.x * kTileSize;
			float worldY = entity->position.y * kTileSize;

			float screenX = (worldX - camera.position().x) * m_pixelsPerMeter * zoom + halfViewW;
			float screenY = (worldY - camera.position().y) * m_pixelsPerMeter * zoom + halfViewH;

			// Create spawned instance for batching
			assets::SpawnedInstance instance{
				.position = Foundation::Vec2{screenX, screenY},
				.rotation = 0.0F,
				.scale = zoom,  // Scale with zoom
				.colorTint = Foundation::Color{1.0F, 1.0F, 1.0F, 1.0F}
			};

			instancesByType[entity->defName].push_back(instance);
		}
	}

	// Batch instances by type and render
	for (const auto& [defName, instances] : instancesByType) {
		const auto* templateMesh = getTemplate(defName);
		if (templateMesh == nullptr) {
			continue;
		}

		m_batcher.addInstances(*templateMesh, instances);
	}

	// Render all batches
	for (const auto& batch : m_batcher.batches()) {
		Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
			.vertices = batch.vertices.data(),
			.indices = batch.indices.data(),
			.vertexCount = batch.vertices.size(),
			.indexCount = batch.indices.size(),
			.colors = batch.colors.data()
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
