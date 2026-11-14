#pragma once

// The player visibility cover (player-visibility.md 3.4): two masked fills over the view after the
// world is drawn. The tint darkens everything out of every player colonist's sight; the unseen fill,
// opaque over it, hides everything outside the seen record and its pending shapes. Hard vector
// edges, drawn through salal's CSS mask (docs/masks.md); nothing is rendered to a texture.

#include "world/camera/WorldCamera.h"

#include <vision/SeenArea.h>

#include <core/Vec2i64.h>

#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <math/Types.h>
#include <primitives/InstanceData.h>
#include <vector/Types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ecs {
	class World;
	class PlayerVisibilitySystem;
} // namespace ecs

namespace worldview {

	// How dark seen ground out of sight is: the tint's alpha.
	constexpr float kSeenDimAlpha = 0.5F;
	// The colour of unseen ground.
	constexpr Foundation::Color kUnseenColor{0.0F, 0.0F, 0.0F, 1.0F};

	// The most vertices one uploaded mesh holds: its indices are 16-bit.
	constexpr std::size_t kSeenMeshVertexLimit = 65535;

	// The cover's two fills, under the debug overlays (the lowest, the terrain polygon overlay, is
	// 40), the drawing preview (898 and up), the selection indicator and move marker (100) and the
	// placement ghost (1000) that are drawn after it in the same flush.
	constexpr std::int32_t kCoverTintKey = 30;
	constexpr std::int32_t kCoverUnseenKey = 31;

	// The committed record's triangles as meshes in world meters, each within kSeenMeshVertexLimit.
	std::vector<renderer::TessellatedMesh> seenMeshes(const std::vector<vision::SeenArea::Triangle>& triangles);

	class VisibilityCoverRenderer {
	  public:
		// One frame's mask layers, in screen pixels: what the tint leaves clear and what the unseen fill
		// leaves clear besides the record's meshes.
		struct Layers {
			bool									   drawn = false;
			std::vector<std::vector<Foundation::Vec2>> visibleOutlines;
			std::vector<std::vector<Foundation::Vec2>> pendingOutlines;
		};

		// Brings the uploaded record up to date when its epoch has moved, gathers the frame's layers and
		// draws the two fills. Nothing is drawn or uploaded while the player visibility system is revealed.
		void render(ecs::World& world, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float pixelsPerMeter);

		// The last frame's layers.
		[[nodiscard]] const Layers& layers() const { return frameLayers; }
		// The record's meshes as uploaded, and how many times the record has been uploaded.
		[[nodiscard]] std::size_t	meshCount() const { return meshes.size(); }
		[[nodiscard]] std::uint64_t uploads() const { return uploadCount; }

	  private:
		void upload(const vision::SeenArea& seen);
		void gather(
			ecs::World&						   world,
			const ecs::PlayerVisibilitySystem& visibility,
			const world::WorldCamera&		   camera,
			int								   viewportWidth,
			int								   viewportHeight,
			float							   pixelsPerMeter
		);

		std::vector<Renderer::InstancedMeshHandle>		   meshes;
		std::optional<std::uint64_t>					   uploadedEpoch;
		std::uint64_t									   uploadCount = 0;
		Layers											   frameLayers;
		std::vector<vision::SeenArea::Triangle>			   triangles;
		std::vector<const std::vector<geometry::Vec2i64>*> pending;
	};

} // namespace worldview
