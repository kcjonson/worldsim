#pragma once

// GlobeView - reusable 3D planet view for scenes (WorldCreator, LandingSite).
// Owns the planet-view mesh/colorizer/renderer/camera, renders the globe into
// a logical-coordinate rect, and handles orbit/zoom/pick input within it.
//
// Render-order contract: call render() BEFORE any 2D Primitives for the frame
// region; the globe blits immediately while Primitives batches flush later,
// so 2D UI always composites on top.

#include <planet-view/OrbitCamera.h>
#include <planet-view/PlanetColorizer.h>
#include <planet-view/PlanetMesh.h>
#include <planet-view/PlanetPicker.h>
#include <planet-view/PlanetRenderer.h>
#include <worldgen/data/GeneratedWorld.h>

#include <input/InputEvent.h>
#include <math/Types.h>
#include <shapes/Shapes.h>
#include <threading/TaskPool.h>

#include <memory>
#include <optional>

namespace world_sim {

class GlobeView {
  public:
	// (Re)build the globe from a world snapshot. Safe to call repeatedly;
	// rebuilds mesh/colorizer when the grid changes, otherwise just recolors.
	void setWorld(std::shared_ptr<const worldgen::GeneratedWorld> world);

	// Recolor from the current world (e.g. after a progressive snapshot).
	void refreshColors();

	void setColorMode(planetview::ColorMode mode);
	planetview::ColorMode colorMode() const { return mode; }
	void cycleColorMode();

	bool isReady() const;

	// True while an orbit drag is in progress. Scenes should route input to
	// the globe FIRST while dragging so widget hover states don't react to
	// drag mouse-moves.
	bool isDragging() const { return dragging; }

	void update(float dt) { camera.update(dt); }

	// Keyboard pan: spin yaw / tilt pitch by radians (pitch obeys the camera's
	// zoom gate). For scenes that drive the camera from held keys.
	void panCamera(float dYaw, float dPitch) { camera.nudge(dYaw, dPitch); }

	// Render the globe into `rect` (logical UI coordinates, top-left origin).
	void render(const Foundation::Rect& rect, float logicalW, float logicalH);

	// Orbit drag / scroll zoom / pick within rect. Right-click cycles color
	// mode when cycleOnRightClick. Returns true when the event was consumed.
	bool handleInput(UI::InputEvent& event, const Foundation::Rect& rect, bool cycleOnRightClick);

	// Ray-pick the lat/lon under a logical-coordinate position inside rect.
	std::optional<planetview::LatLon> pick(Foundation::Vec2 pos, const Foundation::Rect& rect) const;

	// Project a lat/lon to logical coordinates inside rect (false if behind globe).
	bool projectLatLon(planetview::LatLon site, const Foundation::Rect& rect,
	                   float& outX, float& outY) const;

	const std::shared_ptr<const worldgen::GeneratedWorld>& world() const { return currentWorld; }

  private:
	// pool must outlive colorizer: it bakes on the pool asynchronously and waits
	// on those bakes in its destructor. Declaration order = construction order;
	// reverse is destruction order, so pool is destroyed last.
	foundation::TaskPool        pool;
	planetview::PlanetMesh      mesh;
	planetview::PlanetColorizer colorizer;
	planetview::PlanetRenderer  renderer;
	planetview::OrbitCamera     camera;

	std::shared_ptr<const worldgen::GeneratedWorld> currentWorld;
	const worldgen::SphereGrid* builtGrid{nullptr};
	planetview::ColorMode       mode{planetview::ColorMode::Terrain};
	bool dragging{false};

	void chooseMinDistance(uint32_t n);

	static bool contains(const Foundation::Rect& r, Foundation::Vec2 p) {
		return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
	}
};

} // namespace world_sim
