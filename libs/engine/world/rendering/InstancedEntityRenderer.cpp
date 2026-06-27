#include "InstancedEntityRenderer.h"

#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>
#include <vector/Types.h>

#include <algorithm>

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

			// Per-frame CPU batch for animated entities (per-part deformed; not GPU-instanceable).
			m_animVertices.clear();
			m_animColors.clear();
			m_animIndices.clear();
			uint32_t	animVertexBase = 0;
			const float scale = ctx.pixelsPerMeter * zoom;
			const float halfViewW = static_cast<float>(ctx.viewportWidth) * 0.5F;
			const float halfViewH = static_cast<float>(ctx.viewportHeight) * 0.5F;

			// Emit the accumulated animated batch as one CPU draw, then reset it. The batch uses a
			// 16-bit index space, so we flush before it would overflow (only reachable at very high
			// animated-entity counts) and once at the end. A mid-loop flush draws under the
			// instanced entities; the common single end-flush draws over them.
			auto flushAnim = [&]() {
				if (m_animIndices.empty()) {
					return;
				}
				Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
					.vertices = m_animVertices.data(),
					.indices = m_animIndices.data(),
					.vertexCount = m_animVertices.size(),
					.indexCount = m_animIndices.size(),
					.colors = m_animColors.data()
				});
				m_animVertices.clear();
				m_animColors.clear();
				m_animIndices.clear();
				animVertexBase = 0;
			};

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

				// Animated entities carry per-part transforms; they can't be GPU-instanced (each
				// has unique deformed geometry), so emit them to the CPU batch with their parts moved.
				if (entity.partTransforms != nullptr && !entity.partTransforms->empty() && !templateMesh->parts.empty()) {
					// Keep the batch inside the 16-bit index space: flush if this entity overflows it.
					if (animVertexBase + static_cast<uint32_t>(templateMesh->vertices.size()) > 65535U) {
						flushAnim();
					}
					const auto& xforms = *entity.partTransforms;
					const bool	hasMeshColors = templateMesh->hasColors();

					// Copy the template verts, then deform each part's range by its transform.
					thread_local std::vector<Foundation::Vec2> animVerts;
					animVerts.assign(templateMesh->vertices.begin(), templateMesh->vertices.end());
					for (size_t k = 0; k < templateMesh->parts.size() && k < xforms.size(); ++k) {
						const auto&	   part = templateMesh->parts[k];
						const uint32_t end =
							std::min<uint32_t>(part.vertexStart + part.vertexCount, static_cast<uint32_t>(animVerts.size()));
						for (uint32_t i = part.vertexStart; i < end; ++i) {
							const glm::vec2 r = xforms[k].apply({animVerts[i].x, animVerts[i].y});
							animVerts[i] = {r.x, r.y};
						}
					}

					const float entityScale = entity.scale;
					for (size_t i = 0; i < animVerts.size(); ++i) {
						const float worldX = animVerts[i].x * entityScale + entity.position.x;
						const float worldY = animVerts[i].y * entityScale + entity.position.y;
						m_animVertices.emplace_back((worldX - camX) * scale + halfViewW, (worldY - camY) * scale + halfViewH);
						if (hasMeshColors) {
							const auto& mc = templateMesh->colors[i];
							m_animColors.emplace_back(
								mc.r * entity.colorTint.r, mc.g * entity.colorTint.g, mc.b * entity.colorTint.b, mc.a * entity.colorTint.a
							);
						} else {
							m_animColors.emplace_back(entity.colorTint.r, entity.colorTint.g, entity.colorTint.b, entity.colorTint.a);
						}
					}
					for (const auto idx : templateMesh->indices) {
						m_animIndices.push_back(static_cast<uint16_t>(animVertexBase + idx));
					}
					animVertexBase += static_cast<uint32_t>(animVerts.size());
					stats.entities++;
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

			// Draw the animated dynamic entities (CPU per-part deformed) over the instanced ones,
			// so a colonist's limbs sit on top of any instanced ground items.
			flushAnim();
		}
	}

} // namespace engine::world
