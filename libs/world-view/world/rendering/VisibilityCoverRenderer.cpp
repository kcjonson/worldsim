#include "VisibilityCoverRenderer.h"

#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>

#include <primitives/MaskImage.h>
#include <primitives/Primitives.h>
#include <primitives/WorldCamera.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace worldview {

	namespace {

		// The fills reach this far past the view, so their own anti-aliased edges fall outside it.
		constexpr float kCoverOverhangPixels = 4.0F;

		constexpr std::size_t kTriangleVertices = 3;

		Foundation::Vec2 toWorldMeters(const geometry::Vec2i64& millimetres) {
			return {
				static_cast<float>(static_cast<double>(millimetres.x) / static_cast<double>(geometry::kMillimetersPerMeter)),
				static_cast<float>(static_cast<double>(millimetres.y) / static_cast<double>(geometry::kMillimetersPerMeter))
			};
		}

		// The outline in screen pixels, or empty when its bounds miss `view`.
		std::vector<Foundation::Vec2> onScreen(
			const std::vector<geometry::Vec2i64>& outline,
			const world::WorldCamera&			  camera,
			int									  viewportWidth,
			int									  viewportHeight,
			float								  pixelsPerMeter,
			const Foundation::Rect&				  view
		) {
			std::vector<Foundation::Vec2> screen;
			screen.reserve(outline.size());
			Foundation::Vec2 low{std::numeric_limits<float>::max()};
			Foundation::Vec2 high{std::numeric_limits<float>::lowest()};
			for (const geometry::Vec2i64& point : outline) {
				const Foundation::Vec2 meters = toWorldMeters(point);
				const Foundation::Vec2 pixel = camera.worldToScreen(meters.x, meters.y, viewportWidth, viewportHeight, pixelsPerMeter);
				low = {std::min(low.x, pixel.x), std::min(low.y, pixel.y)};
				high = {std::max(high.x, pixel.x), std::max(high.y, pixel.y)};
				screen.push_back(pixel);
			}
			if (screen.size() < 3 || high.x < view.x || high.y < view.y || low.x > view.x + view.width || low.y > view.y + view.height) {
				screen.clear();
			}
			return screen;
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

	void VisibilityCoverRenderer::gather(
		ecs::World&						   world,
		const ecs::PlayerVisibilitySystem& visibility,
		const world::WorldCamera&		   camera,
		int								   viewportWidth,
		int								   viewportHeight,
		float							   pixelsPerMeter
	) {
		const Foundation::Rect view{0.0F, 0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)};
		frameLayers.visibleOutlines.clear();
		frameLayers.pendingOutlines.clear();

		for (auto [entity, position, colonist, memory] : world.view<ecs::Position, ecs::Colonist, ecs::Memory>()) {
			// A colonist whose sight misses the view draws nothing in it, so his shape is never cut.
			const Foundation::Vec2 corner =
				camera.worldToScreen(position.value.x - memory.sightRadius, position.value.y - memory.sightRadius, viewportWidth, viewportHeight, pixelsPerMeter);
			const Foundation::Vec2 opposite =
				camera.worldToScreen(position.value.x + memory.sightRadius, position.value.y + memory.sightRadius, viewportWidth, viewportHeight, pixelsPerMeter);
			if (std::max(corner.x, opposite.x) < view.x || std::max(corner.y, opposite.y) < view.y || std::min(corner.x, opposite.x) > view.x + view.width ||
				std::min(corner.y, opposite.y) > view.y + view.height) {
				continue;
			}
			const std::optional<std::vector<geometry::Vec2i64>> outline = visibility.drawnVisible(entity);
			if (!outline.has_value()) {
				continue;
			}
			std::vector<Foundation::Vec2> screen = onScreen(*outline, camera, viewportWidth, viewportHeight, pixelsPerMeter, view);
			if (!screen.empty()) {
				frameLayers.visibleOutlines.push_back(std::move(screen));
			}
		}

		pending.clear();
		visibility.seenArea().appendPendingShapes(pending);
		for (const std::vector<geometry::Vec2i64>* shape : pending) {
			std::vector<Foundation::Vec2> screen = onScreen(*shape, camera, viewportWidth, viewportHeight, pixelsPerMeter, view);
			if (!screen.empty()) {
				frameLayers.pendingOutlines.push_back(std::move(screen));
			}
		}
	}

	void VisibilityCoverRenderer::render(
		ecs::World&				  world,
		const world::WorldCamera& camera,
		int						  viewportWidth,
		int						  viewportHeight,
		float					  pixelsPerMeter
	) {
		const ecs::PlayerVisibilitySystem* visibility = world.tryGetSystem<ecs::PlayerVisibilitySystem>();
		frameLayers.drawn = visibility != nullptr && !visibility->revealed();
		if (!frameLayers.drawn) {
			frameLayers.visibleOutlines.clear();
			frameLayers.pendingOutlines.clear();
			return;
		}
		if (uploadedEpoch != visibility->epoch()) {
			upload(visibility->seenArea());
		}
		gather(world, *visibility, camera, viewportWidth, viewportHeight, pixelsPerMeter);

		namespace Primitives = Renderer::Primitives;
		const Foundation::Rect view{
			-kCoverOverhangPixels,
			-kCoverOverhangPixels,
			static_cast<float>(viewportWidth) + 2.0F * kCoverOverhangPixels,
			static_cast<float>(viewportHeight) + 2.0F * kCoverOverhangPixels
		};
		const Primitives::WorldCamera meshCamera{
			.position = {camera.position().x, camera.position().y}, .zoom = camera.zoom(), .pixelsPerMeter = pixelsPerMeter
		};

		// Batched draws still pending would sort with the cover by key; everything before it is under it.
		Primitives::flush();

		std::vector<Primitives::MaskLayer> tintMask = {
			{.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract}
		};
		for (const std::vector<Foundation::Vec2>& outline : frameLayers.visibleOutlines) {
			tintMask.push_back({.image = Primitives::MaskImage::polygon(outline)});
		}
		Primitives::drawRect(
			{.bounds = view,
			 .style = {.fill = {0.0F, 0.0F, 0.0F, kSeenDimAlpha}},
			 .mask = tintMask,
			 .id = "visibility-tint",
			 .key = kCoverTintKey}
		);

		std::vector<Primitives::MaskLayer> unseenMask = {
			{.image = Primitives::MaskImage::rect(view), .composite = Primitives::MaskComposite::Subtract}
		};
		for (const Renderer::InstancedMeshHandle& mesh : meshes) {
			unseenMask.push_back({.image = Primitives::MaskImage::mesh(mesh, meshCamera)});
		}
		for (const std::vector<Foundation::Vec2>& outline : frameLayers.pendingOutlines) {
			unseenMask.push_back({.image = Primitives::MaskImage::polygon(outline)});
		}
		Primitives::drawRect(
			{.bounds = view, .style = {.fill = kUnseenColor}, .mask = unseenMask, .id = "visibility-unseen", .key = kCoverUnseenKey}
		);
	}

} // namespace worldview
