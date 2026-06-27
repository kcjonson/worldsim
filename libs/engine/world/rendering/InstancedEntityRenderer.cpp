#include "InstancedEntityRenderer.h"

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

	void InstancedEntityRenderer::renderDynamic(const RenderContext& ctx, RenderStats& stats) {
		// Dynamic entities (from ECS) change position each frame, so we rebuild them.
		// GL state note: BatchRenderer::drawInstanced() sets up its own GL state internally,
		// so we don't need to carry state from the baked path here.
		const auto* dynamicEntities = ctx.dynamicEntities;
		if (dynamicEntities != nullptr && !dynamicEntities->empty()) {
			// Clear per-frame instance batches (keep capacity for reuse)
			for (auto& [defName, batch] : m_instanceBatches) {
				batch.clear();
			}

			float zoom = ctx.camera.zoom();
			float camX = ctx.camera.position().x;
			float camY = ctx.camera.position().y;

			const VisibleBounds vis = computeVisibleBounds(ctx.camera, ctx.viewportWidth, ctx.viewportHeight, ctx.pixelsPerMeter);

			for (const auto& entity : *dynamicEntities) {
				// Frustum culling for dynamic entities
				if (entity.position.x < vis.minX || entity.position.x > vis.maxX || entity.position.y < vis.minY ||
					entity.position.y > vis.maxY) {
					continue;
				}

				// Get or create mesh handle for this asset type
				const auto* templateMesh = m_templateCache.get(entity.defName);
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
						handleIt->second, instances.data(), static_cast<uint32_t>(instances.size()), cameraPos, zoom, ctx.pixelsPerMeter
					);
				}
			}
		}
	}

} // namespace engine::world
