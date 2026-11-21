#include "InstancedEntityRenderer.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/InstanceTransform.h>
#include <primitives/Primitives.h>
#include <threading/TaskPool.h>
#include <utils/Log.h>
#include <vector/Types.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace worldview {

	namespace {
		size_t meshBytes(const renderer::TessellatedMesh& mesh, uint32_t capacity) {
			return (mesh.vertices.size() * (sizeof(Foundation::Vec2) + sizeof(Foundation::Color))) + (mesh.indices.size() * sizeof(uint16_t)) +
				   (static_cast<size_t>(capacity) * sizeof(Renderer::InstanceData));
		}

		uint32_t useOf(std::vector<InstancedEntityRenderer::LevelStats::Use>& uses, float density) {
			auto use = std::find_if(uses.begin(), uses.end(), [&](const InstancedEntityRenderer::LevelStats::Use& entry) {
				return entry.pixelsPerMeter == density;
			});
			if (use == uses.end()) {
				use = uses.insert(uses.end(), InstancedEntityRenderer::LevelStats::Use{.pixelsPerMeter = density});
			}
			return static_cast<uint32_t>(use - uses.begin());
		}
	} // namespace

	InstancedEntityRenderer::InstancedEntityRenderer()
		: levelBuilder([](const TemplateLevelKey& key) {
			  assets::AssetRegistry::BuiltTemplate built = assets::AssetRegistry::Get().buildTemplateLevel(key.templateId, key.level);
			  return std::move(built.mesh);
		  }),
		  viewBuilder([](const ItemViewKey& key) -> std::optional<assets::ItemView> {
			  const assets::AssetDefinition* def = assets::AssetRegistry::Get().templateRecord(key.templateId).def;
			  if (def == nullptr) {
				  return std::nullopt;
			  }
			  return assets::buildItemMesh(*def, key.turnQuanta, key.level, assets::GameViewConfig::Get().config());
		  }) {}

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
				// Uploaded now, outside the budget, so a template draws on the frame it first appears.
				slot.registryMesh = levels.mesh;
				upload(index);
				return;
			}
			slot.state = SlotState::Requested;
		}
		if (slot.state == SlotState::Requested) {
			levelBuilder.want({.templateId = templateId, .level = level});
		}
	}

	void InstancedEntityRenderer::collectAndUpload() {
		for (auto& done : levelBuilder.takeFinished()) {
			const uint32_t index = slotIndex(done.key.templateId, done.key.level);
			MeshSlot&	   slot = slotAt(index);
			if (!done.result.has_value() || done.result->vertices.empty()) {
				LOG_ERROR(Engine, "Template %u has no mesh at detail level %u", done.key.templateId, static_cast<unsigned>(done.key.level));
				slot.state = SlotState::Failed;
				continue;
			}
			slot.builtMesh = std::move(done.result);
			slot.state = SlotState::Waiting;
			uploadQueue.push_back({.index = index, .view = false});
		}

		const uint64_t generation = assets::AssetRegistry::Get().generation();
		for (auto& done : viewBuilder.takeFinished()) {
			const std::optional<uint32_t> slot =
				done.key.generation == generation ? itemViews.findView(done.key.templateId, done.key.turnQuanta, done.key.level) : std::nullopt;
			if (!slot.has_value() || itemViews.view(*slot).state != ViewState::Requested) {
				continue; // released, or from before a clear
			}
			if (!done.result.has_value() || !done.result->error.empty() || done.result->mesh.vertices.empty()) {
				LOG_ERROR(
					Engine, "Template %u has no view at %u quanta, detail level %u: %s", done.key.templateId, done.key.turnQuanta,
					static_cast<unsigned>(done.key.level), done.result.has_value() ? done.result->error.c_str() : "the build failed"
				);
				itemViews.setState(*slot, ViewState::Failed);
				continue;
			}
			viewSlots[*slot].mesh = std::move(done.result->mesh);
			viewSlots[*slot].strips = std::move(done.result->strips);
			itemViews.setState(*slot, ViewState::Waiting);
			uploadQueue.push_back({.index = *slot, .view = true});
		}

		uint32_t uploadedVertices = 0;
		while (!uploadQueue.empty()) {
			const Upload next = uploadQueue.front();
			if (next.view && itemViews.view(next.index).state != ViewState::Waiting) {
				uploadQueue.pop_front(); // released while it waited
				continue;
			}
			const auto vertices = static_cast<uint32_t>(next.view ? viewSlots[next.index].mesh.vertices.size() : meshSlots[next.index].mesh()->vertices.size());
			if (uploadedVertices > 0 && uploadedVertices + vertices > kUploadVertexBudget) {
				break;
			}
			uploadQueue.pop_front();
			uploadedVertices += vertices;
			if (next.view) {
				uploadView(next.index);
			} else {
				upload(next.index);
			}
		}
	}

	void InstancedEntityRenderer::upload(uint32_t index) {
		MeshSlot&						 slot = meshSlots[index];
		const renderer::TessellatedMesh& mesh = *slot.mesh();
		const uint32_t					 templateId = index / static_cast<uint32_t>(assets::kMaxDetailLevels);
		const auto						 level = static_cast<uint8_t>(index % assets::kMaxDetailLevels);
		slot.handle = Renderer::Primitives::uploadInstancedMesh(mesh, kInitialInstanceCapacity);
		if (!slot.handle.isValid()) {
			LOG_ERROR(Engine, "Template %u failed to upload at detail level %u", templateId, static_cast<unsigned>(level));
			release(index, SlotState::Failed);
			return;
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
		levelsOf(templateId).readyMask |= static_cast<uint8_t>(1U << level);
	}

	void InstancedEntityRenderer::release(uint32_t index, SlotState state) {
		MeshSlot& slot = meshSlots[index];
		slot.handle = {};
		slot.builtMesh.reset();
		slot.capacity = 0;
		slot.state = state;
		TemplateLevels& levels = templateLevels[index / assets::kMaxDetailLevels];
		levels.readyMask = static_cast<uint8_t>(levels.readyMask & ~(1U << (index % assets::kMaxDetailLevels)));
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
				release(index, SlotState::Absent);
				continue;
			}
			++resident;
			bytes += meshBytes(*slot.builtMesh, slot.capacity);
		}
		lastLevelStats.residentLevels = resident;
		lastLevelStats.residentBytes = bytes;

		// A reference view stays while an installed item stands at its template and turn: the registry keeps that
		// layout exactly as long (AssetRegistry::releaseUnaskedLayouts).
		assets::AssetRegistry& registry = assets::AssetRegistry::Get();
		for (const uint32_t slot : itemViews.releaseStale(frameIndex, kEvictAfterFrames, [&](uint32_t templateId, uint32_t turnQuanta) {
				 return registry.keepsStripLayout(templateId, turnQuanta);
			 })) {
			viewSlots[slot] = ViewSlot{};
		}
		uint32_t views = 0;
		size_t	 viewBytes = 0;
		for (const ViewSlot& view : viewSlots) {
			if (view.handle.isValid()) {
				++views;
				viewBytes += meshBytes(view.mesh, view.capacity);
			}
		}
		lastLevelStats.residentViews = views;
		lastLevelStats.residentViewBytes = viewBytes;
	}

	void InstancedEntityRenderer::installView(uint32_t slot, assets::ItemView view) {
		const ViewRecord& record = itemViews.view(slot);
		if (!view.error.empty() || view.mesh.vertices.empty()) {
			LOG_ERROR(
				Engine, "Template %u has no view at %u quanta, detail level %u: %s", record.templateId, record.turnQuanta,
				static_cast<unsigned>(record.level), view.error.empty() ? "nothing drawn" : view.error.c_str()
			);
			itemViews.setState(slot, ViewState::Failed);
			return;
		}
		viewSlots[slot].mesh = std::move(view.mesh);
		viewSlots[slot].strips = std::move(view.strips);
		uploadView(slot);
	}

	void InstancedEntityRenderer::uploadView(uint32_t slot) {
		ViewSlot& view = viewSlots[slot];
		view.handle = Renderer::Primitives::uploadInstancedMesh(view.mesh, kInitialViewInstanceCapacity);
		if (!view.handle.isValid()) {
			const ViewRecord& record = itemViews.view(slot);
			LOG_ERROR(Engine, "The view of template %u at %u quanta, detail level %u, failed to upload", record.templateId, record.turnQuanta, static_cast<unsigned>(record.level));
			viewSlots[slot] = ViewSlot{};
			itemViews.setState(slot, ViewState::Failed);
			return;
		}
		view.capacity = kInitialViewInstanceCapacity;
		itemViews.view(slot).drawnFrame = frameIndex;
		itemViews.setState(slot, ViewState::Ready);
	}

	void InstancedEntityRenderer::wantView(const assets::ItemLayout& layout, uint8_t level, uint8_t reference) {
		const auto [slot, isNew] = itemViews.viewSlot(layout.templateId, layout.turnQuanta, level, level == reference, frameIndex);
		if (slot >= viewSlots.size()) {
			viewSlots.resize(static_cast<size_t>(slot) + 1);
		}
		ViewRecord& record = itemViews.view(slot);
		record.wantedFrame = frameIndex;
		if (record.state == ViewState::Absent) {
			if (level == reference) {
				// On this thread at its first want, uploaded at once, so the item draws on the frame it first appears
				// (rotatable-placed-items 10): one item's parts, a few dozen shapes.
				const assets::AssetDefinition* def = assets::AssetRegistry::Get().templateRecord(layout.templateId).def;
				installView(
					slot, def != nullptr ? assets::buildItemMesh(*def, layout.turnQuanta, level, assets::GameViewConfig::Get().config())
										 : assets::ItemView{.error = "no definition"}
				);
				return;
			}
			itemViews.setState(slot, ViewState::Requested);
		}
		if (record.state == ViewState::Requested) {
			viewBuilder.want({.templateId = layout.templateId, .turnQuanta = layout.turnQuanta, .level = level, .generation = assets::AssetRegistry::Get().generation()});
		}
	}

	uint32_t InstancedEntityRenderer::settleView(const assets::ItemLayout& layout, LevelStats& levelStats) {
		const TemplateLevels& levels = levelsOf(layout.templateId);
		const uint8_t		  wanted = assets::detailLevelFor(levels.densities, levelStats.screenPixelsPerMeter);
		wantView(layout, levels.reference, levels.reference); // the fallback of last resort
		wantView(layout, wanted, levels.reference);

		const std::optional<uint8_t> level = assets::nearestReadyLevel(itemViews.readyMask(layout.templateId, layout.turnQuanta), wanted);
		if (!level.has_value()) {
			return kNotAView;
		}
		const uint32_t slot = *itemViews.findView(layout.templateId, layout.turnQuanta, *level);
		itemViews.view(slot).drawnFrame = frameIndex;
		return slot;
	}

	void InstancedEntityRenderer::prepareFirstViews(const std::vector<assets::DepthSortItem>& items, float screenPixelsPerMeter) {
		struct Job {
			uint32_t					   slot = 0;
			const assets::AssetDefinition* def = nullptr;
			uint32_t					   turnQuanta = 0;
			uint8_t						   level = 0;
		};
		std::vector<Job>			 jobs;
		std::unordered_set<uint32_t> seenLayouts;
		for (const assets::DepthSortItem& item : items) {
			const assets::ItemLayout* layout = item.upright->itemLayout;
			if (layout == nullptr || !seenLayouts.insert(layout->id).second) {
				continue;
			}
			const TemplateLevels&		   levels = levelsOf(layout->templateId);
			const assets::AssetDefinition* def = assets::AssetRegistry::Get().templateRecord(layout->templateId).def;
			for (const uint8_t level : {levels.reference, assets::detailLevelFor(levels.densities, screenPixelsPerMeter)}) {
				const auto [slot, isNew] = itemViews.viewSlot(layout->templateId, layout->turnQuanta, level, level == levels.reference, frameIndex);
				if (isNew) {
					jobs.push_back({.slot = slot, .def = def, .turnQuanta = layout->turnQuanta, .level = level});
				}
			}
		}
		if (jobs.empty()) {
			return;
		}
		viewSlots.resize(std::max<size_t>(viewSlots.size(), itemViews.slotBound()));
		std::vector<assets::ItemView> built(jobs.size());
		{
			foundation::TaskPool pool;
			pool.parallelFor(0, jobs.size(), 1, [&](size_t first, size_t last) {
				for (size_t i = first; i < last; ++i) {
					built[i] = jobs[i].def != nullptr ? assets::buildItemMesh(*jobs[i].def, jobs[i].turnQuanta, jobs[i].level, assets::GameViewConfig::Get().config())
													  : assets::ItemView{.error = "no definition"};
				}
			});
		}
		for (size_t i = 0; i < jobs.size(); ++i) {
			installView(jobs[i].slot, std::move(built[i]));
		}
		LOG_DEBUG(Engine, "Built %zu item views for the first frame on the task pool", jobs.size());
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

		const assets::InstanceTransform transform(entity.position, entity.rotation, entity.scale);
		for (size_t i = 0; i < animVerts.size(); ++i) {
			const glm::vec2 world = transform.toWorld({animVerts[i].x, animVerts[i].y});
			animVertices.emplace_back((world.x - camX) * scale + halfViewW, (world.y - camY) * scale + halfViewH);
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
			levels.use = useOf(levelStats.uses, levels.densities[*level]);
		}
	}

	void InstancedEntityRenderer::drawOutlinePieces(size_t first, size_t count, const assets::RenderContext& ctx) {
		const SelectionOutline& outline = selectionOutline;
		const Foundation::Color color{outline.rgba.r, outline.rgba.g, outline.rgba.b, outline.rgba.a};
		const auto				toScreen = [&](Foundation::Vec2 point) {
			 return ctx.camera.worldToScreen(point.x, point.y, ctx.viewportWidth, ctx.viewportHeight, ctx.pixelsPerMeter);
		};
		for (size_t index = first; index < first + count; ++index) {
			const SelectionOutline::Piece& piece = outline.pieces[index];
			for (const std::vector<Foundation::Vec2>& line : piece.lines) {
				const size_t points = line.size();
				if (points < 2) {
					continue;
				}
				const size_t segments = piece.closed ? points : points - 1;
				for (size_t i = 0; i < segments; ++i) {
					const Foundation::Vec2 from = toScreen(line[i]);
					const Foundation::Vec2 to = toScreen(line[(i + 1) % points]);
					Renderer::Primitives::drawLine(Renderer::Primitives::LineArgs{
						.start = from, .end = to, .style = {.color = color, .width = outline.widthPx}, .id = "sel-outline", .key = 0
					});
				}
				// Filled dot per vertex for round joins (drawLine has butt caps); an open line's last end too.
				for (size_t i = 0; i < points; ++i) {
					Renderer::Primitives::drawCircle(Renderer::Primitives::CircleArgs{
						.center = toScreen(line[i]), .radius = outline.widthPx * 0.5F, .style = {.fill = color}, .id = "sel-join", .key = 0
					});
				}
			}
		}
		Renderer::Primitives::flush(); // draw at this sorted position
	}

	void InstancedEntityRenderer::emitSorted(
		const std::vector<assets::DepthSortItem>& items, uint32_t stripUprights, const assets::RenderContext& ctx, RenderStats& stats
	) {
		// GL state note: Primitives::drawInstanced() sets up its own GL state internally,
		// so we don't need to carry state from the baked path here.
		// NOTE: do NOT early-out on items.empty(). A selectable static below the tall-occluder
		// height (small rock/bush) is drawn by the baked path, so `items` can be empty while a
		// selection outline still needs stroking; the loop no-ops and the trailing outline draw runs.

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

		// A registry clear started template ids and layouts over: the views kept by them go, with their waiting uploads.
		if (itemViews.follow(assets::AssetRegistry::Get().generation())) {
			viewSlots.clear();
			std::erase_if(uploadQueue, [](const Upload& upload) { return upload.view; });
		}

		// Take what the level builders finished and upload it within budget first, so the walk below
		// draws every level that is ready: a level other than the reference first wanted in this
		// frame's walk draws from a later frame, and its template draws the nearest ready level until
		// then. A reference level uploads in the walk, the first time its template is drawn.
		collectAndUpload();
		if (frameIndex == 1) {
			prepareFirstViews(items, scale);
		}

		// The items were gathered before this, so every template they name has an id the registry
		// has handed out, and every slot the walk can draw is below this bound.
		const uint32_t		   slotBound = (assets::AssetRegistry::Get().templateCount() + 1) * static_cast<uint32_t>(assets::kMaxDetailLevels);
		LevelStats			   levelStats{.screenPixelsPerMeter = scale};
		const Foundation::Vec2 screenOffset{halfViewW - (camX * scale), halfViewH - (camY * scale)};
		// Each parts item's layout in the stream numbers its strips' planner mesh ids on from the template slots at
		// its first strip, so the bound grows only with the strips in view (rotatable-placed-items 9.2).
		planner.beginFrame(ctx.viewportWidth, ctx.viewportHeight, slotBound + stripUprights);
		uint32_t nextStripMesh = slotBound;
		runCount = 0;

		// The outline's pieces and their boxes on the screen, padded by the line's half width.
		outlineAnchors.clear();
		outlineBoxes.clear();
		if (selectionOutline.valid) {
			const float pad = selectionOutline.widthPx * 0.5F;
			for (const SelectionOutline::Piece& piece : selectionOutline.pieces) {
				ScreenBox box{.min = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
							  .max = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()}};
				for (const std::vector<Foundation::Vec2>& line : piece.lines) {
					for (const Foundation::Vec2& point : line) {
						const Foundation::Vec2 pixel = (point * scale) + screenOffset;
						box.min = {std::min(box.min.x, pixel.x - pad), std::min(box.min.y, pixel.y - pad)};
						box.max = {std::max(box.max.x, pixel.x + pad), std::max(box.max.y, pixel.y + pad)};
					}
				}
				outlineAnchors.push_back(piece.anchorY);
				outlineBoxes.push_back(box);
			}
		}
		outlineCursor.begin(outlineAnchors, outlineBoxes);

		// One ranged draw of `strips` consecutive depth strips of a view, from the run's strip on.
		auto drawStrips = [&](Run& run, uint32_t strips) {
			ViewSlot&  view = viewSlots[run.view];
			const auto count = static_cast<uint32_t>(run.instances.size());
			if (itemViews.view(run.view).state == ViewState::Ready && count > view.capacity) {
				uint32_t capacity = view.capacity;
				while (capacity < count) {
					capacity *= 2;
				}
				view.handle = Renderer::Primitives::uploadInstancedMesh(view.mesh, capacity);
				view.capacity = capacity;
				if (!view.handle.isValid()) {
					LOG_ERROR(Engine, "Item view %u failed to grow to %u instances; it is dropped", run.view, capacity);
					view = ViewSlot{};
					itemViews.setState(run.view, ViewState::Failed);
				}
			}
			if (itemViews.view(run.view).state != ViewState::Ready) {
				return;
			}
			const assets::ViewStrip& first = view.strips[run.strip];
			const assets::ViewStrip& last = view.strips[run.strip + strips - 1];
			const uint32_t			 indices = last.firstIndex + last.indexCount - first.firstIndex;
			Renderer::Primitives::drawInstanced(view.handle, run.instances.data(), count, camera, {.first = first.firstIndex, .count = indices});
			stats.uprightDrawCalls++;
			stats.itemDrawCalls++;
			stats.uprightTriangles += count * (indices / 3);
		};

		// drawInstanced draws immediately; the CPU triangle batch only draws when the
		// BatchRenderer is flushed. To keep depth order we draw every open run before an
		// interleaving animated entity (so the runs sit behind it), and force the anim batch
		// out (drawTriangles + flush) before the next instanced entity opens or joins a run
		// (so the anim sits behind it). Only one of the two kinds is ever pending at a time.
		auto flushRuns = [&]() {
			const std::span<const uint32_t> closing = planner.close();
			for (const uint32_t index : closing) {
				Run& run = runs[index];
				if (run.view != kNotAView) {
					run.itemsApart = !itemViews.frameOf(run.layout).itemsMeet;
				}
			}
			spanRuns(planner, closing, runs, runSpans);
			for (const RunSpan& span : runSpans) {
				Run& run = runs[closing[span.first]];
				if (run.view != kNotAView) {
					drawStrips(run, span.count);
					for (uint32_t i = 0; i < span.count; ++i) {
						runs[closing[span.first + i]].instances.clear();
					}
					continue;
				}
				MeshSlot&  slot = meshSlots[run.slot];
				const auto count = static_cast<uint32_t>(run.instances.size());
				if (slot.state == SlotState::Ready && count > slot.capacity) {
					uint32_t capacity = slot.capacity;
					while (capacity < count) {
						capacity *= 2;
					}
					slot.handle = Renderer::Primitives::uploadInstancedMesh(*slot.mesh(), capacity);
					slot.capacity = capacity;
					if (!slot.handle.isValid()) {
						// Its template settles on another ready level from the next frame.
						LOG_ERROR(Engine, "Mesh slot %u failed to grow to %u instances; that level is dropped", run.slot, capacity);
						release(run.slot, SlotState::Failed);
					}
				}
				if (slot.state == SlotState::Ready) {
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

		// The outline pieces due before an upright at `anchorY` (OutlineCursor), stroked and flushed now, so
		// everything at or behind their depth draws first and everything nearer draws after, occluding them.
		auto injectOutline = [&](float anchorY, const std::optional<ScreenBox>& box, bool ownStrip) {
			const size_t first = outlineCursor.drawnSoFar();
			const size_t count = outlineCursor.before(anchorY, box, ownStrip);
			if (count == 0) {
				return;
			}
			flushRuns();
			flushAnim();
			drawOutlinePieces(first, count, ctx);
		};

		// One walk of the stream: each upright is classified, bounded on the screen by the level its
		// template draws this frame, and placed in a run as it comes.
		for (const assets::DepthSortItem& item : items) {
			const assets::Upright& upright = *item.upright;

			// A parts item's depth strip: drawn as an index range of its view, placed like any instanced entity
			// with the strip as its mesh (rotatable-placed-items 9.2).
			if (upright.itemLayout != nullptr) {
				const assets::ItemLayout& layout = *upright.itemLayout;
				LayoutFrame*			  settled = nullptr;
				if (itemViews.settle(layout, frameIndex, nextStripMesh, settled)) {
					settled->view = settleView(layout, levelStats);
					const TemplateLevels& levels = levelsOf(layout.templateId);
					if (settled->view != kNotAView && !levels.densities.empty()) {
						settled->levelUse = useOf(levelStats.viewUses, levels.densities[itemViews.view(settled->view).level]);
					}
					// In the stored order, ascending anchor, as the stream brings them, so each strip ranks after
					// the strips of its item it shares cells with and stays in their layer.
					planner.reserveOrders(settled->firstMesh, static_cast<uint32_t>(layout.layout.strips.size()));
				}
				// Strip 0 has the item's least anchor, so it comes first of its strips, and the gather keeps or culls
				// an item's strips together by its position: once an item.
				if (upright.strip == 0) {
					ItemViewIndex::noteItem(*settled, upright.position);
					if (settled->levelUse != UINT32_MAX) {
						++levelStats.viewUses[settled->levelUse].instances;
					}
				}
				if (settled->view == kNotAView || upright.strip >= viewSlots[settled->view].strips.size() ||
					viewSlots[settled->view].strips[upright.strip].indexCount == 0) {
					continue;
				}
				const assets::ItemStrip& box = layout.layout.strips[upright.strip];
				const Foundation::Vec2	 position{upright.position.x, upright.position.y};
				const Foundation::Vec2	 pixelMin = ((position + Foundation::Vec2{box.minX, box.minY}) * scale) + screenOffset;
				const Foundation::Vec2	 pixelMax = ((position + Foundation::Vec2{box.maxX, box.maxY}) * scale) + screenOffset;
				const bool				 small = std::max(pixelMax.x - pixelMin.x, pixelMax.y - pixelMin.y) <= kUnorderedFootprintPixels;
				injectOutline(
					item.anchorY, ScreenBox{.min = pixelMin, .max = pixelMax},
					&layout == selectionOutline.itemLayout && upright.position == selectionOutline.itemPosition
				);

				flushAnim(); // draw any pending anim behind this strip
				const uint32_t runIndex = planner.place(settled->firstMesh + upright.strip, planner.cellsOf(pixelMin, pixelMax), small);
				if (runIndex == runCount) {
					if (runCount == runs.size()) {
						runs.emplace_back();
					}
					Run& run = runs[runCount++];
					run.view = settled->view;
					run.layout = layout.id;
					run.strip = upright.strip;
				}
				runs[runIndex].instances.emplace_back(position, 0.0F, 1.0F, upright.colorTint);
				stats.entities++;
				continue;
			}

			const uint32_t		   templateId = upright.templateId;
			TemplateLevels&		   levels =
				  templateId < templateLevels.size() && templateLevels[templateId].resolved ? templateLevels[templateId] : levelsOf(templateId);
			const renderer::TessellatedMesh* templateMesh = levels.mesh;
			if (templateMesh == nullptr) {
				continue;
			}
			if (upright.isAnimated && !templateMesh->parts.empty()) {
				injectOutline(item.anchorY, std::nullopt, false);
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
			injectOutline(item.anchorY, ScreenBox{.min = pixelMin, .max = pixelMax}, false);

			flushAnim(); // draw any pending anim behind this entity
			const uint32_t slot = levels.drawSlot;
			const uint32_t r = planner.place(slot, planner.cellsOf(pixelMin, pixelMax), small);
			if (r == runCount) {
				if (runCount == runs.size()) {
					runs.emplace_back();
				}
				Run& run = runs[runCount++];
				run.slot = slot;
				run.view = kNotAView;
			}
			runs[r].instances.emplace_back(position, upright.rotation, upright.scale, upright.colorTint);
			stats.entities++;
		}

		flushRuns();
		flushAnim();
		stats.uprightOverlapSplits += planner.splits();
		const size_t firstLeft = outlineCursor.drawnSoFar();
		if (const size_t left = outlineCursor.remaining(); left > 0) {
			drawOutlinePieces(firstLeft, left, ctx); // frontmost: drawn last, on top of everything
		}

		levelBuilder.endFrame();
		viewBuilder.endFrame();
		const auto byDensity = [](const LevelStats::Use& a, const LevelStats::Use& b) { return a.pixelsPerMeter < b.pixelsPerMeter; };
		std::sort(levelStats.uses.begin(), levelStats.uses.end(), byDensity);
		std::sort(levelStats.viewUses.begin(), levelStats.viewUses.end(), byDensity);
		levelStats.pendingRequests = static_cast<uint32_t>(levelBuilder.pending() + viewBuilder.pending() + uploadQueue.size());
		lastLevelStats = std::move(levelStats);
		evictStale();
	}

} // namespace worldview
