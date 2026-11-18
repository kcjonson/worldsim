#include "VisibilityCoverRenderer.h"

#include "assets/AssetRegistry.h"
#include "assets/placement/RenderContext.h"

#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <ecs/systems/VisionSystem.h>

#include <primitives/MaskImage.h>
#include <primitives/Primitives.h>
#include <primitives/WorldCamera.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <span>

namespace worldview {

	namespace {

		namespace Primitives = Renderer::Primitives;

		// The fills reach this far past the view, so their own anti-aliased edges fall outside it.
		constexpr float kCoverOverhangPixels = 4.0F;

		constexpr std::size_t kTriangleVertices = 3;

		// How many frames a replaced mesh is kept before its buffers go.
		constexpr std::uint64_t kRetiredMeshFrames = 3;

		Foundation::Vec2 toWorldMeters(const geometry::Vec2i64& millimetres) {
			return {
				static_cast<float>(static_cast<double>(millimetres.x) / static_cast<double>(geometry::kMillimetersPerMeter)),
				static_cast<float>(static_cast<double>(millimetres.y) / static_cast<double>(geometry::kMillimetersPerMeter))
			};
		}

		glm::dvec2 toDouble(const geometry::Vec2i64& point) { return {static_cast<double>(point.x), static_cast<double>(point.y)}; }

		double microsecondsSince(std::chrono::steady_clock::time_point start) {
			return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
		}

		// World millimetres to screen pixels: the camera's visible rect scaled to the viewport, no turn.
		struct ScreenMap {
			glm::dvec2 originMm{0.0};
			double	   pixelsPerMm = 1.0;

			[[nodiscard]] Foundation::Vec2 operator()(glm::dvec2 millimetres) const {
				const glm::dvec2 pixel = (millimetres - originMm) * pixelsPerMm;
				return {static_cast<float>(pixel.x), static_cast<float>(pixel.y)};
			}
		};

		// Every polygon of the frame's masks in screen pixels, in one block reserved up front, so the
		// spans the mask elements hold stay put until the draws read them.
		class ScreenPoints {
		  public:
			void reserve(std::size_t count) { points.reserve(count); }

			std::span<const Foundation::Vec2> add(std::span<const glm::dvec2> millimetres, const ScreenMap& map) {
				const std::size_t first = points.size();
				for (const glm::dvec2 point : millimetres) {
					points.push_back(map(point));
				}
				return {points.data() + first, millimetres.size()};
			}

			std::span<const Foundation::Vec2> add(const std::vector<Foundation::Vec2>& screen) {
				const std::size_t first = points.size();
				points.insert(points.end(), screen.begin(), screen.end());
				return {points.data() + first, screen.size()};
			}

		  private:
			std::vector<Foundation::Vec2> points;
		};

		// A bright shape's rim sides as edges with its inside on the left.
		std::vector<DirectedEdge> rimEdgesOf(const ecs::PlayerVisibilitySystem::DrawnShape& drawn) {
			std::vector<DirectedEdge> edges;
			const std::size_t		  count = drawn.outline.size();
			double					  doubled = 0.0;
			for (std::size_t k = 0; k < count; ++k) {
				doubled += geometry::cross(drawn.outline[k], drawn.outline[(k + 1) % count]).toDouble();
			}
			for (std::size_t side = 0; side < count; ++side) {
				if (drawn.rim[side]) {
					const geometry::Vec2i64& from = drawn.outline[side];
					const geometry::Vec2i64& to	  = drawn.outline[(side + 1) % count];
					edges.emplace_back(doubled >= 0.0 ? from : to, doubled >= 0.0 ? to : from);
				}
			}
			return edges;
		}

	} // namespace

	std::vector<renderer::TessellatedMesh> seenMeshes(const std::vector<vision::SeenArea::Triangle>& triangles) {
		constexpr std::size_t				   kTrianglesPerMesh = kSeenMeshVertexLimit / kTriangleVertices;
		std::vector<renderer::TessellatedMesh> meshes;
		meshes.reserve((triangles.size() + kTrianglesPerMesh - 1) / kTrianglesPerMesh);
		for (std::size_t first = 0; first < triangles.size(); first += kTrianglesPerMesh) {
			const std::size_t		  count = std::min(kTrianglesPerMesh, triangles.size() - first);
			renderer::TessellatedMesh mesh;
			mesh.vertices.reserve(count * kTriangleVertices);
			mesh.indices.reserve(count * kTriangleVertices);
			for (std::size_t triangle = first; triangle < first + count; ++triangle) {
				for (const geometry::Vec2i64& corner : triangles[triangle].corners) {
					mesh.indices.push_back(static_cast<std::uint16_t>(mesh.vertices.size()));
					mesh.vertices.push_back(toWorldMeters(corner));
				}
			}
			mesh.colors.assign(mesh.vertices.size(), Foundation::Color::white());
			meshes.push_back(std::move(mesh));
		}
		return meshes;
	}

	assets::DrawBound coverDrawBound(ecs::World& world, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float pixelsPerMeter) {
		const ecs::PlayerVisibilitySystem* visibility = ecs::PlayerVisibilitySystem::gate(world);
		if (visibility == nullptr) {
			return assets::DrawBound::everywhere();
		}
		const assets::VisibleBounds view = assets::computeVisibleBounds(
			camera, viewportWidth, viewportHeight, pixelsPerMeter, assets::entityCullMarginMeters(assets::AssetRegistry::Get().maxReachMeters())
		);
		std::vector<ecs::PlayerVisibilitySystem::SeenBox> boxes;
		visibility->appendSeenBoxes({view.minX, view.minY}, {view.maxX, view.maxY}, boxes);
		const float		  margin = drawBoundMarginMeters(camera, pixelsPerMeter);
		assets::DrawBound bound;
		for (const ecs::PlayerVisibilitySystem::SeenBox& box : boxes) {
			bound.add({box.low.x - margin, box.high.x + margin, box.low.y - margin, box.high.y + margin});
		}
		return bound;
	}

	float drawBoundMarginMeters(const world::WorldCamera& camera, float pixelsPerMeter) {
		return kDrawBoundMarginPixels / (pixelsPerMeter * camera.zoom());
	}

	bool cameraLoadsChunk(ecs::World& world, const world::WorldCamera& camera, float pixelsPerMeter, world::ChunkCoordinate coord) {
		const ecs::PlayerVisibilitySystem* visibility = ecs::PlayerVisibilitySystem::gate(world);
		if (visibility == nullptr) {
			return true;
		}
		const float reach =
			assets::entityCullMarginMeters(assets::AssetRegistry::Get().maxReachMeters()) + drawBoundMarginMeters(camera, pixelsPerMeter);
		const glm::vec2 low{static_cast<float>(coord.x) * world::kChunkWorldSize, static_cast<float>(coord.y) * world::kChunkWorldSize};
		return visibility->anySeenBox(low - glm::vec2(reach), low + glm::vec2(world::kChunkWorldSize + reach));
	}

	// A mesh the GPU may still be drawing from is let go a few frames later: deleting its buffers at once
	// can stall the frame until the GPU is done with them.
	void VisibilityCoverRenderer::retire(std::vector<Renderer::InstancedMeshHandle>& uploaded) {
		if (!uploaded.empty()) {
			retired.emplace_back(frameNumber, std::move(uploaded));
		}
		uploaded.clear();
	}

	void VisibilityCoverRenderer::upload(const vision::SeenArea& seen) {
		triangles.clear();
		seen.appendTriangles(triangles);
		// The old meshes are released here; one a pending draw still names waits for the flush (docs/masks.md).
		meshes.clear();
		for (const renderer::TessellatedMesh& mesh : seenMeshes(triangles)) {
			meshes.push_back(Renderer::Primitives::uploadInstancedMesh(mesh, 1));
		}
		uploadedEpoch = seen.epoch();
		++uploadCount;
	}

	// Each colonist in view: his bright shape, the feather on its rim sides, and his drawn shadows.
	void VisibilityCoverRenderer::gather(
		ecs::World&					 world,
		ecs::PlayerVisibilitySystem& visibility,
		const world::WorldCamera&	 camera,
		int							 viewportWidth,
		int							 viewportHeight,
		float						 pixelsPerMeter
	) {
		const Foundation::Rect view{0.0F, 0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)};
		frameLayers.visibleOutlines.clear();
		frameLayers.pendingOutlines.clear();
		frameColonists.clear();
		const ecs::VisionSystem* vision = world.tryGetSystem<ecs::VisionSystem>();
		if (vision == nullptr) {
			return;
		}
		const vision::SightTunables& tunables = vision->sightTunables();
		const double				 featherMm = static_cast<double>(tunables.sightFeatherMm);

		for (auto [entity, position, colonist, memory] : world.view<ecs::Position, ecs::Colonist, ecs::Memory>()) {
			// A colonist whose sight misses the view draws nothing in it.
			const Foundation::Vec2 corner =
				camera.worldToScreen(position.value.x - memory.sightRadius, position.value.y - memory.sightRadius, viewportWidth, viewportHeight, pixelsPerMeter);
			const Foundation::Vec2 opposite =
				camera.worldToScreen(position.value.x + memory.sightRadius, position.value.y + memory.sightRadius, viewportWidth, viewportHeight, pixelsPerMeter);
			if (std::max(corner.x, opposite.x) < view.x || std::max(corner.y, opposite.y) < view.y || std::min(corner.x, opposite.x) > view.x + view.width ||
				std::min(corner.y, opposite.y) > view.y + view.height) {
				continue;
			}
			const std::optional<ecs::PlayerVisibilitySystem::DrawnShape> drawn = visibility.drawnVisible(entity);
			if (!drawn.has_value()) {
				continue;
			}
			ColonistState&	 state = states[entity];
			state.frame			   = frameNumber;
			const glm::dvec2 eyeMm{static_cast<double>(position.value.x) * 1000.0, static_cast<double>(position.value.y) * 1000.0};
			const double	 walkedMm = state.placed ? glm::length(eyeMm - state.lastPosition) : 0.0;
			state.placed			  = true;
			state.lastPosition		  = eyeMm;

			ColonistCover cover{.colonist = entity, .bright = drawn->outline, .rim = drawn->rim};
			const std::vector<DirectedEdge> rim = rimEdgesOf(*drawn);
			cover.rimSides						= rim.size();
			appendFeather(rim, featherMm, cover.feather);

			if (const vision::SightFeed* feed = visibility.feedOf(entity)) {
				if (const std::optional<vision::SightFeed::SideOffsets> offsets = feed->drawnOffsets()) {
					if (state.hasOffsets) {
						for (std::size_t side = 0; side < offsets->size(); ++side) {
							cover.rimMovedMm = std::max(cover.rimMovedMm, std::abs((*offsets)[side] - state.lastOffsets[side]));
						}
					}
					state.lastOffsets.assign(offsets->begin(), offsets->end());
					state.hasOffsets = true;
				}
			}

			const std::optional<ecs::VisionSystem::SightBodies> bodies = vision->sightBodies(entity);
			const std::optional<ecs::VisionSystem::SightShape>	shape  = vision->sightShape(entity);
			bool shadowsRebuilt = false;
			if (bodies.has_value() && shape.has_value() && bodies->bodies != nullptr && bodies->shadowEdges != nullptr) {
				double brightReachMm = 0.0;
				for (const geometry::Vec2i64& point : drawn->outline) {
					brightReachMm = std::max(brightReachMm, glm::length(toDouble(point) - eyeMm));
				}
				state.shadows.update(*bodies, shape->buildRevision, eyeMm, walkedMm, brightReachMm, tunables);
				cover.shadows  = &state.shadows;
				shadowsRebuilt = state.shadows.counts().rebuilt;
				frameCounts.shadowRebuilds += shadowsRebuilt ? 1 : 0;
			}
			// His pieces go up as meshes, each again only when it changes: the feather with his bright shape,
			// the shadows when they are rebuilt.
			const auto uploadStart = std::chrono::steady_clock::now();
			if (!state.featherUploaded || state.lastBright != drawn->outline) {
				retire(state.featherMeshes);
				for (const renderer::TessellatedMesh& mesh : valueMeshes(cover.feather)) {
					state.featherMeshes.push_back(Renderer::Primitives::uploadInstancedMesh(mesh, 1));
				}
				state.lastBright	  = drawn->outline;
				state.featherUploaded = true;
				++frameCounts.meshUploads;
			}
			if (cover.shadows == nullptr) {
				retire(state.shadowMeshes);
			} else if (shadowsRebuilt) {
				retire(state.shadowMeshes);
				for (const renderer::TessellatedMesh& mesh : valueMeshes(cover.shadows->pieces())) {
					state.shadowMeshes.push_back(Renderer::Primitives::uploadInstancedMesh(mesh, 1));
				}
				++frameCounts.meshUploads;
			}
			frameCounts.uploadMicroseconds += microsecondsSince(uploadStart);
			cover.featherMeshes = &state.featherMeshes;
			cover.shadowMeshes	= &state.shadowMeshes;
			frameCounts.featherPieces += cover.feather.size();
			if (cover.shadows != nullptr) {
				frameCounts.softBodies += cover.shadows->softBodies().size();
				frameCounts.shadowEdges += cover.shadows->edges().size();
				frameCounts.shadowPieces += cover.shadows->pieces().size();
			}
			frameColonists.push_back(std::move(cover));
		}
		for (auto it = states.begin(); it != states.end();) {
			if (it->second.frame != frameNumber) {
				retire(it->second.featherMeshes);
				retire(it->second.shadowMeshes);
				it = states.erase(it);
			} else {
				++it;
			}
		}
		frameCounts.colonists = frameColonists.size();
	}

	void VisibilityCoverRenderer::render(
		ecs::World&				  world,
		const world::WorldCamera& camera,
		int						  viewportWidth,
		int						  viewportHeight,
		float					  pixelsPerMeter
	) {
		++frameNumber;
		frameCounts = Counts{};
		frameTint	= TintShape{};
		while (!retired.empty() && frameNumber - retired.front().first >= kRetiredMeshFrames) {
			retired.pop_front();
		}
		ecs::PlayerVisibilitySystem* visibility = world.tryGetSystem<ecs::PlayerVisibilitySystem>();
		frameLayers.drawn						= visibility != nullptr && !visibility->revealed();
		if (!frameLayers.drawn) {
			frameLayers.visibleOutlines.clear();
			frameLayers.pendingOutlines.clear();
			frameColonists.clear();
			return;
		}
		if (uploadedEpoch != visibility->epoch()) {
			upload(visibility->seenArea());
		}
		const ecs::VisionSystem* vision = world.tryGetSystem<ecs::VisionSystem>();
		const std::int64_t		 featherMm =
			  vision != nullptr ? vision->sightTunables().sightFeatherMm : vision::SightTunables{}.sightFeatherMm;

		const Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, pixelsPerMeter);
		const ScreenMap		   map{
			   .originMm	= {static_cast<double>(visibleRect.x) * 1000.0, static_cast<double>(visibleRect.y) * 1000.0},
			   .pixelsPerMm = static_cast<double>(pixelsPerMeter) * static_cast<double>(camera.zoom()) / 1000.0
		};
		const geometry::Vec2i64 viewLow{std::llround(static_cast<double>(visibleRect.x) * 1000.0), std::llround(static_cast<double>(visibleRect.y) * 1000.0)};
		const geometry::Vec2i64 viewHigh{
			std::llround(static_cast<double>(visibleRect.x + visibleRect.width) * 1000.0), std::llround(static_cast<double>(visibleRect.y + visibleRect.height) * 1000.0)
		};

		const auto seenStart = std::chrono::steady_clock::now();
		changed.clear();
		visibility->takeChangedBoxes(changed);
		feather.update(visibility->seenArea(), changed, viewLow, viewHigh, featherMm);
		frameCounts.seenMicroseconds = microsecondsSince(seenStart);

		const auto buildStart = std::chrono::steady_clock::now();
		gather(world, *visibility, camera, viewportWidth, viewportHeight, pixelsPerMeter);
		frameCounts.buildMicroseconds = microsecondsSince(buildStart);

		const auto			   composeStart = std::chrono::steady_clock::now();
		const Foundation::Rect view{
			-kCoverOverhangPixels,
			-kCoverOverhangPixels,
			static_cast<float>(viewportWidth) + 2.0F * kCoverOverhangPixels,
			static_cast<float>(viewportHeight) + 2.0F * kCoverOverhangPixels
		};
		const Primitives::WorldCamera meshCamera{
			.position = {camera.position().x, camera.position().y}, .zoom = camera.zoom(), .pixelsPerMeter = pixelsPerMeter
		};

		// Every point the masks name, reserved whole before any span into it is taken.
		std::size_t pointCount = 4;
		for (const ColonistCover& cover : frameColonists) {
			pointCount += cover.bright.size();
		}
		for (const std::vector<glm::dvec2>& grown : feather.pendingGrown()) {
			pointCount += grown.size();
		}
		for (const std::vector<geometry::Vec2i64>* shape : feather.pendingInView()) {
			pointCount += shape->size();
		}
		ScreenPoints screen;
		screen.reserve(pointCount);

		// The tint (7.5): the view at 1, then a group a colonist, darkened in, holding his bright shape at 0
		// and over it his feather and drawn shadows, each clipped to his bright shape.
		const std::array<Foundation::Vec2, 4> viewCorners = {
			Foundation::Vec2{view.x, view.y}, Foundation::Vec2{view.x + view.width, view.y}, Foundation::Vec2{view.x + view.width, view.y + view.height},
			Foundation::Vec2{view.x, view.y + view.height}
		};
		std::vector<Primitives::MaskElement> tint = {
			{.points = screen.add(std::vector<Foundation::Vec2>(viewCorners.begin(), viewCorners.end())), .paint = Primitives::MaskPaint::solid(1.0F)}
		};
		std::vector<std::array<Primitives::MaskLayer, 1>> insideBrightShapes;
		insideBrightShapes.reserve(frameColonists.size());
		for (const ColonistCover& cover : frameColonists) {
			std::vector<glm::dvec2> brightMm;
			brightMm.reserve(cover.bright.size());
			for (const geometry::Vec2i64& point : cover.bright) {
				brightMm.push_back(toDouble(point));
			}
			const std::span<const Foundation::Vec2> bright = screen.add(brightMm, map);
			frameLayers.visibleOutlines.emplace_back(bright.begin(), bright.end());
			insideBrightShapes.push_back({{{.image = Primitives::MaskImage::polygon(bright)}}});
			Primitives::MaskElement group{.blend = Primitives::MixBlendMode::Darken};
			group.children.push_back({.points = bright, .paint = Primitives::MaskPaint::solid(0.0F)});
			for (const std::vector<Renderer::InstancedMeshHandle>* uploaded : {cover.featherMeshes, cover.shadowMeshes}) {
				for (const Renderer::InstancedMeshHandle& mesh : *uploaded) {
					group.children.push_back(
						{.mesh = {mesh, meshCamera}, .mask = insideBrightShapes.back(), .blend = Primitives::MixBlendMode::Lighten}
					);
				}
			}
			frameCounts.tintElements += group.children.size();
			frameTint.groups += 1;
			frameTint.everyGroupDarkens = frameTint.everyGroupDarkens && group.blend == Primitives::MixBlendMode::Darken;
			for (std::size_t child = 1; child < group.children.size(); ++child) {
				frameTint.everyPieceInsideItsBrightShape = frameTint.everyPieceInsideItsBrightShape &&
														   group.children[child].mask.data() == insideBrightShapes.back().data() &&
														   group.children[child].blend == Primitives::MixBlendMode::Lighten;
			}
			tint.push_back(std::move(group));
		}

		// The unseen fill (7.5): the record's cells and the pending pieces, combined by lighten, over the
		// view less the record and the pending shapes. The cells give way to the pending pieces inside the
		// pending shapes grown by the feather: each cell is masked to outside those its pieces can reach,
		// and a cell that reaches none draws whole.
		std::vector<std::span<const Foundation::Vec2>>	 grownOnScreen;
		std::vector<std::pair<glm::dvec2, glm::dvec2>> grownBoxes;
		for (const std::vector<glm::dvec2>& grown : feather.pendingGrown()) {
			glm::dvec2 low	= grown.front();
			glm::dvec2 high = grown.front();
			for (const glm::dvec2 point : grown) {
				low	 = glm::min(low, point);
				high = glm::max(high, point);
			}
			grownBoxes.emplace_back(low, high);
			grownOnScreen.push_back(screen.add(grown, map));
		}
		const double						reachMm = static_cast<double>(featherMm);
		std::vector<std::vector<Primitives::MaskLayer>> outsidePendingFeather;
		outsidePendingFeather.reserve(feather.cellsInView().size());
		std::vector<Primitives::MaskElement> unseenFeather;
		for (const SeenFeather::Cell* cell : feather.cellsInView()) {
			const glm::dvec2 cellLow  = toDouble(cell->low) - glm::dvec2{reachMm};
			const glm::dvec2 cellHigh = toDouble(cell->high) + glm::dvec2{reachMm};
			std::vector<Primitives::MaskLayer>& mask = outsidePendingFeather.emplace_back();
			for (std::size_t grown = 0; grown < grownBoxes.size(); ++grown) {
				const auto& [low, high] = grownBoxes[grown];
				if (low.x <= cellHigh.x && cellLow.x <= high.x && low.y <= cellHigh.y && cellLow.y <= high.y) {
					if (mask.empty()) {
						mask.push_back({.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract});
					}
					mask.push_back({.image = Primitives::MaskImage::polygon(grownOnScreen[grown])});
				}
			}
			for (const Renderer::InstancedMeshHandle& mesh : cell->meshes) {
				unseenFeather.push_back({.mesh = {mesh, meshCamera}, .mask = mask, .blend = Primitives::MixBlendMode::Lighten});
			}
		}
		for (const Renderer::InstancedMeshHandle* mesh : feather.pendingMeshes()) {
			unseenFeather.push_back({.mesh = {*mesh, meshCamera}, .blend = Primitives::MixBlendMode::Lighten});
		}
		frameCounts.unseenElements = unseenFeather.size();

		std::vector<Primitives::MaskLayer> unseenMask;
		if (!unseenFeather.empty()) {
			unseenMask.push_back({.image = Primitives::MaskImage::svgMask(unseenFeather)});
		}
		unseenMask.push_back({.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract});
		for (const Renderer::InstancedMeshHandle& mesh : meshes) {
			unseenMask.push_back({.image = Primitives::MaskImage::mesh(mesh, meshCamera)});
		}
		for (const std::vector<geometry::Vec2i64>* shape : feather.pendingInView()) {
			std::vector<glm::dvec2> outline;
			outline.reserve(shape->size());
			for (const geometry::Vec2i64& point : *shape) {
				outline.push_back(toDouble(point));
			}
			const std::span<const Foundation::Vec2> pending = screen.add(outline, map);
			frameLayers.pendingOutlines.emplace_back(pending.begin(), pending.end());
			unseenMask.push_back({.image = Primitives::MaskImage::polygon(pending)});
		}

		// Batched draws still pending would sort with the cover by key; everything before it is under it.
		Primitives::flush();
		const std::array<Primitives::MaskLayer, 1> tintMask = {{{.image = Primitives::MaskImage::svgMask(tint)}}};
		Primitives::drawRect(
			{.bounds = view, .style = {.fill = {0.0F, 0.0F, 0.0F, kSeenDimAlpha}}, .mask = tintMask, .id = "visibility-tint", .key = kCoverTintKey}
		);
		Primitives::drawRect(
			{.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .id = "visibility-unseen", .key = kCoverUnseenKey}
		);
		frameCounts.composeMicroseconds = microsecondsSince(composeStart);
	}

} // namespace worldview
