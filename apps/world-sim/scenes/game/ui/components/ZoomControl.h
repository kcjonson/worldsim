#pragma once

// ZoomControl - vertical zoom column (+ / reset / -) with a mono % readout
// beneath, docked bottom-right by ZoomControlPanel.
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

	/// Buttons render as children; the mono % readout paints beneath them
	void render() override;

	// Layout constants (column footprint, used by ZoomControlPanel)
	static constexpr float kButtonSize = 28.0F;
	static constexpr float kSpacing = 4.0F;
	static constexpr float kLabelHeight = 14.0F;
	static constexpr float kTotalWidth = kButtonSize;
	static constexpr float kTotalHeight = kButtonSize * 3.0F + kSpacing * 3.0F + kLabelHeight;

  private:
	int zoomPercent = 100;

	UI::LayerHandle zoomInButtonHandle;
	UI::LayerHandle zoomResetButtonHandle;
	UI::LayerHandle zoomOutButtonHandle;

	void positionElements();

	static constexpr float kIconSize = 16.0F;
	static constexpr float kFontSize = 11.0F;
};

}  // namespace world_sim
