#pragma once

// ZoomControl - Compact zoom level display with +/- buttons.
// Shows current zoom percentage and allows step-based zoom changes.
// Uses Button with SVG icons.
// Extends UI::Component to use the Layer system for child management.

#include <component/Component.h>
#include <components/button/Button.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>

namespace world_sim {

/// Compact zoom control widget for the game overlay.
class ZoomControl : public UI::Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		std::function<void()> onZoomIn = nullptr;
		std::function<void()> onZoomOut = nullptr;
		std::function<void()> onZoomReset = nullptr;
		std::string id = "zoom_control";
	};

	explicit ZoomControl(const Args& args);

	/// Update the displayed zoom percentage
	void setZoomPercent(int percent);

	/// Update position (for viewport-relative positioning)
	void setPosition(float x, float y) override;

	/// Dispatch an input event - delegates to children via dispatchEvent
	bool handleEvent(UI::InputEvent& event) override;

	// render() inherited from Component - auto-renders children

  private:
	int zoomPercent = 100;

	UI::LayerHandle zoomOutButtonHandle;
	UI::LayerHandle zoomTextHandle;
	UI::LayerHandle zoomInButtonHandle;
	UI::LayerHandle zoomResetButtonHandle;

	void updateZoomText();
	void positionElements();

	// Layout constants
	static constexpr float kButtonSize = 28.0F;
	static constexpr float kIconSize = 16.0F;
	static constexpr float kTextWidth = 50.0F;
	static constexpr float kSpacing = 4.0F;
	static constexpr float kFontSize = 14.0F;
};

}  // namespace world_sim
