#include "InstancedEntityRenderer.h"

#include "assets/AssetRegistry.h"

#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>

namespace engine::world {

	InstancedEntityRenderer::~InstancedEntityRenderer() {
		// Release all shared GPU mesh handles (these use BatchRenderer's management)
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer != nullptr) {
			for (auto& [defName, handle] : m_meshHandles) {
				batchRenderer->releaseInstancedMesh(handle);
			}
		}
		m_meshHandles.clear();
	}

	const renderer::TessellatedMesh* InstancedEntityRenderer::getTemplate(const std::string& defName) {
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

	Renderer::InstancedMeshHandle&
	InstancedEntityRenderer::getOrCreateMeshHandle(const std::string& defName, const renderer::TessellatedMesh* mesh) {
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

	void InstancedEntityRenderer::renderDynamic(
		const std::vector<assets::PlacedEntity>* dynamicEntities,
		const WorldCamera&						 camera,
		int										 viewportWidth,
		int										 viewportHeight,
		float									 pixelsPerMeter,
		RenderStats&							 stats
	) {
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
			float scale = pixelsPerMeter * zoom;
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
				stats.entities++;
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
					// so these draws are NOT added to stats.drawCalls (avoids double count)
					batchRenderer->drawInstanced(
						handleIt->second, instances.data(), static_cast<uint32_t>(instances.size()), cameraPos, zoom, pixelsPerMeter
					);
				}
			}
		}
	}

} // namespace engine::world
