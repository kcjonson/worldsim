#pragma once

// BatchedEntityRenderer - CPU batching fallback path.
// All entities are transformed on the CPU into one geometry buffer and drawn in
// a single call. Used when GPU instancing is disabled (A/B testing and fallback).

#include "assets/placement/PlacementExecutor.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/rendering/InstancingUniforms.h"
#include "world/rendering/RenderContext.h"
#include "world/rendering/TemplateMeshCache.h"

#include <cstdint>
#include <math/Types.h>
#include <graphics/Color.h>
#include <vector>

namespace engine::world {

/// Renders entities via CPU batching (original implementation).
class BatchedEntityRenderer {
  public:
	// uniforms is unused here (CPU batching needs no shader uniforms) but kept in
	// the signature so all render paths share one shape; the orchestrator can call
	// them uniformly.
	void render(const RenderContext& ctx, InstancingUniforms& uniforms, RenderStats& stats);

  private:
	// Memoized template meshes (keyed by defName)
	TemplateMeshCache m_templateCache;

	// Per-frame geometry buffers (reused each frame)
	std::vector<Foundation::Vec2> m_vertices;
	std::vector<Foundation::Color> m_colors;
	std::vector<uint16_t> m_indices;
};

}  // namespace engine::world
