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

/// Renders dynamic (per-frame) entities via GPU instancing.
class InstancedEntityRenderer {
  public:
	~InstancedEntityRenderer();

	/// Render dynamic entities (per-frame rebuild) via GPU instancing.
	void renderDynamic(const RenderContext& ctx, RenderStats& stats);

  private:
	// Maximum instances per mesh type for GPU instancing
	// Set high enough to handle extreme zoom-out scenarios (observed 34k+ entities)
	static constexpr uint32_t kMaxInstancesPerMesh = 50000;

	// Cache for GPU mesh handles (keyed by defName)
	// These hold the SHARED mesh geometry (VBO/IBO) that all chunks reference
	std::unordered_map<std::string, Renderer::InstancedMeshHandle> m_meshHandles;

	// Per-frame instance batches (grouped by mesh type, reused each frame)
	// Used ONLY for dynamic entities that change per-frame
	std::unordered_map<std::string, std::vector<Renderer::InstanceData>> m_instanceBatches;

	// Memoized template meshes (keyed by defName)
	TemplateMeshCache m_templateCache;

	// Per-frame CPU geometry for animated entities (per-part deformed, so not GPU-instanceable):
	// their parts are transformed on the CPU and drawn in one batch over the instanced entities.
	std::vector<Foundation::Vec2>  m_animVertices;
	std::vector<Foundation::Color> m_animColors;
	std::vector<uint16_t>		   m_animIndices;

	/// Get or create GPU mesh handle for a template
	Renderer::InstancedMeshHandle& getOrCreateMeshHandle(
		const std::string& defName,
		const renderer::TessellatedMesh* mesh
	);
};

}  // namespace engine::world
