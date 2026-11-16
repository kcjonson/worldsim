#pragma once

// InstancedEntityRenderer - dynamic ECS entities via GPU instancing.
// Dynamic entities (from ECS) change position each frame, so we rebuild their
// instance batches per frame and draw one batch per run of one template mesh. Shared
// mesh geometry (VBO/IBO) is uploaded once per template and detail level and reused.

#include "assets/DetailLevels.h"
#include "assets/placement/PlacementExecutor.h"
#include "assets/placement/RenderContext.h"
#include "assets/placement/WorldDepthSort.h"
#include "primitives/InstanceData.h"
#include "world/rendering/RenderStats.h"
#include "world/rendering/RunPlanner.h"
#include "world/rendering/SelectionOutline.h"
#include "world/rendering/TemplateLevelBuilder.h"

#include <graphics/Color.h>
#include <math/Types.h>

#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace renderer {
struct TessellatedMesh;
}

namespace worldview {

/// Emits the Y-sorted upright stream (tall static occluders + dynamic ECS
/// entities) so that every pair of entities whose drawn areas can overlap is
/// submitted in depth order, except two that are both a few pixels across.
/// Non-animated entities are gathered into runs of one template mesh, each one
/// GPU-instanced draw, which a RunPlanner picks: entities that cannot touch reorder
/// freely, so variants interleaved by depth still share a draw. Animated colonists
/// (per-part deformed) and the selection outline draw on the CPU path at their
/// sorted position, after every run opened before them.
///
/// A template whose def declares <detailLevels> draws at the level the camera's density
/// picks (assets::detailLevelFor). The registry holds the reference level's mesh, uploaded the
/// first time its template is drawn; the others are built on request by a TemplateLevelBuilder
/// off this thread, uploaded here within kUploadVertexBudget a frame, and until a level is
/// uploaded its template draws the nearest one that is. A level not drawn for kEvictAfterFrames is released.
class InstancedEntityRenderer {
  public:
	/// What the last frame drew at which detail level, for /api/state?what=uprights.
	struct LevelStats {
		struct Use {
			float	 pixelsPerMeter = 0.0F; // the level's density
			uint32_t instances = 0;
		};
		float			 screenPixelsPerMeter = 0.0F;
		std::vector<Use> uses;				// instances of leveled templates, by the level they drew
		uint32_t		 pendingRequests = 0; // levels wanted and not yet uploaded
		uint32_t		 residentLevels = 0;  // non-reference level meshes on the GPU
		size_t			 residentBytes = 0;   // and their vertex, index, and instance buffer bytes
	};

	/// Emit the pre-sorted upright stream in order (ascending anchorY).
	void emitSorted(const std::vector<assets::DepthSortItem>& items, const assets::RenderContext& ctx, RenderStats& stats);

	/// Set (or clear) the selected entity's outline for this frame. Injected into
	/// the depth-sort stream at its anchorY so nearer entities occlude it.
	void setSelectionOutline(SelectionOutline o) { selectionOutline = std::move(o); }

	[[nodiscard]] const LevelStats& levelStats() const { return lastLevelStats; }

  private:
	// Instance room a mesh handle starts with. A run that outgrows its handle re-uploads
	// it at the next power of two that holds the run, so a template drawn a few times a
	// frame costs 128 KB and one carpeting a zoomed-out view grows to fit.
	static constexpr uint32_t kInitialInstanceCapacity = 4096;

	// An entity whose screen bounds are at most this many pixels on their longer side is small,
	// and two small entities may draw in either order: they share a few pixels at most, and
	// which of two specks of foliage is on top is not something a player can pick out. Order
	// against anything larger is kept. Measured on the river view at zoom 0.25 (trees about
	// 2 px across): 154 of 5.3M pixels changed against exact ordering, and upright draws fell
	// from 6,505 to 550. Trees grew 2.25x since, so the same trees at that zoom are about 5 to
	// 8 px across now; the bound grew with them.
	static constexpr float kUnorderedFootprintPixels = 8.0F;

	// Non-reference level vertices uploaded a frame; the first waiting mesh always goes, however large.
	// A level 80 tree is about 20,000, the reference level about 5,000.
	static constexpr uint32_t kUploadVertexBudget = 131072;

	// A non-reference level not drawn for this many frames (about 30 s at 60 fps) is released.
	static constexpr uint64_t kEvictAfterFrames = 1800;

	enum class SlotState : uint8_t {
		Absent,	   // not built
		Requested, // with the level builder
		Waiting,   // built, waiting for an upload
		Ready,	   // on the GPU
		Failed,	   // the build or upload failed (logged); never retried
	};

	// GPU mesh per (template id, detail level): the SHARED geometry (VBO/IBO) every instance
	// draws, the room in its instance buffer, and the mesh's local bounds. Each handle
	// releases its buffers when its slot is released or the vector is destroyed.
	struct MeshSlot {
		Renderer::InstancedMeshHandle			 handle;
		const renderer::TessellatedMesh*		 registryMesh = nullptr; // the reference level's, which the registry keeps
		std::optional<renderer::TessellatedMesh> builtMesh;				 // any other level's, built for this renderer
		uint32_t								 capacity = 0;
		uint32_t								 triangles = 0;
		Foundation::Vec2						 boundsMin{0.0F, 0.0F};
		Foundation::Vec2						 boundsMax{0.0F, 0.0F};
		float									 radius = 0.0F; // furthest vertex from the origin, for a rotated instance
		uint64_t								 lastDrawnFrame = 0;
		uint64_t								 wantedFrame = 0;
		SlotState								 state = SlotState::Absent;

		[[nodiscard]] const renderer::TessellatedMesh* mesh() const { return builtMesh.has_value() ? &*builtMesh : registryMesh; }
	};
	std::vector<MeshSlot> meshSlots;

	[[nodiscard]] static uint32_t slotIndex(uint32_t templateId, uint8_t level) {
		return (templateId * static_cast<uint32_t>(assets::kMaxDetailLevels)) + level;
	}

	static constexpr uint32_t kNoUse = UINT32_MAX;
	static constexpr uint32_t kNoSlot = UINT32_MAX;

	// Per template id: its registry mesh (the reference level's) and def's detail levels, which
	// levels are on the GPU, and what this frame settled for it once, at its first instanced
	// entity: the slot it draws, that mesh's bounds, and its LevelStats::uses entry.
	struct TemplateLevels {
		const renderer::TessellatedMesh* mesh = nullptr; // the registry's; null: nothing to draw
		std::span<const float>			 densities;		 // empty: one level, 0
		uint8_t							 reference = 0;
		uint8_t							 readyMask = 0; // bit i: level i is Ready
		bool							 resolved = false;
		uint64_t						 frame = 0;
		uint32_t						 drawSlot = kNoSlot; // kNoSlot: no level is ready
		Foundation::Vec2				 boundsMin{0.0F, 0.0F};
		Foundation::Vec2				 boundsMax{0.0F, 0.0F};
		float							 radius = 0.0F;
		uint32_t						 use = kNoUse;
	};
	std::vector<TemplateLevels> templateLevels;

	TemplateLevelBuilder levelBuilder;
	std::deque<uint32_t> uploadQueue; // slot indices in Waiting, oldest first
	uint64_t			 frameIndex = 0;
	LevelStats			 lastLevelStats;

	// This frame's runs, by the index the planner gave them; the planner says the order they draw in.
	struct Run {
		uint32_t							slot = 0;
		std::vector<Renderer::InstanceData> instances;
	};
	std::vector<Run> runs;
	uint32_t		 runCount = 0;
	RunPlanner		 planner;

	// Per-frame CPU geometry for animated entities (per-part deformed, so not GPU-instanceable):
	// their parts are transformed on the CPU and flushed at their sorted position.
	std::vector<Foundation::Vec2>  animVertices;
	std::vector<Foundation::Color> animColors;
	std::vector<uint16_t>		   animIndices;

	// The selected entity's outline for this frame (valid=false when nothing
	// entity-backed is selected). Drawn at its anchorY inside emitSorted.
	SelectionOutline selectionOutline;

	/// A template's mesh and detail levels, read from the registry the first time.
	TemplateLevels& levelsOf(uint32_t templateId);

	/// Settles a template's level for this frame, at its first instanced entity: asks for the level
	/// the camera's density picks and the reference level, and draws the nearest one that is ready.
	void settleLevel(uint32_t templateId, TemplateLevels& levels, LevelStats& levelStats);

	/// The slot of a template's level, made on first use.
	MeshSlot& slotAt(uint32_t index);

	/// Asks for a template's level this frame: the reference level is the registry's mesh, uploaded
	/// at once; another goes to the level builder until it comes back.
	void want(uint32_t templateId, uint8_t level);

	/// Uploads a slot's mesh and marks its level ready, or logs the failure and marks it Failed.
	void upload(uint32_t index);

	/// Frees a slot's GPU mesh and built mesh, leaving it in `state` and its level not ready.
	void release(uint32_t index, SlotState state);

	/// Takes the level builder's finished meshes, then uploads waiting meshes within the budget.
	void collectAndUpload();

	/// Releases the non-reference levels not drawn for kEvictAfterFrames, and counts the rest.
	void evictStale();

	/// Append one animated entity's per-part-deformed, screen-space triangles to
	/// the CPU batch (anim*). animVertexBase tracks the batch's 16-bit index base.
	void emitAnimated(
		const assets::PlacedEntity&		 entity,
		const renderer::TessellatedMesh* mesh,
		float camX, float camY, float scale, float halfViewW, float halfViewH,
		uint32_t& animVertexBase
	);
};

}  // namespace worldview
