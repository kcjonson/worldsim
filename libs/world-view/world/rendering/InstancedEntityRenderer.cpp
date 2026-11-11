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
			levels.mesh = assets::AssetRegistry::Get().getTemplate(templateId);
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

	void InstancedEntityRenderer::want(uint32_t templateId, uint8_t level) {
		const uint32_t index = slotIndex(templateId, level);
		MeshSlot&	   slot = slotAt(index);
		if (slot.wantedFrame == frameIndex) {
			return;
		}
		slot.wantedFrame = frameIndex;
		if (slot.state == SlotState::Absent) {
			const TemplateLevels& levels = levelsOf(templateId);
			if (level == levels.reference) {
				slot.registryMesh = levels.mesh;
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

	void InstancedEntityRenderer::settleLevel(uint32_t templateId, TemplateLevels& levels, LevelStats& levelStats) {
		levels.frame = frameIndex;
		const uint8_t wanted = assets::detailLevelFor(levels.densities, levelStats.screenPixelsPerMeter);
		want(templateId, levels.reference); // the fallback of last resort
		want(templateId, wanted);

		const std::optional<uint8_t> level = assets::nearestReadyLevel(levels.readyMask, wanted);
		levels.drawSlot = level.has_value() ? slotIndex(templateId, *level) : kNoSlot;
		levels.use = kNoUse;
		if (!level.has_value()) {
			return;
		}
		MeshSlot& slot = meshSlots[levels.drawSlot];
		slot.lastDrawnFrame = frameIndex;
		levels.boundsMin = slot.boundsMin;
		levels.boundsMax = slot.boundsMax;
		levels.radius = slot.radius;
		if (!levels.densities.empty()) {
			const float density = levels.densities[*level];
			auto		use = std::find_if(levelStats.uses.begin(), levelStats.uses.end(), [&](const LevelStats::Use& u) {
				   return u.pixelsPerMeter == density;
			   });
			if (use == levelStats.uses.end()) {
				use = levelStats.uses.insert(levelStats.uses.end(), LevelStats::Use{.pixelsPerMeter = density});
			}
			levels.use = static_cast<uint32_t>(use - levelStats.uses.begin());
		}
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

		// Take what the level builder finished and the reference meshes asked for last frame and
		// upload them within budget first, so the walk below draws every level that is ready: a
		// level first wanted in this frame's walk draws from the next frame, and its template draws
		// the nearest ready level until then.
		collectAndUpload();

		// Every slot a template draws is Ready, so it exists now; a slot the walk makes isn't drawn this frame.
		LevelStats			   levelStats{.screenPixelsPerMeter = scale};
		const Foundation::Vec2 screenOffset{halfViewW - (camX * scale), halfViewH - (camY * scale)};
		planner.beginFrame(ctx.viewportWidth, ctx.viewportHeight, static_cast<uint32_t>(meshSlots.size()));
		runCount = 0;

		// drawInstanced draws immediately; the CPU triangle batch only draws when the
		// BatchRenderer is flushed. To keep depth order we draw every open run before an
		// interleaving animated entity (so the runs sit behind it), and force the anim batch
		// out (drawTriangles + flush) before the next instanced entity opens or joins a run
		// (so the anim sits behind it). Only one of the two kinds is ever pending at a time.
		auto flushRuns = [&]() {
			for (const uint32_t r : planner.close()) {
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

		// One walk of the stream: each upright is classified, bounded on the screen by the level its
		// template draws this frame, and placed in a run as it comes.
		bool outlineInjected = false;
		for (const assets::DepthSortItem& item : items) {
			// Inject the outline right before the first entity strictly nearer than it,
			// so everything at-or-behind its depth draws first (and it occludes them),
			// and everything nearer draws after (occluding it).
			if (!outlineInjected && selectionOutline.valid && item.anchorY > selectionOutline.anchorY) {
				flushRuns();
				flushAnim();
				drawOutline();
				outlineInjected = true;
			}

			const assets::Upright& upright = *item.upright;
			const uint32_t		   templateId = upright.templateId;
			TemplateLevels&		   levels =
				  templateId < templateLevels.size() && templateLevels[templateId].resolved ? templateLevels[templateId] : levelsOf(templateId);
			const renderer::TessellatedMesh* templateMesh = levels.mesh;
			if (templateMesh == nullptr) {
				continue;
			}
			if (upright.isAnimated && !templateMesh->parts.empty()) {
				// Animated entities carry per-part transforms; they can't be GPU-instanced
				// (each has unique deformed geometry), so emit them on the CPU triangle path.
				flushRuns(); // draw the open runs behind this actor
				// Keep the CPU batch inside the 16-bit index space.
				if (animVertexBase + static_cast<uint32_t>(templateMesh->vertices.size()) > 65535U) {
					flushAnim();
				}
				emitAnimated(*upright.entity, templateMesh, camX, camY, scale, halfViewW, halfViewH, animVertexBase);
				stats.entities++;
				continue;
			}

			if (levels.frame != frameIndex) {
				settleLevel(templateId, levels, levelStats);
			}
			if (levels.drawSlot == kNoSlot) {
				continue;
			}
			if (levels.use != kNoUse) {
				++levelStats.uses[levels.use].instances;
			}

			// The shader places a vertex at position + rotate(vertex * scale), and the camera
			// maps world to screen by scale and offset alone.
			const Foundation::Vec2 position{upright.position.x, upright.position.y};
			Foundation::Vec2	   worldMin;
			Foundation::Vec2	   worldMax;
			if (upright.rotation == 0.0F) {
				const Foundation::Vec2 a = levels.boundsMin * upright.scale;
				const Foundation::Vec2 b = levels.boundsMax * upright.scale;
				worldMin = position + Foundation::Vec2{std::min(a.x, b.x), std::min(a.y, b.y)};
				worldMax = position + Foundation::Vec2{std::max(a.x, b.x), std::max(a.y, b.y)};
			} else {
				const float r = levels.radius * std::abs(upright.scale);
				worldMin = position - Foundation::Vec2{r, r};
				worldMax = position + Foundation::Vec2{r, r};
			}
			const Foundation::Vec2 pixelMin = (worldMin * scale) + screenOffset;
			const Foundation::Vec2 pixelMax = (worldMax * scale) + screenOffset;
			const bool			   small = std::max(pixelMax.x - pixelMin.x, pixelMax.y - pixelMin.y) <= kUnorderedFootprintPixels;

			flushAnim(); // draw any pending anim behind this entity
			const uint32_t slot = levels.drawSlot;
			const uint32_t r = planner.place(slot, planner.cellsOf(pixelMin, pixelMax), small);
			if (r == runCount) {
				if (runCount == runs.size()) {
					runs.emplace_back();
				}
				runs[runCount++].slot = slot;
			}
			runs[r].instances.emplace_back(position, upright.rotation, upright.scale, upright.colorTint);
			stats.entities++;
		}

		flushRuns();
		flushAnim();
		stats.uprightOverlapSplits += planner.splits();
		if (!outlineInjected) {
			drawOutline(); // frontmost selection: draw last, on top of everything
		}

		levelBuilder.endFrame();
		std::sort(levelStats.uses.begin(), levelStats.uses.end(), [](const LevelStats::Use& a, const LevelStats::Use& b) {
			return a.pixelsPerMeter < b.pixelsPerMeter;
		});
		levelStats.pendingRequests = static_cast<uint32_t>(levelBuilder.pending() + uploadQueue.size());
		lastLevelStats = std::move(levelStats);
		evictStale();
	}

} // namespace worldview
