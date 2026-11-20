#include "VisibilityCoverRenderer.h"

#include "assets/AssetRegistry.h"
#include "assets/placement/RenderContext.h"

#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <ecs/systems/VisionSystem.h>
#include <nav/NavCoords.h>

#include <primitives/MaskImage.h>
#include <primitives/Primitives.h>
#include <primitives/WorldCamera.h>

#include <GL/glew.h>
#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <span>
#include <utility>

namespace worldview {

	namespace {

		namespace Primitives = Renderer::Primitives;

		// The fills reach this far past the view, so their own anti-aliased edges fall outside it.
		constexpr float kCoverOverhangPixels = 4.0F;

		constexpr std::size_t kTriangleVertices = 3;

		// How many frames a replaced mesh is kept before its buffers go.
		constexpr std::uint64_t kRetiredMeshFrames = 3;

		// What the partial masks need of the window (salal docs/masks.md).
		constexpr GLint kAlphaBitsNeeded = 8;
		constexpr GLint kDepthBitsNeeded = 24;

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

		// The points appended to `screen`, which the caller reserved for every point of the frame, so the
		// spans the mask elements hold stay put until the draws read them.
		template <typename Point> std::span<const Foundation::Vec2> addOnScreen(std::vector<Foundation::Vec2>& screen, std::span<const Point> points, const ScreenMap& map) {
			const std::size_t first = screen.size();
			for (const Point& point : points) {
				if constexpr (std::is_same_v<Point, geometry::Vec2i64>) {
					screen.push_back(map(toDouble(point)));
				} else {
					screen.push_back(map(point));
				}
			}
			return {screen.data() + first, points.size()};
		}

		// A shape's rim sides as edges with its inside on the left, into `out`.
		void appendRimEdges(std::span<const geometry::Vec2i64> outline, const std::vector<bool>& rim, std::vector<DirectedEdge>& out) {
			const std::size_t count	  = outline.size();
			double			  doubled = 0.0;
			for (std::size_t corner = 0; corner < count; ++corner) {
				doubled += geometry::cross(outline[corner], outline[(corner + 1) % count]).toDouble();
			}
			for (std::size_t side = 0; side < count; ++side) {
				if (rim[side]) {
					const geometry::Vec2i64& from = outline[side];
					const geometry::Vec2i64& to	  = outline[(side + 1) % count];
					out.emplace_back(doubled >= 0.0 ? from : to, doubled >= 0.0 ? to : from);
				}
			}
		}

	} // namespace

	bool coverTargetHoldsPartialMasks() {
		// The window's own framebuffer, whatever target is bound when this is asked: the cover draws there.
		GLint bound = 0;
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &bound);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		GLint alpha = 0;
		GLint depth = 0;
		glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_BACK_LEFT, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &alpha);
		glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_DEPTH, GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depth);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(bound));
		return alpha >= kAlphaBitsNeeded && depth >= kDepthBitsNeeded;
	}

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

	void VisibilityCoverRenderer::uploadPieces(std::span<const ValueTriangle> pieces, glm::dvec2 anchorMm, std::vector<Renderer::InstancedMeshHandle>& into) {
		retire(into);
		const auto start = std::chrono::steady_clock::now();
		for (const renderer::TessellatedMesh& mesh : valueMeshes(pieces, anchorMm)) {
			into.push_back(Renderer::Primitives::uploadInstancedMesh(mesh, 1));
			++frameCounts.meshUploads;
		}
		frameCounts.uploadMicroseconds += microsecondsSince(start);
	}

	// Each colonist in view: his bright shape and the feather on its rim sides, built about where he
	// stands and uploaded with his bands when either changes; then his due soft bodies, over every
	// colonist in view, the longest due first, up to kDrawnShadowPiecesPerFrame pieces.
	void VisibilityCoverRenderer::gather(
		ecs::World&					 world,
		ecs::PlayerVisibilitySystem& visibility,
		const world::WorldCamera&	 camera,
		int							 viewportWidth,
		int							 viewportHeight,
		float						 pixelsPerMeter
	) {
		const Foundation::Rect view{0.0F, 0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)};
		frameColonists.clear();
		inView.clear();
		const ecs::VisionSystem* vision = world.tryGetSystem<ecs::VisionSystem>();
		if (vision == nullptr) {
			return;
		}
		const vision::SightTunables& tunables	 = vision->sightTunables();
		const double				 featherMm	 = static_cast<double>(tunables.sightFeatherMm);
		const double				 pixelsPerMm = static_cast<double>(pixelsPerMeter) * static_cast<double>(camera.zoom()) / 1000.0;

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
			const auto											   brightStart = std::chrono::steady_clock::now();
			const ecs::PlayerVisibilitySystem::DrawnShape* const drawn		 = visibility.drawnVisible(entity);
			frameCounts.brightMicroseconds += microsecondsSince(brightStart);
			if (drawn == nullptr) {
				continue;
			}
			ColonistState&			state	 = states[entity];
			const geometry::Vec2i64 standing = nav::toMm(position.value);
			const glm::dvec2		eyeMm	 = toDouble(standing);
			const double			walkedMm = state.placed ? glm::length(eyeMm - state.lastPosition) : 0.0;
			state.placed					 = true;
			state.lastPosition				 = eyeMm;
			state.bright.assign(drawn->outline.begin(), drawn->outline.end());
			state.rim.assign(drawn->rim.begin(), drawn->rim.end());

			// The feather: the same shape about where he stands needs nothing new, and moves with him.
			shapeScratch.clear();
			for (const geometry::Vec2i64& point : state.bright) {
				shapeScratch.push_back(point - standing);
			}
			InView colonistInView{.entity = entity, .state = &state, .standing = standing};
			if (!state.featherBuilt || shapeScratch != state.featherShape || state.rim != state.featherRim || state.featherWidthMm != tunables.sightFeatherMm) {
				edgeScratch.clear();
				appendRimEdges(shapeScratch, state.rim, edgeScratch);
				state.featherPieces.clear();
				appendFeather(edgeScratch, featherMm, state.featherPieces);
				std::swap(state.featherShape, shapeScratch);
				state.featherRim				= state.rim;
				state.featherBuilt				= true;
				state.featherWidthMm			= tunables.sightFeatherMm;
				colonistInView.featherChanged	= true;
				frameCounts.piecesBuilt += state.featherPieces.size();
			}

			ColonistCover cover{.colonist = entity, .bright = &state.bright, .featherAnchor = standing, .feather = &state.featherPieces};
			cover.rimSides = static_cast<std::size_t>(std::ranges::count(state.rim, true));
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

			const std::optional<ecs::VisionSystem::SightShape>	sight  = vision->sightShape(entity);
			const std::optional<ecs::VisionSystem::SightBodies> bodies = vision->sightBodies(entity);
			if (sight.has_value() && bodies.has_value() && bodies->bodies != nullptr && bodies->shadowEdges != nullptr) {
				double brightReachMm = 0.0;
				for (const geometry::Vec2i64& point : state.bright) {
					brightReachMm = std::max(brightReachMm, glm::length(toDouble(point) - eyeMm));
				}
				state.shadows.update(*bodies, sight->buildRevision, eyeMm, walkedMm, state.bright, brightReachMm, pixelsPerMm, tunables, frameNumber);
				colonistInView.hasBodies = true;
				cover.shadows			 = &state.shadows;
			}
			inView.push_back(colonistInView);
			frameColonists.push_back(cover);
		}

		// The due rings of soft bodies, the longest due first, up to the budget.
		due.clear();
		for (std::size_t index = 0; index < inView.size(); ++index) {
			if (!inView[index].hasBodies) {
				continue;
			}
			dueOfOne.clear();
			inView[index].state->shadows.appendDue(dueOfOne);
			for (const DrawnShadows::Due& body : dueOfOne) {
				due.push_back({.due = body, .colonist = index});
			}
		}
		std::ranges::sort(due, [](const DueBody& first, const DueBody& second) { return first.due.since < second.due.since; });
		frameCounts.softRingsDue		= due.size();
		frameCounts.softOldestDueFrames = due.empty() ? 0 : frameNumber - due.front().due.since;
		std::size_t budget				= kDrawnShadowPiecesPerFrame;
		for (const DueBody& body : due) {
			if (body.due.pieces > budget && frameCounts.softRingsBuilt > 0) {
				break;
			}
			const std::size_t built = inView[body.colonist].state->shadows.build(body.due.ring);
			frameCounts.softPiecesBuilt += built;
			frameCounts.piecesBuilt += built;
			budget -= std::min(budget, built);
			++frameCounts.softRingsBuilt;
		}

		for (std::size_t index = 0; index < inView.size(); ++index) {
			const InView&  colonist = inView[index];
			ColonistState& state	= *colonist.state;
			ColonistCover& cover	= frameColonists[index];
			const bool	   bands	= colonist.hasBodies && state.shadows.counts().bandsRebuilt;
			for (std::size_t ring = 0; ring < DrawnShadows::kSoftRings; ++ring) {
				if (!colonist.hasBodies) {
					retire(state.softMeshes[ring]);
				} else if ((state.shadows.counts().softChangedRings & (1U << ring)) != 0U) {
					state.softAnchors[ring] = colonist.standing;
					uploadPieces(state.shadows.rings()[ring].pieces, toDouble(colonist.standing), state.softMeshes[ring]);
				}
			}
			// His feather and his bands, about where he stands, go up together: both change on the frames he
			// walks, and a band he stands still beside changes on none.
			if (colonist.featherChanged || bands) {
				pieceScratch.assign(state.featherPieces.begin(), state.featherPieces.end());
				if (colonist.hasBodies) {
					const glm::dvec2 standing = toDouble(colonist.standing);
					for (ValueTriangle piece : state.shadows.bandPieces()) {
						for (glm::dvec2& point : piece.points) {
							point -= standing;
						}
						pieceScratch.push_back(piece);
					}
				}
				uploadPieces(pieceScratch, {0.0, 0.0}, state.followMeshes);
				++frameCounts.featherRebuilds;
			}
			frameCounts.bandRebuilds += bands ? 1 : 0;
			frameCounts.piecesBuilt += bands ? state.shadows.bandPieces().size() : 0;
			cover.followMeshes = &state.followMeshes;
			cover.softMeshes   = &state.softMeshes;
			cover.softAnchors  = &state.softAnchors;
			frameCounts.featherPieces += state.featherPieces.size();
			if (colonist.hasBodies) {
				frameCounts.softBodies += state.shadows.softBodyCount();
				frameCounts.softBodiesBuilt += state.shadows.counts().bodiesBuilt;
				frameCounts.shadowEdges += state.shadows.edges().size();
				frameCounts.softPieces += state.shadows.softPieceCount();
				frameCounts.bandPieces += state.shadows.bandPieces().size();
			}
		}

		// A colonist's pieces are kept while he lives, in view or out of it, so coming back into view
		// builds nothing and no ease starts again.
		for (auto it = states.begin(); it != states.end();) {
			if (world.getComponent<ecs::Colonist>(it->first) == nullptr) {
				retire(it->second.followMeshes);
				for (std::vector<Renderer::InstancedMeshHandle>& ring : it->second.softMeshes) {
					retire(ring);
				}
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
		frameLayers = Layers{};
		while (!retired.empty() && frameNumber - retired.front().first >= kRetiredMeshFrames) {
			retired.pop_front();
		}
		ecs::PlayerVisibilitySystem* visibility = world.tryGetSystem<ecs::PlayerVisibilitySystem>();
		frameLayers.drawn						= visibility != nullptr && !visibility->revealed();
		if (!frameLayers.drawn) {
			frameColonists.clear();
			return;
		}
		if (uploadedEpoch != visibility->epoch()) {
			upload(visibility->seenArea());
		}
		const ecs::VisionSystem* vision = world.tryGetSystem<ecs::VisionSystem>();
		const std::int64_t featherMm = vision != nullptr ? vision->sightTunables().sightFeatherMm : vision::SightTunables{}.sightFeatherMm;

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
		// A camera moved by `anchor`, which draws a mesh built about it where it belongs.
		const auto cameraAbout = [&](const geometry::Vec2i64& anchor) {
			return Primitives::WorldCamera{
				.position	   = {static_cast<float>(static_cast<double>(camera.position().x) - static_cast<double>(anchor.x) / 1000.0),
							  static_cast<float>(static_cast<double>(camera.position().y) - static_cast<double>(anchor.y) / 1000.0)},
				.zoom		   = camera.zoom(),
				.pixelsPerMeter = pixelsPerMeter
			};
		};
		const Primitives::WorldCamera meshCamera = cameraAbout({0, 0});

		// Every point the masks name, reserved whole before any span into it is taken.
		std::size_t pointCount = 4;
		for (const ColonistCover& cover : frameColonists) {
			pointCount += cover.bright->size();
		}
		for (const SeenFeather::Cluster* cluster : feather.clustersInView()) {
			for (const std::vector<glm::dvec2>& grown : cluster->grown) {
				pointCount += grown.size();
			}
		}
		for (const std::vector<geometry::Vec2i64>* shape : feather.pendingInView()) {
			pointCount += shape->size();
		}
		screenPoints.clear();
		screenPoints.reserve(pointCount);

		// The tint (7.5): the view at 1, then a group a colonist, darkened in, holding his bright shape at 0
		// and over it his feather and drawn shadows, each clipped to his bright shape.
		const std::array<Foundation::Vec2, 4> viewCorners = {
			Foundation::Vec2{view.x, view.y}, Foundation::Vec2{view.x + view.width, view.y}, Foundation::Vec2{view.x + view.width, view.y + view.height},
			Foundation::Vec2{view.x, view.y + view.height}
		};
		const std::size_t viewFirst = screenPoints.size();
		screenPoints.insert(screenPoints.end(), viewCorners.begin(), viewCorners.end());
		std::vector<Primitives::MaskElement> tint = {
			{.points = std::span<const Foundation::Vec2>(screenPoints.data() + viewFirst, viewCorners.size()), .paint = Primitives::MaskPaint::solid(1.0F)}
		};
		std::vector<std::array<Primitives::MaskLayer, 1>> insideBrightShapes;
		insideBrightShapes.reserve(frameColonists.size());
		for (const ColonistCover& cover : frameColonists) {
			const std::span<const Foundation::Vec2> bright = addOnScreen<geometry::Vec2i64>(screenPoints, *cover.bright, map);
			insideBrightShapes.push_back({{{.image = Primitives::MaskImage::polygon(bright)}}});
			Primitives::MaskElement group{.blend = Primitives::MixBlendMode::Darken};
			group.children.push_back({.points = bright, .paint = Primitives::MaskPaint::solid(0.0F)});
			const Primitives::WorldCamera followCamera = cameraAbout(cover.featherAnchor);
			for (const Renderer::InstancedMeshHandle& mesh : *cover.followMeshes) {
				group.children.push_back({.mesh = {mesh, followCamera}, .mask = insideBrightShapes.back(), .blend = Primitives::MixBlendMode::Lighten});
			}
			for (std::size_t ring = 0; ring < DrawnShadows::kSoftRings; ++ring) {
				const Primitives::WorldCamera softCamera = cameraAbout((*cover.softAnchors)[ring]);
				for (const Renderer::InstancedMeshHandle& mesh : (*cover.softMeshes)[ring]) {
					group.children.push_back({.mesh = {mesh, softCamera}, .mask = insideBrightShapes.back(), .blend = Primitives::MixBlendMode::Lighten});
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
		frameLayers.brightShapes = frameColonists.size();

		// The unseen fill (7.5): the record's cells and the pending pieces, combined by lighten, over the
		// view less the record and the pending shapes. The cells give way to the pending pieces inside the
		// pending shapes grown by the feather: each cell is masked to outside those its pieces can reach,
		// and a cell that reaches none draws whole.
		struct GrownOnScreen {
			std::span<const Foundation::Vec2> points;
			glm::dvec2						  low{0.0};
			glm::dvec2						  high{0.0};
		};
		std::vector<GrownOnScreen> grownShapes;
		for (const SeenFeather::Cluster* cluster : feather.clustersInView()) {
			for (const std::vector<glm::dvec2>& grown : cluster->grown) {
				GrownOnScreen shape{.points = addOnScreen<glm::dvec2>(screenPoints, grown, map), .low = grown.front(), .high = grown.front()};
				for (const glm::dvec2 point : grown) {
					shape.low  = glm::min(shape.low, point);
					shape.high = glm::max(shape.high, point);
				}
				grownShapes.push_back(shape);
			}
		}
		const double									reachMm = static_cast<double>(featherMm);
		std::vector<std::vector<Primitives::MaskLayer>> outsidePendingFeather;
		outsidePendingFeather.reserve(feather.cellsInView().size());
		std::vector<Primitives::MaskElement> unseenFeather;
		for (const SeenFeather::Cell* cell : feather.cellsInView()) {
			const glm::dvec2					cellLow	 = toDouble(cell->low) - glm::dvec2{reachMm};
			const glm::dvec2					cellHigh = toDouble(cell->high) + glm::dvec2{reachMm};
			std::vector<Primitives::MaskLayer>& mask	 = outsidePendingFeather.emplace_back();
			for (const GrownOnScreen& grown : grownShapes) {
				if (grown.low.x <= cellHigh.x && cellLow.x <= grown.high.x && grown.low.y <= cellHigh.y && cellLow.y <= grown.high.y) {
					if (mask.empty()) {
						mask.push_back({.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract});
					}
					mask.push_back({.image = Primitives::MaskImage::polygon(grown.points)});
				}
			}
			const Primitives::WorldCamera cellCamera = cameraAbout(cell->anchor);
			for (const Renderer::InstancedMeshHandle& mesh : cell->meshes) {
				unseenFeather.push_back({.mesh = {mesh, cellCamera}, .mask = mask, .blend = Primitives::MixBlendMode::Lighten});
			}
		}
		for (const SeenFeather::Cluster* cluster : feather.clustersInView()) {
			const Primitives::WorldCamera clusterCamera = cameraAbout(cluster->anchor);
			for (const Renderer::InstancedMeshHandle& mesh : cluster->meshes) {
				unseenFeather.push_back({.mesh = {mesh, clusterCamera}, .blend = Primitives::MixBlendMode::Lighten});
			}
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
			unseenMask.push_back({.image = Primitives::MaskImage::polygon(addOnScreen<geometry::Vec2i64>(screenPoints, *shape, map))});
		}
		frameLayers.pendingShapes = feather.pendingInView().size();

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
