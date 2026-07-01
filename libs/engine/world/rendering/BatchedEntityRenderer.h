#pragma once

// BatchedEntityRenderer - CPU batching fallback path.
// All entities are transformed on the CPU into one geometry buffer and drawn in
// a single call. Used when GPU instancing is disabled (A/B testing and fallback).

#include "assets/placement/PlacementExecutor.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/rendering/InstancingUniforms.h"
#include "world/rendering/RenderContext.h"
#include "world/rendering/TemplateMeshCache.h"
#include "world/rendering/WorldDepthSort.h"

#include <cstdint>
#include <math/Types.h>
#include <graphics/Color.h>
#include <vector>

namespace renderer {
struct TessellatedMesh;
}

namespace engine::world {

/// Renders entities via CPU batching (fallback / A-B path). Merges every visible
/// static + dynamic entity, sorts by ground-contact anchorY for the same 2.5D
/// ordering as the instanced path, and emits one draw call.
class BatchedEntityRenderer {
  public:
	// uniforms is unused here (CPU batching needs no shader uniforms) but kept in
	// the signature so all render paths share one shape; the orchestrator can call
	// them uniformly.
	void render(const RenderContext& ctx, InstancingUniforms& uniforms, RenderStats& stats);

  private:
	/// Transform one entity's mesh into screen-space triangles appended to the
	/// per-frame buffers; advances vertexIndex by the mesh's vertex count.
	void appendEntityTriangles(
		const assets::PlacedEntity&		 entity,
		const renderer::TessellatedMesh* mesh,
		float camX, float camY, float scale, float halfViewW, float halfViewH,
		uint32_t& vertexIndex
	);

	// Memoized template meshes (keyed by defName)
	TemplateMeshCache m_templateCache;

	// Shared gather/sort of the merged entity stream (one path with the instanced renderer).
	WorldDepthGather		   m_gather;
	std::vector<DepthSortItem> m_sorted;

	// Per-frame geometry buffers (reused each frame)
	std::vector<Foundation::Vec2> m_vertices;
	std::vector<Foundation::Color> m_colors;
	std::vector<uint16_t> m_indices;
};

}  // namespace engine::world
