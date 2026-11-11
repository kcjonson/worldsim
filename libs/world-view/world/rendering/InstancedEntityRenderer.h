#pragma once

// InstancedEntityRenderer - dynamic ECS entities via GPU instancing.
// Dynamic entities (from ECS) change position each frame, so we rebuild their
// instance batches per frame and draw one batch per run of one template. Shared
// mesh geometry (VBO/IBO) is uploaded once per template id and reused.

#include "assets/placement/PlacementExecutor.h"
#include "assets/placement/RenderContext.h"
#include "assets/placement/WorldDepthSort.h"
#include "primitives/InstanceData.h"
#include "world/rendering/RenderStats.h"
#include "world/rendering/SelectionOutline.h"
#include "world/rendering/TemplateMeshCache.h"

#include <graphics/Color.h>
#include <math/Types.h>

#include <cstdint>
#include <vector>

namespace renderer {
struct TessellatedMesh;
}

namespace worldview {

/// Emits the Y-sorted upright stream (tall static occluders + dynamic ECS
/// entities) so that every pair of entities whose drawn areas can overlap is
/// submitted in depth order. Non-animated entities are gathered into runs of one
/// template, each one GPU-instanced draw: an entity joins its template's latest
/// open run unless something in a later run may overlap it, so entities that
/// cannot touch reorder freely and variants interleaved by depth still share a
/// draw. Animated colonists (per-part deformed) and the selection outline draw on
/// the CPU path at their sorted position, after every run opened before them.
class InstancedEntityRenderer {
  public:
	/// Emit the pre-sorted upright stream in order (ascending anchorY).
	void emitSorted(const std::vector<assets::DepthSortItem>& items, const assets::RenderContext& ctx, RenderStats& stats);

	/// Set (or clear) the selected entity's outline for this frame. Injected into
	/// the depth-sort stream at its anchorY so nearer entities occlude it.
	void setSelectionOutline(SelectionOutline o) { m_selectionOutline = std::move(o); }

  private:
	// Instance room a mesh handle starts with. A run that outgrows its handle re-uploads
	// it at the next power of two that holds the run, so a template drawn a few times a
	// frame costs 128 KB and one carpeting a zoomed-out view grows to fit.
	static constexpr uint32_t kInitialInstanceCapacity = 4096;

	// The overlap grid's side is at most this many cells; its cells are never smaller
	// than the mean entity, so a typical entity covers a few cells.
	static constexpr uint32_t kMaxOverlapGridSide = 256;

	// GPU mesh per template id: the SHARED geometry (VBO/IBO) every instance of the
	// template draws, the room in its instance buffer, and the mesh's local bounds.
	// Each handle releases its buffers when the vector is destroyed.
	struct MeshSlot {
		Renderer::InstancedMeshHandle handle;
		uint32_t					  capacity = 0;
		uint32_t					  triangles = 0;
		Foundation::Vec2			  boundsMin{0.0F, 0.0F};
		Foundation::Vec2			  boundsMax{0.0F, 0.0F};
		float						  radius = 0.0F;	// furthest vertex from the origin, for a rotated instance
		bool						  uploaded = false; // tried once; a failed upload is not retried every frame
	};
	std::vector<MeshSlot> m_meshSlots;

	// One entity of the stream, classified and, when instanced, bounded in world space.
	enum class ItemKind : uint8_t { Skip, Animated, Instanced };
	struct ItemBounds {
		Foundation::Vec2 min{0.0F, 0.0F};
		Foundation::Vec2 max{0.0F, 0.0F};
		ItemKind		 kind = ItemKind::Skip;
	};
	std::vector<ItemBounds> m_itemBounds;

	// This frame's runs, drawn in index order. Runs from m_firstOpenRun on are open:
	// not yet drawn, so a later entity can still join one.
	struct Run {
		uint32_t							templateId = 0;
		std::vector<Renderer::InstanceData> instances;
	};
	std::vector<Run> m_runs;
	uint32_t		 m_runCount = 0;
	uint32_t		 m_firstOpenRun = 0;

	// Per template id, 1 + the index of its latest run this frame (0: none yet).
	std::vector<uint32_t> m_latestRunOfTemplate;

	// World-space grid over this frame's instanced entities: per cell, 1 + the index of
	// the latest run holding an entity whose bounds touch the cell (0: none).
	std::vector<uint32_t> m_overlapGrid;

	// Memoized template meshes (by template id)
	TemplateMeshCache m_templateCache;

	// Per-frame CPU geometry for animated entities (per-part deformed, so not GPU-instanceable):
	// their parts are transformed on the CPU and flushed at their sorted position.
	std::vector<Foundation::Vec2>  m_animVertices;
	std::vector<Foundation::Color> m_animColors;
	std::vector<uint16_t>		   m_animIndices;

	// The selected entity's outline for this frame (valid=false when nothing
	// entity-backed is selected). Drawn at its anchorY inside emitSorted.
	SelectionOutline m_selectionOutline;

	/// The GPU mesh of a template, uploaded on first use with kInitialInstanceCapacity.
	MeshSlot& meshSlot(uint32_t templateId, const renderer::TessellatedMesh& mesh);

	/// Append one animated entity's per-part-deformed, screen-space triangles to
	/// the CPU batch (m_anim*). animVertexBase tracks the batch's 16-bit index base.
	void emitAnimated(
		const assets::PlacedEntity&		 entity,
		const renderer::TessellatedMesh* mesh,
		float camX, float camY, float scale, float halfViewW, float halfViewH,
		uint32_t& animVertexBase
	);
};

}  // namespace worldview
