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
#include "world/rendering/SelectionOutline.h"
#include "world/rendering/TemplateLevelBuilder.h"
#include "world/rendering/TemplateMeshCache.h"

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
/// submitted in depth order. Non-animated entities are gathered into runs of one
/// template mesh, each one GPU-instanced draw: an entity joins its mesh's latest
/// open run unless something in a later run may overlap it, so entities that
/// cannot touch reorder freely and variants interleaved by depth still share a
/// draw. Animated colonists (per-part deformed) and the selection outline draw on
/// the CPU path at their sorted position, after every run opened before them.
///
/// A template whose def declares <detailLevels> draws at the level the camera's density
/// picks (assets::detailLevelFor). The registry holds the reference level's mesh; the
/// others are built on request by a TemplateLevelBuilder off this thread, uploaded here
/// within kUploadVertexBudget a frame, and until a level is uploaded its template draws
/// the nearest one that is. A level not drawn for kEvictAfterFrames is released.
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
		size_t			 residentBytes = 0;   // and their vertex and index bytes
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

	// The overlap grid's side is at most this many cells; its cells are never smaller
	// than the mean entity, so a typical entity covers a few cells.
	static constexpr uint32_t kMaxOverlapGridSide = 256;

	// Mesh vertices uploaded a frame; the first waiting mesh always goes, however large.
	// A level 80 tree is about 20,000, the reference level about 5,000.
	static constexpr uint32_t kUploadVertexBudget = 131072;

	// A non-reference level not drawn for this many frames (about 30 s at 60 fps) is released.
	static constexpr uint64_t kEvictAfterFrames = 1800;

	enum class SlotState : uint8_t {
		Absent,	   // not built
		Requested, // with the level builder
		Waiting,   // built, waiting for an upload
		Ready,	   // on the GPU
		Failed,	   // the build or upload failed; never retried
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

	// Per template id: its def's detail levels and which are on the GPU.
	struct TemplateLevels {
		std::span<const float> densities; // empty: one level, 0
		uint8_t				   reference = 0;
		uint8_t				   readyMask = 0; // bit i: level i is Ready
		bool				   resolved = false;
	};
	std::vector<TemplateLevels> templateLevels;

	TemplateLevelBuilder levelBuilder;
	std::deque<uint32_t> uploadQueue; // slot indices in Waiting, oldest first
	uint64_t			 frameIndex = 0;
	LevelStats			 lastLevelStats;

	// One entity of the stream, classified and, when instanced, bounded in world space.
	enum class ItemKind : uint8_t { Skip, Animated, Instanced };
	struct ItemBounds {
		Foundation::Vec2 min{0.0F, 0.0F};
		Foundation::Vec2 max{0.0F, 0.0F};
		uint32_t		 slot = 0;	// the mesh slot it draws
		uint8_t			 wanted = 0; // the level the camera's density picks
		ItemKind		 kind = ItemKind::Skip;
	};
	std::vector<ItemBounds> itemBounds;

	// This frame's runs, drawn in index order. Runs from firstOpenRun on are open:
	// not yet drawn, so a later entity can still join one.
	struct Run {
		uint32_t							slot = 0;
		std::vector<Renderer::InstanceData> instances;
	};
	std::vector<Run> runs;
	uint32_t		 runCount = 0;
	uint32_t		 firstOpenRun = 0;

	// Per mesh slot, 1 + the index of its latest run this frame (0: none yet).
	std::vector<uint32_t> latestRunOfSlot;

	// World-space grid over this frame's instanced entities: per cell, 1 + the index of
	// the latest run holding an entity whose bounds touch the cell (0: none).
	std::vector<uint32_t> overlapGrid;

	// Memoized reference template meshes (by template id)
	TemplateMeshCache templateCache;

	// Per-frame CPU geometry for animated entities (per-part deformed, so not GPU-instanceable):
	// their parts are transformed on the CPU and flushed at their sorted position.
	std::vector<Foundation::Vec2>  animVertices;
	std::vector<Foundation::Color> animColors;
	std::vector<uint16_t>		   animIndices;

	// The selected entity's outline for this frame (valid=false when nothing
	// entity-backed is selected). Drawn at its anchorY inside emitSorted.
	SelectionOutline selectionOutline;

	/// A template's detail levels, read from the registry the first time.
	TemplateLevels& levelsOf(uint32_t templateId);

	/// The slot of a template's level, made on first use.
	MeshSlot& slotAt(uint32_t index);

	/// Asks for a template's level this frame: the reference level is the registry's mesh, queued
	/// for upload; another goes to the level builder until it comes back.
	void want(uint32_t templateId, uint8_t level, const renderer::TessellatedMesh& referenceMesh);

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
