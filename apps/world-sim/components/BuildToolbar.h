#pragma once

// BuildToolbar - Build mode toggle button for the game overlay.
// Follows the ZoomControl pattern: small interactive widget in bottom-left.

#include <components/button/Button.h>
#include <input/InputEvent.h>
#include <shapes/Shapes.h>

#include <functional>
#include <memory>

namespace world_sim {

/// Build mode toggle button widget.
class BuildToolbar {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		std::function<void()> onBuildClick = nullptr;
		std::string id = "build_toolbar";
	};

	explicit BuildToolbar(const Args& args);

	/// Update position (for viewport-relative positioning)
	void setPosition(Foundation::Vec2 position);

	/// Set whether build mode is currently active (changes button appearance)
	void setActive(bool active);

	/// Handle input event, returns true if consumed
	bool handleEvent(UI::InputEvent& event);

	/// Render the control
	void render();

  private:
	Foundation::Vec2 m_position;
	bool m_isActive = false;

	std::unique_ptr<UI::Button> m_buildButton;
	std::function<void()> m_onBuildClick;

	void updateButtonStyle();
};

} // namespace world_sim
