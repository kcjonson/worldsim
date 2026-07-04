#pragma once

// RegionMinimapPanel - fixed-window overview map, first item in the right stack.
//
// A Salvage Panel (title "Region", data accent, flush body) whose body is a
// projected canvas over a fixed world window centered on the crash site:
// camera viewport rectangle, colonist dots, crash-site marker, faint grid,
// landing-coordinate label, and off-map bearing chevrons at the inset edge
// for colonists outside the window. Terrain and click-to-navigate belong to
// the separate Minimap epic; the body stays a flat inset-tinted rect.

#include "scenes/game/ui/adapters/ColonistAdapter.h"

#include <component/Component.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <math/Types.h>

#include <string>
#include <vector>

namespace world_sim {

/// Region overview panel for the top-right HUD stack.
class RegionMinimapPanel : public UI::Component {
  public:
	struct Args {
		float width = 232.0F;
		const char* id = "region_minimap";
	};

	explicit RegionMinimapPanel(const Args& args);

	/// Set position (anchor point is the panel's top-right corner)
	void setAnchorPosition(float x, float y);

	/// One-time landing context: map window center + coordinate label source.
	void setContext(Foundation::Vec2 crashSite, double latDeg, double lonDeg);

	/// Per-frame data: camera visible rect (world meters) + colonist snapshot.
	void updateData(const Foundation::Rect& visibleRect, const std::vector<adapters::ColonistData>& colonists);

	/// Consume clicks over the panel so they never reach the world.
	bool handleEvent(UI::InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	void render() override;

	/// Current bounds (for right-stack layout)
	[[nodiscard]] Foundation::Rect getBounds() const;

	const char* debugTypeName() const override { return "RegionMinimapPanel"; }
	const char* debugId() const override { return id; }

  private:
	// Fixed map window edge (world meters). The default 100% view is ~128 m
	// wide, so 256 m shows it at half scale with working room around the
	// clearing; the window height follows the body aspect (~163 m).
	static constexpr float kWindowMeters = 256.0F;
	static constexpr float kBodyHeight = 148.0F; // 232 x 148 mirrors the prototype's 100:64 canvas
	static constexpr float kEdgeInset = 0.12F;	 // chevron perimeter inset (fraction of body)

	float panelWidth;
	float headerHeight = 0.0F;
	const char* id;

	Foundation::Vec2 crashSite{0.0F, 0.0F};
	std::string coordLat;  // "14.2" (degree ring + hemisphere drawn as vectors)
	std::string coordLatHemi;
	std::string coordLon;
	std::string coordLonHemi;
	bool hasContext = false;

	Foundation::Rect viewRect{};
	std::vector<Foundation::Vec2> colonistDots;

	// Map window (world meters), fixed once context is set
	[[nodiscard]] Foundation::Rect windowRect() const;
	[[nodiscard]] Foundation::Rect bodyRect() const;
	[[nodiscard]] Foundation::Vec2 worldToMap(Foundation::Vec2 world) const;

	void renderGrid(const Foundation::Rect& body) const;
	void renderViewportRect() const;
	void renderCrashMarker() const;
	void renderColonists() const;
	void renderCoordLabel(const Foundation::Rect& body) const;
	void renderBearingChevron(const Foundation::Rect& body, Foundation::Vec2 colonist) const;
};

}  // namespace world_sim
