#pragma once

// GhostRenderer - Renders a semi-transparent preview of an entity during placement.
// A parts item is drawn from its item view at the ghost's turn (assets::buildItemMesh, the builder the placed
// item's view comes from), any other def from its template, upright; both through worldview::drawItemView with alpha,
// tinted where placement is refused. Under a parts item, its use areas, clearance, and collider (assets::itemOverlays,
// drawn by worldview::drawItemOverlays).

#include <assets/ItemOverlays.h>
#include <math/Types.h>
#include <vector/Types.h>
#include <world/camera/WorldCamera.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

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
	/// @param uiScale The interface size: a logical pixel of the overlays' lines and labels is this many screen pixels
	void render(const std::string& defName,
				Foundation::Vec2 worldPos,
				uint32_t turnQuanta,
				const world::WorldCamera& camera,
				int viewportWidth,
				int viewportHeight,
				bool isValid,
				float uiScale);

	/// The reason a ghost at `worldPos` can't go there (rotatable-placed-items 14.4), a short label beside it in the
	/// invalid tint, sized by the interface scale.
	void renderRefusal(std::string_view label, Foundation::Vec2 worldPos, const world::WorldCamera& camera, int viewportWidth, int viewportHeight, float uiScale);

	/// Drops the views no ghost drew this frame, so a drag through many turns keeps only the one it shows.
	void endFrame();

  private:
	// A parts ghost's view and overlays: the placing ghost and every pending install each hold one.
	struct HeldView {
		renderer::TessellatedMesh mesh;
		assets::ItemOverlays	  overlays;
		bool					  drawn = false;
	};
	// By (def, turn, level), under the registry generation they were built in.
	std::map<std::tuple<std::string, uint32_t, uint8_t>, HeldView> views;
	uint64_t													   viewsGeneration = 0;
};

} // namespace world_sim
