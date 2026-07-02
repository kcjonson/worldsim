#pragma once

// InstancedEntityRenderer - dynamic ECS entities via GPU instancing.
// Dynamic entities (from ECS) change position each frame, so we rebuild their
// instance batches per frame and draw one batch per mesh type. Shared mesh
// geometry (VBO/IBO) is uploaded once per defName and reused.

#include "assets/placement/PlacementExecutor.h"
#include "primitives/InstanceData.h"
#include "world/rendering/InstancingUniforms.h"
#include "world/rendering/RenderContext.h"
#include "world/rendering/TemplateMeshCache.h"
#include "world/rendering/WorldDepthSort.h"

#include <graphics/Color.h>
#include <math/Types.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace renderer {
struct TessellatedMesh;
}

namespace engine::world {

/// Emits the Y-sorted upright stream (tall static occluders + dynamic ECS
/// entities) in the order the caller supplies, so submission order == depth
/// order. Consecutive same-defName non-animated entities are coalesced into one
/// GPU-instanced draw; animated colonists (per-part deformed) break the run and
/// flush at their sorted position on the CPU triangle path.
class InstancedEntityRenderer {
  public:
	~InstancedEntityRenderer();

	/// Emit the pre-sorted upright stream in order (ascending anchorY).
	void emitSorted(const std::vector<DepthSortItem>& items, const RenderContext& ctx, RenderStats& stats);

  private:
	// Maximum instances per mesh type for GPU instancing
	// Set high enough to handle extreme zoom-out scenarios (observed 34k+ entities)
	static constexpr uint32_t kMaxInstancesPerMesh = 50000;

	// Cache for GPU mesh handles (keyed by defName)
	// These hold the SHARED mesh geometry (VBO/IBO) that all chunks reference
	std::unordered_map<std::string, Renderer::InstancedMeshHandle> m_meshHandles;

	// Current run of consecutive same-defName non-animated entities, drawn as one
	// drawInstanced call and flushed whenever the defName changes or an animated
	// entity interleaves (preserving depth order).
	std::vector<Renderer::InstanceData> m_runInstances;
	std::string							m_runDefName;

	// Memoized template meshes (keyed by defName)
	TemplateMeshCache m_templateCache;

	// Per-frame CPU geometry for animated entities (per-part deformed, so not GPU-instanceable):
	// their parts are transformed on the CPU and flushed at their sorted position.
	std::vector<Foundation::Vec2>  m_animVertices;
	std::vector<Foundation::Color> m_animColors;
	std::vector<uint16_t>		   m_animIndices;

	/// Get or create GPU mesh handle for a template
	Renderer::InstancedMeshHandle& getOrCreateMeshHandle(
		const std::string& defName,
		const renderer::TessellatedMesh* mesh
	);

	/// Append one animated entity's per-part-deformed, screen-space triangles to
	/// the CPU batch (m_anim*). animVertexBase tracks the batch's 16-bit index base.
	void emitAnimated(
		const assets::PlacedEntity&		 entity,
		const renderer::TessellatedMesh* mesh,
		float camX, float camY, float scale, float halfViewW, float halfViewH,
		uint32_t& animVertexBase
	);
};

}  // namespace engine::world
