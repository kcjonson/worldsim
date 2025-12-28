#pragma once

// GameplayBar - Main gameplay action bar at bottom of screen.
//
// Layout:
// ┌─────────────────────────────────────────────────────────────────┐
// │        [Actions▾]  [Build▾]  [Production▾]  [Furniture▾]        │
// └─────────────────────────────────────────────────────────────────┘
//
// Each dropdown expands to show relevant options.
// Replaces the simple BuildToolbar with full category access.

#include <components/dropdown/DropdownButton.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <shapes/Shapes.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace world_sim {

/// Main gameplay action bar with category dropdowns.
class GameplayBar {
  public:
	struct Args {
		std::function<void()> onBuildClick = nullptr;  ///< Opens build menu
		std::function<void(const std::string&)> onActionSelected = nullptr;
		std::function<void(const std::string&)> onProductionSelected = nullptr;
		std::function<void(const std::string&)> onFurnitureSelected = nullptr;
		std::string id = "gameplay_bar";
	};

	explicit GameplayBar(const Args& args);

	/// Layout the bar within viewport bounds
	void layout(const Foundation::Rect& viewportBounds);

	/// Handle input events
	bool handleEvent(UI::InputEvent& event);

	/// Render the bar
	void render();

	/// Get height of the bar
	[[nodiscard]] float getHeight() const { return kBarHeight; }

	/// Close all open dropdowns
	void closeAllDropdowns();

  private:
	// Layout constants
	static constexpr float kBarHeight = 40.0F;
	static constexpr float kButtonWidth = 100.0F;
	static constexpr float kButtonHeight = 28.0F;
	static constexpr float kButtonSpacing = 8.0F;
	static constexpr float kBottomMargin = 12.0F;

	// Background
	std::unique_ptr<UI::Rectangle> background;

	// Dropdown buttons
	std::unique_ptr<UI::DropdownButton> actionsDropdown;
	std::unique_ptr<UI::DropdownButton> buildDropdown;
	std::unique_ptr<UI::DropdownButton> productionDropdown;
	std::unique_ptr<UI::DropdownButton> furnitureDropdown;

	// Callbacks
	std::function<void()> onBuildClick;
	std::function<void(const std::string&)> onActionSelected;
	std::function<void(const std::string&)> onProductionSelected;
	std::function<void(const std::string&)> onFurnitureSelected;

	// Cached viewport
	Foundation::Rect viewportBounds;

	/// Position all elements
	void positionElements();
};

}  // namespace world_sim
