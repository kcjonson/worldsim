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
/// entities) in the order the caller supplies, so submission order == depth
/// order. Consecutive same-template non-animated entities are coalesced into one
/// GPU-instanced draw; animated colonists (per-part deformed) break the run and
/// flush at their sorted position on the CPU triangle path.
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

	// GPU mesh per template id: the SHARED geometry (VBO/IBO) every instance of the
	// template draws, and the room in its instance buffer. Each handle releases its
	// buffers when the vector is destroyed.
	struct MeshSlot {
		Renderer::InstancedMeshHandle handle;
		uint32_t					  capacity = 0;
		bool						  uploaded = false; // tried once; a failed upload is not retried every frame
	};
	std::vector<MeshSlot> m_meshSlots;

	// Current run of consecutive same-template non-animated entities, drawn as one
	// drawInstanced call and flushed whenever the template changes or an animated
	// entity interleaves (preserving depth order).
	std::vector<Renderer::InstanceData> m_runInstances;
	uint32_t							m_runTemplateId = 0;

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
