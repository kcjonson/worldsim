#include "InstancedEntityRenderer.h"

#include <assets/AssetRegistry.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>
#include <vector/Types.h>

#include <algorithm>
#include <cmath>

namespace worldview {

	InstancedEntityRenderer::TemplateLevels& InstancedEntityRenderer::levelsOf(uint32_t templateId) {
		if (templateId >= templateLevels.size()) {
			templateLevels.resize(static_cast<size_t>(templateId) + 1);
		}
		TemplateLevels& levels = templateLevels[templateId];
		if (!levels.resolved) {
			levels.densities = assets::AssetRegistry::Get().detailLevels(templateId);
			levels.reference = assets::referenceDetailLevel(levels.densities);
			levels.resolved = true;
		}
		return levels;
	}

	InstancedEntityRenderer::MeshSlot& InstancedEntityRenderer::slotAt(uint32_t index) {
		if (index >= meshSlots.size()) {
			meshSlots.resize(static_cast<size_t>(index) + 1);
		}
		return meshSlots[index];
	}

	void InstancedEntityRenderer::want(uint32_t templateId, uint8_t level, const renderer::TessellatedMesh& referenceMesh) {
		const uint32_t index = slotIndex(templateId, level);
		MeshSlot&	   slot = slotAt(index);
		if (slot.wantedFrame == frameIndex) {
			return;
		}
		slot.wantedFrame = frameIndex;
		if (slot.state == SlotState::Absent) {
			if (level == levelsOf(templateId).reference) {
				slot.registryMesh = &referenceMesh;
				slot.state = SlotState::Waiting;
				uploadQueue.push_back(index);
				return;
			}
			slot.state = SlotState::Requested;
		}
		if (slot.state == SlotState::Requested) {
			levelBuilder.want(templateId, level);
		}
	}

	void InstancedEntityRenderer::collectAndUpload() {
		for (TemplateLevelBuilder::Finished& done : levelBuilder.takeFinished()) {
			const uint32_t index = slotIndex(done.templateId, done.level);
			MeshSlot&	   slot = slotAt(index);
			if (!done.mesh.has_value() || done.mesh->vertices.empty()) {
				LOG_ERROR(Engine, "Template %u has no mesh at detail level %u", done.templateId, static_cast<unsigned>(done.level));
				slot.state = SlotState::Failed;
				continue;
			}
			slot.builtMesh = std::move(done.mesh);
			slot.state = SlotState::Waiting;
			uploadQueue.push_back(index);
		}

		uint32_t uploadedVertices = 0;
		while (!uploadQueue.empty()) {
			MeshSlot&						 slot = meshSlots[uploadQueue.front()];
			const renderer::TessellatedMesh& mesh = *slot.mesh();
			const auto						 vertices = static_cast<uint32_t>(mesh.vertices.size());
			if (uploadedVertices > 0 && uploadedVertices + vertices > kUploadVertexBudget) {
				break;
			}
			const uint32_t index = uploadQueue.front();
			uploadQueue.pop_front();
			uploadedVertices += vertices;

			slot.handle = Renderer::Primitives::uploadInstancedMesh(mesh, kInitialInstanceCapacity);
			if (!slot.handle.isValid()) {
				slot.state = SlotState::Failed;
				continue;
			}
			slot.capacity = kInitialInstanceCapacity;
			slot.triangles = static_cast<uint32_t>(mesh.getTriangleCount());
			slot.boundsMin = mesh.vertices.front();
			slot.boundsMax = mesh.vertices.front();
			slot.radius = 0.0F;
			for (const auto& v : mesh.vertices) {
				slot.boundsMin = {std::min(slot.boundsMin.x, v.x), std::min(slot.boundsMin.y, v.y)};
				slot.boundsMax = {std::max(slot.boundsMax.x, v.x), std::max(slot.boundsMax.y, v.y)};
				slot.radius = std::max(slot.radius, std::sqrt((v.x * v.x) + (v.y * v.y)));
			}
			slot.state = SlotState::Ready;
			slot.lastDrawnFrame = frameIndex;
			const uint32_t templateId = index / static_cast<uint32_t>(assets::kMaxDetailLevels);
			levelsOf(templateId).readyMask |= static_cast<uint8_t>(1U << (index % assets::kMaxDetailLevels));
		}
	}

	void InstancedEntityRenderer::evictStale() {
		uint32_t resident = 0;
		size_t	 bytes = 0;
		for (uint32_t index = 0; index < meshSlots.size(); ++index) {
			MeshSlot& slot = meshSlots[index];
			if (slot.state != SlotState::Ready || !slot.builtMesh.has_value()) {
				continue; // reference levels stay while the registry keeps their template
			}
			if (frameIndex - slot.lastDrawnFrame > kEvictAfterFrames) {
				slot.handle = {};
				slot.builtMesh.reset();
				slot.state = SlotState::Absent;
				TemplateLevels& levels = templateLevels[index / assets::kMaxDetailLevels];
				levels.readyMask = static_cast<uint8_t>(levels.readyMask & ~(1U << (index % assets::kMaxDetailLevels)));
				continue;
			}
			++resident;
			bytes += (slot.builtMesh->vertices.size() * (sizeof(Foundation::Vec2) + sizeof(Foundation::Color))) +
					 (slot.builtMesh->indices.size() * sizeof(uint16_t));
		}
		lastLevelStats.residentLevels = resident;
		lastLevelStats.residentBytes = bytes;
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
			animVertices.emplace_back((worldX - camX) * scale + halfViewW, (worldY - camY) * scale + halfViewH);
			if (hasMeshColors) {
				const auto& mc = mesh->colors[i];
				animColors.emplace_back(
					mc.r * entity.colorTint.r, mc.g * entity.colorTint.g, mc.b * entity.colorTint.b, mc.a * entity.colorTint.a
				);
			} else {
				animColors.emplace_back(entity.colorTint.r, entity.colorTint.g, entity.colorTint.b, entity.colorTint.a);
			}
		}
		for (const auto idx : mesh->indices) {
			animIndices.push_back(static_cast<uint16_t>(animVertexBase + idx));
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

		++frameIndex;
		animVertices.clear();
		animColors.clear();
		animIndices.clear();
		uint32_t animVertexBase = 0;

		// Classify every entity and ask for the level the camera's density picks for each
		// instanced template, then take what the level builder finished and upload within budget.
		itemBounds.resize(items.size());
		for (size_t i = 0; i < items.size(); ++i) {
			const assets::DepthSortItem& item = items[i];
			ItemBounds&					 bounds = itemBounds[i];
			bounds.kind = ItemKind::Skip;
			const assets::PlacedEntity& entity = *item.entity;
			const auto*					templateMesh = templateCache.get(entity.templateId);
			if (templateMesh == nullptr) {
				continue;
			}
			if (item.isAnimated && entity.partTransforms != nullptr && !entity.partTransforms->empty() && !templateMesh->parts.empty()) {
				bounds.kind = ItemKind::Animated;
				continue;
			}
			const TemplateLevels& levels = levelsOf(entity.templateId);
			bounds.wanted = assets::detailLevelFor(levels.densities, scale);
			want(entity.templateId, levels.reference, *templateMesh); // the fallback of last resort
			want(entity.templateId, bounds.wanted, *templateMesh);
			bounds.kind = ItemKind::Instanced;
		}
		levelBuilder.endFrame();
		collectAndUpload();

		// Bound every instanced entity by the level it draws: the wanted one when it is on the
		// GPU, else the nearest that is. The overlap grid is sized to their extent and cells to
		// their mean size.
		LevelStats levelStats{.screenPixelsPerMeter = scale};
		Foundation::Vec2 extentMin{0.0F, 0.0F};
		Foundation::Vec2 extentMax{0.0F, 0.0F};
		float			 sizeSum = 0.0F;
		uint32_t		 instancedCount = 0;
		for (size_t i = 0; i < items.size(); ++i) {
			ItemBounds& bounds = itemBounds[i];
			if (bounds.kind != ItemKind::Instanced) {
				continue;
			}
			const assets::PlacedEntity&	 entity = *items[i].entity;
			const TemplateLevels&		 levels = templateLevels[entity.templateId];
			const std::optional<uint8_t> level = assets::nearestReadyLevel(levels.readyMask, bounds.wanted);
			if (!level.has_value()) {
				bounds.kind = ItemKind::Skip;
				continue;
			}
			bounds.slot = slotIndex(entity.templateId, *level);
			MeshSlot& slot = meshSlots[bounds.slot];
			slot.lastDrawnFrame = frameIndex;
			if (!levels.densities.empty()) {
				const float density = levels.densities[*level];
				auto		use = std::find_if(levelStats.uses.begin(), levelStats.uses.end(), [&](const LevelStats::Use& u) {
					   return u.pixelsPerMeter == density;
				   });
				if (use == levelStats.uses.end()) {
					use = levelStats.uses.insert(levelStats.uses.end(), LevelStats::Use{.pixelsPerMeter = density});
				}
				++use->instances;
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
			if (instancedCount == 0) {
				extentMin = bounds.min;
				extentMax = bounds.max;
			}
			extentMin = {std::min(extentMin.x, bounds.min.x), std::min(extentMin.y, bounds.min.y)};
			extentMax = {std::max(extentMax.x, bounds.max.x), std::max(extentMax.y, bounds.max.y)};
			sizeSum += std::max(bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y);
			++instancedCount;
		}
		std::sort(levelStats.uses.begin(), levelStats.uses.end(), [](const LevelStats::Use& a, const LevelStats::Use& b) {
			return a.pixelsPerMeter < b.pixelsPerMeter;
		});
		levelStats.pendingRequests = static_cast<uint32_t>(levelBuilder.pending() + uploadQueue.size());
		lastLevelStats = std::move(levelStats);
		evictStale();

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
		overlapGrid.assign(static_cast<size_t>(gridW) * gridH, 0);
		auto cellOf = [&](float v, float origin, uint32_t cells) {
			return std::min(static_cast<uint32_t>(std::max((v - origin) / cellSize, 0.0F)), cells - 1);
		};

		latestRunOfSlot.assign(meshSlots.size(), 0);
		runCount = 0;
		firstOpenRun = 0;

		// drawInstanced draws immediately; the CPU triangle batch only draws when the
		// BatchRenderer is flushed. To keep depth order we draw every open run before an
		// interleaving animated entity (so the runs sit behind it), and force the anim batch
		// out (drawTriangles + flush) before the next instanced entity opens or joins a run
		// (so the anim sits behind it). Only one of the two kinds is ever pending at a time.
		auto flushRuns = [&]() {
			for (uint32_t r = firstOpenRun; r < runCount; ++r) {
				Run&	   run = runs[r];
				MeshSlot&  slot = meshSlots[run.slot];
				const auto count = static_cast<uint32_t>(run.instances.size());
				if (count > slot.capacity) {
					uint32_t capacity = slot.capacity;
					while (capacity < count) {
						capacity *= 2;
					}
					slot.handle = Renderer::Primitives::uploadInstancedMesh(*slot.mesh(), capacity);
					slot.capacity = capacity;
				}
				if (slot.handle.isValid()) {
					Renderer::Primitives::drawInstanced(slot.handle, run.instances.data(), count, camera);
					stats.uprightDrawCalls++;
					stats.uprightTriangles += count * slot.triangles;
				}
				run.instances.clear();
			}
			firstOpenRun = runCount;
		};

		// An entity joins its mesh's latest open run only if no entity in a run after
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
					latestCovering = std::max(latestCovering, overlapGrid[(static_cast<size_t>(y) * gridW) + x]);
				}
			}
			uint32_t   runKey = latestRunOfSlot[bounds.slot]; // 1 + run index
			const bool joinable = runKey > firstOpenRun && latestCovering <= runKey;
			if (!joinable) {
				if (runCount == runs.size()) {
					runs.emplace_back();
				}
				runs[runCount].slot = bounds.slot;
				runKey = ++runCount;
				latestRunOfSlot[bounds.slot] = runKey;
			}
			runs[runKey - 1].instances.emplace_back(
				Foundation::Vec2(entity.position.x, entity.position.y), entity.rotation, entity.scale, entity.colorTint
			);
			// Every covered cell held at most runKey, so runKey is now the latest there.
			for (uint32_t y = y0; y <= y1; ++y) {
				for (uint32_t x = x0; x <= x1; ++x) {
					overlapGrid[(static_cast<size_t>(y) * gridW) + x] = runKey;
				}
			}
		};
		auto flushAnim = [&]() {
			if (animIndices.empty()) {
				return;
			}
			stats.uprightDrawCalls++;
			// The batch renderer draws each submitted triangle as two (a quad with a repeated
			// corner) and counts it so; match that or this undercounts against the total.
			stats.uprightTriangles += static_cast<uint32_t>(animIndices.size() / 3) * 2;
			Renderer::Primitives::drawTriangles(Renderer::Primitives::TrianglesArgs{
				.vertices = animVertices.data(),
				.indices = animIndices.data(),
				.vertexCount = animVertices.size(),
				.indexCount = animIndices.size(),
				.colors = animColors.data()
			});
			Renderer::Primitives::flush(); // draw now, at this sorted position
			animVertices.clear();
			animColors.clear();
			animIndices.clear();
			animVertexBase = 0;
		};

		// Stroke the selected entity's outline (world-space rings -> screen) and flush
		// it now, so it sits at this depth in the stream and nearer entities occlude it.
		auto drawOutline = [&]() {
			if (!selectionOutline.valid) {
				return;
			}
			const auto&				so = selectionOutline;
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
			if (!outlineInjected && selectionOutline.valid && item.anchorY > selectionOutline.anchorY) {
				flushRuns();
				flushAnim();
				drawOutline();
				outlineInjected = true;
			}

			const ItemBounds&			bounds = itemBounds[i];
			const assets::PlacedEntity& entity = *item.entity;
			if (bounds.kind == ItemKind::Instanced) {
				flushAnim(); // draw any pending anim behind this entity
				addInstanced(entity, bounds);
				stats.entities++;
			} else if (bounds.kind == ItemKind::Animated) {
				// Animated entities carry per-part transforms; they can't be GPU-instanced
				// (each has unique deformed geometry), so emit them on the CPU triangle path.
				flushRuns(); // draw the open runs behind this actor
				const auto* templateMesh = templateCache.get(entity.templateId);
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
