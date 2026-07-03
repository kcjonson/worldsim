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

	void InstancedEntityRenderer::emitAnimated(
		const assets::PlacedEntity&		 entity,
		const renderer::TessellatedMesh* mesh,
		float camX, float camY, float scale, float halfViewW, float halfViewH,
		uint32_t& animVertexBase
	) {
		const auto& xforms = *entity.partTransforms;
		const bool	hasMeshColors = mesh->hasColors();

		// Copy the template verts, then deform each part's range by its transform.
		thread_local std::vector<Foundation::Vec2> animVerts;
		animVerts.assign(mesh->vertices.begin(), mesh->vertices.end());
		for (size_t k = 0; k < mesh->parts.size() && k < xforms.size(); ++k) {
			const auto&	   part = mesh->parts[k];
			const uint32_t end = std::min<uint32_t>(part.vertexStart + part.vertexCount, static_cast<uint32_t>(animVerts.size()));
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
				const auto& mc = mesh->colors[i];
				m_animColors.emplace_back(
					mc.r * entity.colorTint.r, mc.g * entity.colorTint.g, mc.b * entity.colorTint.b, mc.a * entity.colorTint.a
				);
			} else {
				m_animColors.emplace_back(entity.colorTint.r, entity.colorTint.g, entity.colorTint.b, entity.colorTint.a);
			}
		}
		for (const auto idx : mesh->indices) {
			m_animIndices.push_back(static_cast<uint16_t>(animVertexBase + idx));
		}
		animVertexBase += static_cast<uint32_t>(animVerts.size());
	}

	void InstancedEntityRenderer::emitSorted(const std::vector<DepthSortItem>& items, const RenderContext& ctx, RenderStats& stats) {
		// GL state note: BatchRenderer::drawInstanced() sets up its own GL state internally,
		// so we don't need to carry state from the baked path here.
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer == nullptr || items.empty()) {
			return;
		}

		const float			   zoom = ctx.camera.zoom();
		const float			   camX = ctx.camera.position().x;
		const float			   camY = ctx.camera.position().y;
		const float			   scale = ctx.pixelsPerMeter * zoom;
		const float			   halfViewW = static_cast<float>(ctx.viewportWidth) * 0.5F;
		const float			   halfViewH = static_cast<float>(ctx.viewportHeight) * 0.5F;
		const Foundation::Vec2 cameraPos(camX, camY);

		m_runInstances.clear();
		m_runDefName.clear();
		m_animVertices.clear();
		m_animColors.clear();
		m_animIndices.clear();
		uint32_t animVertexBase = 0;

		// drawInstanced draws immediately; the CPU triangle batch only draws when the
		// BatchRenderer is flushed. To keep submission order == depth order we flush the
		// current instanced run before an interleaving animated entity (so the run sits
		// behind it), and force the anim batch out (drawTriangles + flush) before a
		// following instanced run (so the anim sits behind that run). Only one of the two
		// pending batches is ever non-empty at a time, so the two flushes don't fight.
		auto flushRun = [&]() {
			if (m_runInstances.empty()) {
				return;
			}
			auto it = m_meshHandles.find(m_runDefName);
			if (it != m_meshHandles.end() && it->second.isValid()) {
				batchRenderer->drawInstanced(
					it->second, m_runInstances.data(), static_cast<uint32_t>(m_runInstances.size()), cameraPos, zoom, ctx.pixelsPerMeter
				);
			}
			m_runInstances.clear();
			m_runDefName.clear();
		};
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
			batchRenderer->flush(); // draw now, at this sorted position
			m_animVertices.clear();
			m_animColors.clear();
			m_animIndices.clear();
			animVertexBase = 0;
		};

		// Stroke the selected entity's outline (world-space rings -> screen) and flush
		// it now, so it sits at this depth in the stream and nearer entities occlude it.
		auto drawOutline = [&]() {
			if (!m_selectionOutline.valid) {
				return;
			}
			const auto&				so = m_selectionOutline;
			const Foundation::Color col{so.rgba.r, so.rgba.g, so.rgba.b, so.rgba.a};
			for (const auto& ring : so.worldRings) {
				const size_t n = ring.size();
				if (n < 2) {
					continue;
				}
				for (size_t i = 0; i < n; ++i) {
					const Foundation::Vec2 a =
						ctx.camera.worldToScreen(ring[i].x, ring[i].y, ctx.viewportWidth, ctx.viewportHeight, ctx.pixelsPerMeter);
					const Foundation::Vec2 b = ctx.camera.worldToScreen(
						ring[(i + 1) % n].x, ring[(i + 1) % n].y, ctx.viewportWidth, ctx.viewportHeight, ctx.pixelsPerMeter
					);
					Renderer::Primitives::drawLine(Renderer::Primitives::LineArgs{
						.start = a, .end = b, .style = {.color = col, .width = so.widthPx}, .id = "sel-outline", .zIndex = 0
					});
					// Filled dot per vertex for round joins (drawLine has butt caps).
					Renderer::Primitives::drawCircle(Renderer::Primitives::CircleArgs{
						.center = a, .radius = so.widthPx * 0.5F, .style = {.fill = col}, .id = "sel-join", .zIndex = 0
					});
				}
			}
			batchRenderer->flush(); // draw at this sorted position
		};

		bool outlineInjected = false;
		for (const auto& item : items) {
			// Inject the outline right before the first entity strictly nearer than it,
			// so everything at-or-behind its depth draws first (and it occludes them),
			// and everything nearer draws after (occluding it).
			if (!outlineInjected && m_selectionOutline.valid && item.anchorY > m_selectionOutline.anchorY) {
				flushRun();
				flushAnim();
				drawOutline();
				outlineInjected = true;
			}

			const assets::PlacedEntity& entity = *item.entity;
			const auto*					templateMesh = m_templateCache.get(entity.defName);
			if (templateMesh == nullptr) {
				continue;
			}

			// Animated entities carry per-part transforms; they can't be GPU-instanced
			// (each has unique deformed geometry), so emit them on the CPU triangle path.
			if (item.isAnimated && entity.partTransforms != nullptr && !entity.partTransforms->empty() && !templateMesh->parts.empty()) {
				flushRun(); // draw the pending run behind this actor
				// Keep the CPU batch inside the 16-bit index space.
				if (animVertexBase + static_cast<uint32_t>(templateMesh->vertices.size()) > 65535U) {
					flushAnim();
				}
				emitAnimated(entity, templateMesh, camX, camY, scale, halfViewW, halfViewH, animVertexBase);
				stats.entities++;
				continue;
			}

			// Non-animated: coalesce consecutive same-defName entities into one run.
			auto& handle = getOrCreateMeshHandle(entity.defName, templateMesh);
			if (!handle.isValid()) {
				continue;
			}
			flushAnim(); // draw any pending anim behind this run
			if (!m_runInstances.empty() && m_runDefName != entity.defName) {
				flushRun();
			}
			m_runDefName = entity.defName;
			m_runInstances.emplace_back(
				Foundation::Vec2(entity.position.x, entity.position.y), entity.rotation, entity.scale, entity.colorTint
			);
			stats.entities++;
		}

		flushRun();
		flushAnim();
		if (!outlineInjected) {
			drawOutline(); // frontmost selection: draw last, on top of everything
		}
	}

} // namespace engine::world
