#pragma once

// GhostRenderer - Renders a semi-transparent preview of an entity during placement.
// Uses the asset registry to get the tessellated mesh and renders it with alpha.

#include <graphics/Color.h>
#include <math/Types.h>
#include <world/camera/WorldCamera.h>

#include <string>
#include <vector>

namespace renderer {
struct TessellatedMesh;
}

namespace world_sim {

/// Renders a ghost preview of an entity at a given world position.
/// Used during placement mode to show where the entity will be placed.
class GhostRenderer {
  public:
	GhostRenderer() = default;

	/// Render the ghost at the given world position
	/// @param defName Asset definition name to render
	/// @param worldPos World position (center of entity)
	/// @param camera World camera for coordinate transforms
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	/// @param isValid Whether the placement position is valid (affects tint color)
	void render(const std::string& defName,
				Foundation::Vec2 worldPos,
				const engine::world::WorldCamera& camera,
				int viewportWidth,
				int viewportHeight,
				bool isValid = true);

  private:
	// Per-frame buffers for transformed mesh (reused to avoid allocations)
	std::vector<Foundation::Vec2> m_transformedVertices;
	std::vector<Foundation::Color> m_ghostColors;
};

} // namespace world_sim
