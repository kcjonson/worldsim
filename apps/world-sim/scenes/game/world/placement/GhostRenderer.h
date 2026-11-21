#pragma once

// GhostRenderer - Renders a semi-transparent preview of an entity during placement.
// A parts item is drawn from its item view at the ghost's turn (assets::buildItemMesh, the builder the placed
// item's view comes from), any other def from its template, upright; both on the CPU batch with alpha. Under a parts
// item, its use areas, clearance, and collider (assets::itemOverlays, drawn by worldview::drawItemOverlays).

#include <assets/AssetDefinition.h>
#include <assets/ItemOverlays.h>
#include <graphics/Color.h>
#include <math/Types.h>
#include <vector/Types.h>
#include <world/camera/WorldCamera.h>

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace world_sim {

/// Renders a ghost preview of an entity at a given world position.
/// Used during placement mode to show where the entity will be placed.
class GhostRenderer {
  public:
	GhostRenderer() = default;

	/// Starts a frame's ghosts: views drawn last frame are kept until endFrame, unless the asset registry has
	/// reloaded since they were built.
	void beginFrame();

	/// Render the ghost at the given world position
	/// @param defName Asset definition name to render
	/// @param worldPos World position (center of entity)
	/// @param turnQuanta The turn a parts item is drawn at; any other def is drawn upright
	/// @param camera World camera for coordinate transforms
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	/// @param isValid Whether the placement position is valid (affects tint color)
	void render(const std::string& defName,
				Foundation::Vec2 worldPos,
				uint32_t turnQuanta,
				const world::WorldCamera& camera,
				int viewportWidth,
				int viewportHeight,
				bool isValid = true);

	/// Drops the views no ghost drew this frame, so a drag through many turns keeps only the one it shows.
	void endFrame();

  private:
	struct HeldView {
		renderer::TessellatedMesh mesh;
		bool					  drawn = false;
	};
	// By (def, turn, level), under the registry generation they were built in.
	std::map<std::tuple<std::string, uint32_t, uint8_t>, HeldView> views;
	uint64_t													   viewsGeneration = 0;

	// The overlays at the turn drawn last, rebuilt when the def, the turn, or the registry changes.
	assets::ItemOverlays overlays;
	std::string			 overlaysDef;
	uint64_t			 overlaysGeneration = 0;

	void drawOverlays(const assets::AssetDefinition& def,
					  uint32_t turnQuanta,
					  Foundation::Vec2 worldPos,
					  const world::WorldCamera& camera,
					  int viewportWidth,
					  int viewportHeight,
					  bool isValid);

	void draw(const renderer::TessellatedMesh& mesh, Foundation::Vec2 offset, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, bool isValid);

	// Per-frame buffers for transformed mesh (reused to avoid allocations)
	std::vector<Foundation::Vec2> m_transformedVertices;
	std::vector<Foundation::Color> m_ghostColors;
};

} // namespace world_sim
