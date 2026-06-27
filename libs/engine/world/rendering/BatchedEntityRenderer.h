#pragma once

// BatchedEntityRenderer - CPU batching fallback path.
// All entities are transformed on the CPU into one geometry buffer and drawn in
// a single call. Used when GPU instancing is disabled (A/B testing and fallback).

#include "assets/placement/PlacementExecutor.h"
#include "world/camera/WorldCamera.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/rendering/InstancingUniforms.h"

#include <cstdint>
#include <math/Types.h>
#include <graphics/Color.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace renderer {
struct TessellatedMesh;
}

namespace engine::world {

/// Renders entities via CPU batching (original implementation).
class BatchedEntityRenderer {
  public:
	void render(
		const assets::PlacementExecutor&		   executor,
		const std::unordered_set<ChunkCoordinate>& processedChunks,
		const std::vector<assets::PlacedEntity>*   dynamicEntities,
		const WorldCamera&						   camera,
		int										   viewportWidth,
		int										   viewportHeight,
		float									   pixelsPerMeter,
		RenderStats&							   stats
	);

  private:
	// Cache for template meshes (keyed by defName)
	std::unordered_map<std::string, const renderer::TessellatedMesh*> m_templateCache;

	// Per-frame geometry buffers (reused each frame)
	std::vector<Foundation::Vec2> m_vertices;
	std::vector<Foundation::Color> m_colors;
	std::vector<uint16_t> m_indices;

	/// Get or cache a template mesh
	const renderer::TessellatedMesh* getTemplate(const std::string& defName);
};

}  // namespace engine::world
