#pragma once

// ZoomControlPanel - Floating zoom control positioned in viewport.
//
// Wraps ZoomControl and handles viewport-relative positioning.
// Positioned on the right side of the viewport.

#include "scenes/game/ui/components/ZoomControl.h"

#include <graphics/Rect.h>
#include <input/InputEvent.h>

#include <functional>
#include <memory>

namespace world_sim {

/// Floating zoom control panel for the game viewport.
class ZoomControlPanel {
  public:
	struct Args {
		std::function<void()> onZoomIn = nullptr;
		std::function<void()> onZoomOut = nullptr;
		std::function<void()> onZoomReset = nullptr;
		std::string id = "zoom_panel";
	};

	explicit ZoomControlPanel(const Args& args);

	/// Position the panel within the viewport (call on resize)
	void layout(const Foundation::Rect& viewportBounds);

	/// Update the displayed zoom percentage
	void setZoomPercent(int percent);

	/// Handle input events
	bool handleEvent(UI::InputEvent& event);

	/// Render the panel
	void render();

  private:
	std::unique_ptr<ZoomControl> zoomControl;
	Foundation::Rect viewportBounds;

	// Layout constants
	static constexpr float kRightMargin = 20.0F;
	static constexpr float kTopMargin = 80.0F;  // Below where TopBar will be
};

}  // namespace world_sim
