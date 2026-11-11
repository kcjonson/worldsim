#include "InstancedEntityRenderer.h"

#include <primitives/Primitives.h>
#include <vector/Types.h>

#include <algorithm>
#include <cmath>

namespace worldview {

	InstancedEntityRenderer::MeshSlot& InstancedEntityRenderer::meshSlot(uint32_t templateId, const renderer::TessellatedMesh& mesh) {
		if (templateId >= m_meshSlots.size()) {
			m_meshSlots.resize(static_cast<size_t>(templateId) + 1);
		}
		MeshSlot& slot = m_meshSlots[templateId];
		if (!slot.uploaded) {
			slot.handle = Renderer::Primitives::uploadInstancedMesh(mesh, kInitialInstanceCapacity);
			slot.capacity = kInitialInstanceCapacity;
			slot.triangles = static_cast<uint32_t>(mesh.getTriangleCount());
			if (!mesh.vertices.empty()) {
				slot.boundsMin = mesh.vertices.front();
				slot.boundsMax = mesh.vertices.front();
			}
			for (const auto& v : mesh.vertices) {
				slot.boundsMin = {std::min(slot.boundsMin.x, v.x), std::min(slot.boundsMin.y, v.y)};
				slot.boundsMax = {std::max(slot.boundsMax.x, v.x), std::max(slot.boundsMax.y, v.y)};
				slot.radius = std::max(slot.radius, std::sqrt((v.x * v.x) + (v.y * v.y)));
			}
			slot.uploaded = true;
		}
		return slot;
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

	void InstancedEntityRenderer::emitSorted(const std::vector<assets::DepthSortItem>& items, const assets::RenderContext& ctx, RenderStats& stats) {
		// GL state note: Primitives::drawInstanced() sets up its own GL state internally,
		// so we don't need to carry state from the baked path here.
		// NOTE: do NOT early-out on items.empty(). A selectable static below the tall-occluder
		// height (small rock/bush) is drawn by the baked path, so `items` can be empty while a
		// selection outline still needs stroking; the loop no-ops and the trailing drawOutline runs.

		const float			   zoom = ctx.camera.zoom();
		const float			   camX = ctx.camera.position().x;
		const float			   camY = ctx.camera.position().y;
		const float			   scale = ctx.pixelsPerMeter * zoom;
		const float			   halfViewW = static_cast<float>(ctx.viewportWidth) * 0.5F;
		const float			   halfViewH = static_cast<float>(ctx.viewportHeight) * 0.5F;
		const Renderer::Primitives::WorldCamera camera{.position = {camX, camY}, .zoom = zoom, .pixelsPerMeter = ctx.pixelsPerMeter};

		m_animVertices.clear();
		m_animColors.clear();
		m_animIndices.clear();
		uint32_t animVertexBase = 0;

		// Classify and bound every entity first: the overlap grid is sized to the
		// instanced entities' extent and cells to their mean size.
		m_itemBounds.resize(items.size());
		Foundation::Vec2 extentMin{0.0F, 0.0F};
		Foundation::Vec2 extentMax{0.0F, 0.0F};
		float			 sizeSum = 0.0F;
		uint32_t		 instancedCount = 0;
		for (size_t i = 0; i < items.size(); ++i) {
			const assets::DepthSortItem& item = items[i];
			ItemBounds&					 bounds = m_itemBounds[i];
			bounds.kind = ItemKind::Skip;
			const assets::PlacedEntity& entity = *item.entity;
			const auto*					templateMesh = m_templateCache.get(entity.templateId);
			if (templateMesh == nullptr) {
				continue;
			}
			if (item.isAnimated && entity.partTransforms != nullptr && !entity.partTransforms->empty() && !templateMesh->parts.empty()) {
				bounds.kind = ItemKind::Animated;
				continue;
			}
			const MeshSlot& slot = meshSlot(entity.templateId, *templateMesh);
			if (!slot.handle.isValid()) {
				continue;
			}
			// The shader places a vertex at position + rotate(vertex * scale).
			const Foundation::Vec2 position{entity.position.x, entity.position.y};
			if (entity.rotation == 0.0F) {
				const Foundation::Vec2 a = slot.boundsMin * entity.scale;
				const Foundation::Vec2 b = slot.boundsMax * entity.scale;
				bounds.min = position + Foundation::Vec2{std::min(a.x, b.x), std::min(a.y, b.y)};
				bounds.max = position + Foundation::Vec2{std::max(a.x, b.x), std::max(a.y, b.y)};
			} else {
				const float r = slot.radius * std::abs(entity.scale);
				bounds.min = position - Foundation::Vec2{r, r};
				bounds.max = position + Foundation::Vec2{r, r};
			}
			bounds.kind = ItemKind::Instanced;
			if (instancedCount == 0) {
				extentMin = bounds.min;
				extentMax = bounds.max;
			}
			extentMin = {std::min(extentMin.x, bounds.min.x), std::min(extentMin.y, bounds.min.y)};
			extentMax = {std::max(extentMax.x, bounds.max.x), std::max(extentMax.y, bounds.max.y)};
			sizeSum += std::max(bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y);
			++instancedCount;
		}

		uint32_t gridW = 1;
		uint32_t gridH = 1;
		float	 cellSize = 1.0F;
		if (instancedCount > 0) {
			const float extentW = extentMax.x - extentMin.x;
			const float extentH = extentMax.y - extentMin.y;
			const auto	maxSide = static_cast<float>(kMaxOverlapGridSide);
			cellSize = std::max({sizeSum / static_cast<float>(instancedCount), extentW / maxSide, extentH / maxSide, 1.0e-3F});
			gridW = std::clamp(static_cast<uint32_t>(std::ceil(extentW / cellSize)), 1U, kMaxOverlapGridSide);
			gridH = std::clamp(static_cast<uint32_t>(std::ceil(extentH / cellSize)), 1U, kMaxOverlapGridSide);
		}
		m_overlapGrid.assign(static_cast<size_t>(gridW) * gridH, 0);
		auto cellOf = [&](float v, float origin, uint32_t cells) {
			return std::min(static_cast<uint32_t>(std::max((v - origin) / cellSize, 0.0F)), cells - 1);
		};

		m_latestRunOfTemplate.assign(m_meshSlots.size(), 0);
		m_runCount = 0;
		m_firstOpenRun = 0;

		// drawInstanced draws immediately; the CPU triangle batch only draws when the
		// BatchRenderer is flushed. To keep depth order we draw every open run before an
		// interleaving animated entity (so the runs sit behind it), and force the anim batch
		// out (drawTriangles + flush) before the next instanced entity opens or joins a run
		// (so the anim sits behind it). Only one of the two kinds is ever pending at a time.
		auto flushRuns = [&]() {
			for (uint32_t r = m_firstOpenRun; r < m_runCount; ++r) {
				Run&	   run = m_runs[r];
				MeshSlot&  slot = m_meshSlots[run.templateId]; // made by meshSlot when the entity was bounded
				const auto count = static_cast<uint32_t>(run.instances.size());
				if (count > slot.capacity) {
					uint32_t capacity = slot.capacity;
					while (capacity < count) {
						capacity *= 2;
					}
					slot.handle = Renderer::Primitives::uploadInstancedMesh(*m_templateCache.get(run.templateId), capacity);
					slot.capacity = capacity;
				}
				if (slot.handle.isValid()) {
					Renderer::Primitives::drawInstanced(slot.handle, run.instances.data(), count, camera);
					stats.uprightDrawCalls++;
					stats.uprightTriangles += count * slot.triangles;
				}
				run.instances.clear();
			}
			m_firstOpenRun = m_runCount;
		};

		// An entity joins its template's latest open run only if no entity in a run after
		// that one shares a grid cell with it: those are drawn after the run but sit behind
		// this entity in depth, so the two must not overlap. Otherwise it opens a new run.
		auto addInstanced = [&](const assets::PlacedEntity& entity, const ItemBounds& bounds) {
			const uint32_t x0 = cellOf(bounds.min.x, extentMin.x, gridW);
			const uint32_t x1 = cellOf(bounds.max.x, extentMin.x, gridW);
			const uint32_t y0 = cellOf(bounds.min.y, extentMin.y, gridH);
			const uint32_t y1 = cellOf(bounds.max.y, extentMin.y, gridH);
			uint32_t	   latestCovering = 0;
			for (uint32_t y = y0; y <= y1; ++y) {
				for (uint32_t x = x0; x <= x1; ++x) {
					latestCovering = std::max(latestCovering, m_overlapGrid[(static_cast<size_t>(y) * gridW) + x]);
				}
			}
			uint32_t   runKey = m_latestRunOfTemplate[entity.templateId]; // 1 + run index
			const bool joinable = runKey > m_firstOpenRun && latestCovering <= runKey;
			if (!joinable) {
				if (m_runCount == m_runs.size()) {
					m_runs.emplace_back();
				}
				m_runs[m_runCount].templateId = entity.templateId;
				runKey = ++m_runCount;
				m_latestRunOfTemplate[entity.templateId] = runKey;
			}
			m_runs[runKey - 1].instances.emplace_back(
				Foundation::Vec2(entity.position.x, entity.position.y), entity.rotation, entity.scale, entity.colorTint
			);
			// Every covered cell held at most runKey, so runKey is now the latest there.
			for (uint32_t y = y0; y <= y1; ++y) {
				for (uint32_t x = x0; x <= x1; ++x) {
					m_overlapGrid[(static_cast<size_t>(y) * gridW) + x] = runKey;
				}
			}
		};
		auto flushAnim = [&]() {
			if (m_animIndices.empty()) {
				return;
			}
			stats.uprightDrawCalls++;
			// The batch renderer draws each submitted triangle as two (a quad with a repeated
			// corner) and counts it so; match that or this undercounts against the total.
			stats.uprightTriangles += static_cast<uint32_t>(m_animIndices.size() / 3) * 2;
			Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
				.vertices = m_animVertices.data(),
				.indices = m_animIndices.data(),
				.vertexCount = m_animVertices.size(),
				.indexCount = m_animIndices.size(),
				.colors = m_animColors.data()
			});
			Renderer::Primitives::flush(); // draw now, at this sorted position
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
						.start = a, .end = b, .style = {.color = col, .width = so.widthPx}, .id = "sel-outline", .key = 0
					});
					// Filled dot per vertex for round joins (drawLine has butt caps).
					Renderer::Primitives::drawCircle(Renderer::Primitives::CircleArgs{
						.center = a, .radius = so.widthPx * 0.5F, .style = {.fill = col}, .id = "sel-join", .key = 0
					});
				}
			}
			Renderer::Primitives::flush(); // draw at this sorted position
		};

		bool outlineInjected = false;
		for (size_t i = 0; i < items.size(); ++i) {
			const assets::DepthSortItem& item = items[i];
			// Inject the outline right before the first entity strictly nearer than it,
			// so everything at-or-behind its depth draws first (and it occludes them),
			// and everything nearer draws after (occluding it).
			if (!outlineInjected && m_selectionOutline.valid && item.anchorY > m_selectionOutline.anchorY) {
				flushRuns();
				flushAnim();
				drawOutline();
				outlineInjected = true;
			}

			const ItemBounds&			bounds = m_itemBounds[i];
			const assets::PlacedEntity& entity = *item.entity;
			if (bounds.kind == ItemKind::Instanced) {
				flushAnim(); // draw any pending anim behind this entity
				addInstanced(entity, bounds);
				stats.entities++;
			} else if (bounds.kind == ItemKind::Animated) {
				// Animated entities carry per-part transforms; they can't be GPU-instanced
				// (each has unique deformed geometry), so emit them on the CPU triangle path.
				flushRuns(); // draw the open runs behind this actor
				const auto* templateMesh = m_templateCache.get(entity.templateId);
				// Keep the CPU batch inside the 16-bit index space.
				if (animVertexBase + static_cast<uint32_t>(templateMesh->vertices.size()) > 65535U) {
					flushAnim();
				}
				emitAnimated(entity, templateMesh, camX, camY, scale, halfViewW, halfViewH, animVertexBase);
				stats.entities++;
			}
		}

		flushRuns();
		flushAnim();
		if (!outlineInjected) {
			drawOutline(); // frontmost selection: draw last, on top of everything
		}
	}

} // namespace worldview
