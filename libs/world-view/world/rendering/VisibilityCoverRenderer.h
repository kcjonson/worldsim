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

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <optional>
#include <vector>

namespace ecs {
	class World;
	class PlayerVisibilitySystem;
} // namespace ecs

namespace worldview {

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

	// Whether the window the cover draws into holds what its partial masks need: 8 alpha and 24 depth
	// bits (salal docs/masks.md, "Targets a mask draws into"). Without them every masked fill draws
	// nothing, so the game says so at start rather than showing everything revealed. Needs a GL context.
	[[nodiscard]] bool coverTargetHoldsPartialMasks();

	// The most soft-body pieces built in one frame over every colonist in view, in whole rings, the
	// most turned first (7.3): a ring is let in while its last count fits what is left, and the first always is.
	constexpr std::size_t kDrawnShadowPiecesPerFrame = 8000;
	// What a frame of waiting counts for against a due ring's turn: ten frames rank as a whole cadence.
	constexpr double kDueTurnAFrame = 0.01;

	class VisibilityCoverRenderer {
	  public:
		// What the last frame drew: whether the cover was drawn, and how many bright and pending shapes.
		struct Layers {
			bool		drawn		   = false;
			std::size_t brightShapes   = 0;
			std::size_t pendingShapes  = 0;
		};

		// One colonist in view this frame: his bright shape and its rim sides, his feather (in world
		// millimetres less `featherAnchor`, where he stands) and drawn shadows, and how far his eased rim
		// moved since the last frame.
		struct ColonistCover {
			ecs::EntityID						 colonist = ecs::kInvalidEntity;
			const std::vector<geometry::Vec2i64>* bright  = nullptr;
			std::size_t							 rimSides = 0;
			geometry::Vec2i64					 featherAnchor{0, 0};
			const std::vector<ValueTriangle>*	 feather = nullptr;
			const DrawnShadows*					 shadows = nullptr;
			// The feather and bands, about featherAnchor, and the soft triangles, a mesh set a ring, each about its anchor, as uploaded.
			const std::vector<Renderer::InstancedMeshHandle>* followMeshes = nullptr;
			const std::array<std::vector<Renderer::InstancedMeshHandle>, DrawnShadows::kSoftRings>* softMeshes  = nullptr;
			const std::array<geometry::Vec2i64, DrawnShadows::kSoftRings>*						  softAnchors = nullptr;
			std::int64_t									 rimMovedMm = 0; // the most any side of the eased rim moved since the last frame
		};

		// What the frame did, for the cover's state view (sight-softening.md 12).
		struct Counts {
			std::size_t colonists		= 0;
			std::size_t featherPieces	= 0; // pieces drawn this frame, by kind
			std::size_t softBodies		= 0;
			std::size_t shadowEdges		= 0;
			std::size_t softPieces		= 0;
			std::size_t bandPieces		= 0;
			std::size_t featherRebuilds = 0; // colonists whose feather or bands were built this frame
			std::size_t bandRebuilds	= 0;
			std::size_t softRingsBuilt	= 0; // colonists' rings of soft bodies built this frame
			std::size_t softRingsDue	= 0; // rings due, built or carried over
			std::size_t softBodiesBuilt = 0; // soft bodies those rings built
			std::uint64_t softOldestDueFrames = 0; // how many frames the longest due ring has waited
			double		  softMostTurnDegrees = 0.0; // how far past its cadence the most turned ring left due has turned, in degrees
			std::size_t softPiecesBuilt = 0; // of which soft bodies' pieces, against kDrawnShadowPiecesPerFrame
			std::size_t piecesBuilt		= 0; // pieces built this frame
			std::size_t meshUploads		= 0; // meshes uploaded this frame
			std::size_t tintElements	= 0; // elements of the tint's mask
			std::size_t unseenElements	= 0;
			double		buildMicroseconds	= 0.0; // building every colonist's feather and drawn shadows, and uploading them
			double		uploadMicroseconds	= 0.0; // of which uploading
			double		brightMicroseconds	= 0.0; // of which asking for each bright shape (PlayerVisibilitySystem::drawnVisible)
			double		seenMicroseconds	= 0.0; // the seen feather's cells and pending cuts
			double		composeMicroseconds = 0.0; // the masks, from the pieces to the two draws
		};

		// How the last frame's tint was composed (7.5): one group a colonist in view, each darkened in, and
		// every piece in it lightened in and clipped to its colonist's bright shape.
		struct TintShape {
			std::size_t groups						   = 0;
			bool		everyGroupDarkens			   = true;
			bool		everyPieceInsideItsBrightShape = true;
		};

		// Brings the uploaded record up to date when its epoch has moved, builds the frame's pieces and
		// draws the two fills. Nothing is drawn or uploaded while the player visibility system is revealed.
		// `frameSeconds` is the time since the last frame.
		void render(ecs::World& world, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float pixelsPerMeter, float frameSeconds);

		// The last frame's layers, colonists, and counts.
		[[nodiscard]] const Layers&						layers() const { return frameLayers; }
		[[nodiscard]] const std::vector<ColonistCover>& colonists() const { return frameColonists; }
		[[nodiscard]] const Counts&						counts() const { return frameCounts; }
		[[nodiscard]] const TintShape&					tintShape() const { return frameTint; }
		[[nodiscard]] const SeenFeather&				seenFeather() const { return feather; }
		// The record's meshes as uploaded, and how many times the record has been uploaded.
		[[nodiscard]] std::size_t	meshCount() const { return meshes.size(); }
		[[nodiscard]] std::uint64_t uploads() const { return uploadCount; }

	  private:
		// What the cover keeps of a colonist while he lives, in view or not: his drawn shadows, his
		// bright shape and feather as built about where he stands (on a leg, once the ease has ended, it
		// moves with him unchanged), and their meshes, each uploaded again only when its pieces change.
		struct ColonistState {
			DrawnShadows							   shadows;
			bool									   placed = false;
			glm::dvec2								   lastPosition{0.0};
			bool									   hasOffsets = false;
			std::vector<std::int64_t>				   lastOffsets;
			std::vector<geometry::Vec2i64>			   bright;
			std::vector<bool>						   rim;
			bool									   featherBuilt	  = false;
			std::int64_t							   featherWidthMm = 0;
			std::vector<geometry::Vec2i64>			   featherShape; // the bright outline less where he stands
			std::vector<bool>						   featherRim;
			std::vector<ValueTriangle>				   featherPieces;
			std::vector<Renderer::InstancedMeshHandle> followMeshes; // his feather and bands, about where he stands
			std::array<std::vector<Renderer::InstancedMeshHandle>, DrawnShadows::kSoftRings> softMeshes;
			std::array<geometry::Vec2i64, DrawnShadows::kSoftRings>						  softAnchors{};
		};
		// One colonist in view, between the frame's passes.
		struct InView {
			ecs::EntityID								  entity = ecs::kInvalidEntity;
			ColonistState*								  state	 = nullptr;
			geometry::Vec2i64							  standing{0, 0};
			bool										  featherChanged = false;
			bool										  hasBodies		 = false;
		};
		struct DueBody {
			DrawnShadows::Due due;
			std::size_t		  colonist = 0; // into the frame's colonists in view
		};

		void upload(const vision::SeenArea& seen);
		void retire(std::vector<Renderer::InstancedMeshHandle>& uploaded);
		void uploadPieces(std::span<const ValueTriangle> pieces, glm::dvec2 anchorMm, std::vector<Renderer::InstancedMeshHandle>& into);
		void gather(
			ecs::World&						   world,
			ecs::PlayerVisibilitySystem&	   visibility,
			const world::WorldCamera&		   camera,
			int								   viewportWidth,
			int								   viewportHeight,
			float							   pixelsPerMeter,
			float							   frameSeconds
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
		// Reused each frame.
		std::vector<InView>					inView;
		std::vector<DrawnShadows::Due>		dueOfOne;
		std::vector<DueBody>				due;
		std::vector<ValueTriangle>			pieceScratch;
		std::vector<geometry::Vec2i64>		shapeScratch;
		std::vector<DirectedEdge>			edgeScratch;
		std::vector<Foundation::Vec2>		screenPoints;
	};

} // namespace worldview
