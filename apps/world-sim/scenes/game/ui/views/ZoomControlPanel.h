#pragma once

// ZoomControlPanel - Floating zoom control positioned in viewport.
//
// Wraps ZoomControl and handles viewport-relative positioning.
// Docked bottom-right as a vertical column.
// Extends UI::Component to use the Layer system for child management.

#include "scenes/game/ui/components/ZoomControl.h"

#include <component/Component.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>

#include <functional>

namespace world_sim {

/// Floating zoom control panel for the game viewport.
class ZoomControlPanel : public UI::Component {
  public:
	struct Args {
		std::function<void()> onZoomIn = nullptr;
		std::function<void()> onZoomOut = nullptr;
		std::function<void()> onZoomReset = nullptr;
		std::string id = "zoom_panel";
	};

	explicit ZoomControlPanel(const Args& args);

	/// Position the panel within the viewport (call on resize)
	void layout(const Foundation::Rect& bounds) override;

	/// Update the displayed zoom percentage
	void setZoomPercent(int percent);

	/// Handle input events - delegates to children via dispatchEvent
	bool handleEvent(UI::InputEvent& event) override;

	// render() inherited from Component - auto-renders children

  private:
	UI::LayerHandle zoomControlHandle;

	// Layout constants
	static constexpr float kRightMargin = 12.0F;
	static constexpr float kBottomMargin = 12.0F;  // Bottom-right per prototype
};

}  // namespace world_sim
