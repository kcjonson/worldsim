#pragma once

// The player visibility cover (player-visibility.md 3.4, sight-softening.md 7): two masked fills over
// the view after the world is drawn. The tint darkens everything out of every player colonist's sight;
// the unseen fill, opaque over it, hides everything outside the seen record and its pending shapes.
// Both edges fade inside the brighter area, and each colonist's soft bodies and opaque shadow edges
// draw their triangles, bands, and fins: vector pieces carrying values, combined by salal's CSS mask
// with partial coverage (docs/masks.md). Nothing is rendered to a texture, and nothing drawn is read
// by the sim.

#include "DrawnShadows.h"
#include "SeenFeather.h"

#include "world/camera/WorldCamera.h"
#include "world/chunk/ChunkCoordinate.h"

#include "assets/placement/DrawBound.h"

#include <ecs/EntityID.h>
#include <vision/SeenArea.h>

#include <core/Vec2i64.h>

#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <math/Types.h>
#include <primitives/InstanceData.h>
#include <vector/Types.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
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

	// How far each box of the draw bound is grown, in screen pixels: past a pixel the record's edge
	// crosses, and a thing's anti-aliased fringe.
	constexpr float kDrawBoundMarginPixels = 2.0F;

	// The committed record's triangles as meshes in world meters, each within kSeenMeshVertexLimit.
	std::vector<renderer::TessellatedMesh> seenMeshes(const std::vector<vision::SeenArea::Triangle>& triangles);

	// What this frame can show, for drawing to cull against: with the cover drawn, the seen boxes in
	// view (PlayerVisibilitySystem::appendSeenBoxes), each grown by kDrawBoundMarginPixels, since
	// everything else is under the opaque fill; with reveal on, or no player visibility, everywhere.
	assets::DrawBound coverDrawBound(ecs::World& world, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float pixelsPerMeter);

	// kDrawBoundMarginPixels in world meters at the camera's zoom.
	[[nodiscard]] float drawBoundMarginMeters(const world::WorldCamera& camera, float pixelsPerMeter);

	// Whether the camera loads the chunk at `coord` (player-visibility.md 3.9): whether the draw bound
	// could ever hold anything of it, so whether seen ground lies within the chunk's square grown by the
	// reach the draw side culls with (entityCullMarginMeters of the registry's reach) and the draw
	// margin. A chunk with no seen ground of its own loads when its crowns can overhang seen ground
	// next door. Every chunk with reveal on, or no player visibility.
	[[nodiscard]] bool cameraLoadsChunk(ecs::World& world, const world::WorldCamera& camera, float pixelsPerMeter, world::ChunkCoordinate coord);

	class VisibilityCoverRenderer {
	  public:
		// One frame's outlines, in screen pixels: each colonist's bright shape and each pending shape.
		struct Layers {
			bool									   drawn = false;
			std::vector<std::vector<Foundation::Vec2>> visibleOutlines;
			std::vector<std::vector<Foundation::Vec2>> pendingOutlines;
		};

		// One colonist in view this frame: his bright shape and its rim sides, his feather and drawn
		// shadows in world millimetres, and how far his eased rim moved since the last frame.
		struct ColonistCover {
			ecs::EntityID					   colonist = ecs::kInvalidEntity;
			std::vector<geometry::Vec2i64>	   bright;
			std::vector<bool>				   rim;		 // per side of `bright`, whether it is rim
			std::size_t						   rimSides = 0;
			std::vector<ValuePolygon>		   feather;
			const DrawnShadows*				   shadows	   = nullptr;
			// The feather and the drawn shadows as uploaded.
			const std::vector<Renderer::InstancedMeshHandle>* featherMeshes = nullptr;
			const std::vector<Renderer::InstancedMeshHandle>* shadowMeshes	= nullptr;
			std::int64_t					   rimMovedMm  = 0; // the most any side of the eased rim moved since the last frame
		};

		// What the frame did, for the cover's state view (sight-softening.md 12).
		struct Counts {
			std::size_t colonists		= 0;
			std::size_t featherPieces	= 0;
			std::size_t softBodies		= 0;
			std::size_t shadowEdges		= 0;
			std::size_t shadowPieces	= 0;
			std::size_t shadowRebuilds	= 0; // colonists whose drawn shadows were rebuilt this frame
			std::size_t meshUploads		= 0; // feathers and drawn shadows uploaded this frame
			std::size_t tintElements	= 0; // elements of the tint's mask
			std::size_t unseenElements	= 0;
			double		buildMicroseconds	= 0.0; // building every colonist's feather and drawn shadows, and uploading them
			double		uploadMicroseconds	= 0.0; // of which uploading
			double		seenMicroseconds	= 0.0; // the seen feather's cells and pending cuts
			double		composeMicroseconds = 0.0; // the masks, from the pieces to the two draws
		};

		// How the last frame's tint was composed (7.5): one group a colonist in view, each darkened in, and
		// every piece in it lightened in and clipped to its colonist's bright shape.
		struct TintShape {
			std::size_t groups							= 0;
			bool		everyGroupDarkens				= true;
			bool		everyPieceInsideItsBrightShape	= true;
		};

		// Brings the uploaded record up to date when its epoch has moved, builds the frame's pieces and
		// draws the two fills. Nothing is drawn or uploaded while the player visibility system is revealed.
		void render(ecs::World& world, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float pixelsPerMeter);

		// The last frame's outlines, colonists, and counts.
		[[nodiscard]] const Layers&						layers() const { return frameLayers; }
		[[nodiscard]] const std::vector<ColonistCover>& colonists() const { return frameColonists; }
		[[nodiscard]] const Counts&						counts() const { return frameCounts; }
		[[nodiscard]] const TintShape&					tintShape() const { return frameTint; }
		[[nodiscard]] const SeenFeather&				seenFeather() const { return feather; }
		// The record's meshes as uploaded, and how many times the record has been uploaded.
		[[nodiscard]] std::size_t	meshCount() const { return meshes.size(); }
		[[nodiscard]] std::uint64_t uploads() const { return uploadCount; }

	  private:
		// What the cover keeps of a colonist between frames.
		struct ColonistState {
			DrawnShadows	  shadows;
			bool			  placed = false;
			glm::dvec2		  lastPosition{0.0};
			bool			  hasOffsets = false;
			std::vector<std::int64_t> lastOffsets;
			std::uint64_t	  frame = 0;
			bool			  featherUploaded = false;
			std::vector<geometry::Vec2i64> lastBright;
			std::vector<Renderer::InstancedMeshHandle> featherMeshes;
			std::vector<Renderer::InstancedMeshHandle> shadowMeshes;
		};

		void upload(const vision::SeenArea& seen);
		void retire(std::vector<Renderer::InstancedMeshHandle>& uploaded);
		void gather(
			ecs::World&						   world,
			ecs::PlayerVisibilitySystem&	   visibility,
			const world::WorldCamera&		   camera,
			int								   viewportWidth,
			int								   viewportHeight,
			float							   pixelsPerMeter
		);

		std::vector<Renderer::InstancedMeshHandle>		   meshes;
		std::optional<std::uint64_t>					   uploadedEpoch;
		std::uint64_t									   uploadCount = 0;
		std::uint64_t									   frameNumber = 0;
		Layers											   frameLayers;
		std::vector<ColonistCover>						   frameColonists;
		Counts											   frameCounts;
		TintShape										   frameTint;
		std::map<ecs::EntityID, ColonistState>			   states;
		SeenFeather										   feather;
		std::vector<vision::SeenArea::Triangle>			   triangles;
		std::vector<vision::SeenArea::Box>				   changed;
		// Replaced meshes, by the frame they were replaced, until the GPU is surely done with them.
		std::deque<std::pair<std::uint64_t, std::vector<Renderer::InstancedMeshHandle>>> retired;
	};

} // namespace worldview
