#pragma once

// ZoomControl - Compact zoom level display with +/- buttons.
// Shows current zoom percentage and allows step-based zoom changes.

#include <components/button/Button.h>
#include <shapes/Shapes.h>

#include <functional>
#include <memory>

namespace world_sim {

/// Compact zoom control widget for the game overlay.
class ZoomControl {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		std::function<void()> onZoomIn = nullptr;
		std::function<void()> onZoomOut = nullptr;
		std::string id = "zoom_control";
	};

	explicit ZoomControl(const Args& args);

	/// Update the displayed zoom percentage
	void setZoomPercent(int percent);

	/// Update position (for viewport-relative positioning)
	void setPosition(Foundation::Vec2 position);

	/// Handle mouse input for buttons
	void handleInput();

	/// Render the control
	void render();

  private:
	Foundation::Vec2 m_position;
	int m_zoomPercent = 100;

	std::unique_ptr<UI::Button> m_zoomOutButton;
	std::unique_ptr<UI::Text> m_zoomText;
	std::unique_ptr<UI::Button> m_zoomInButton;

	void updateZoomText();
};

}  // namespace world_sim
