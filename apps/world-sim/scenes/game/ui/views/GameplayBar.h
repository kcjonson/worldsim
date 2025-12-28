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
// Extends UI::Component to use the Layer system for child management.

#include <component/Component.h>
#include <components/dropdown/DropdownButton.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>

namespace world_sim {

/// Main gameplay action bar with category dropdowns.
class GameplayBar : public UI::Component {
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
	void layout(const Foundation::Rect& viewportBounds) override;

	/// Handle input events
	bool handleEvent(UI::InputEvent& event) override;

	// render() inherited from Component - auto-renders children

	/// Get height of the bar
	[[nodiscard]] float getHeight() const override { return kBarHeight; }

	/// Close all open dropdowns
	void closeAllDropdowns();

  private:
	// Layout constants
	static constexpr float kBarHeight = 40.0F;
	static constexpr float kButtonWidth = 100.0F;
	static constexpr float kButtonHeight = 28.0F;
	static constexpr float kButtonSpacing = 8.0F;
	static constexpr float kBottomMargin = 12.0F;
	static constexpr float kHorizontalPadding = 12.0F;  // Padding on each side of buttons

	// Cached layout values (computed once in layout())
	float cachedBarWidth = 0.0F;
	float cachedBarX = 0.0F;
	float cachedBarY = 0.0F;

	// Child handles
	UI::LayerHandle backgroundHandle;
	UI::LayerHandle actionsDropdownHandle;
	UI::LayerHandle buildDropdownHandle;
	UI::LayerHandle productionDropdownHandle;
	UI::LayerHandle furnitureDropdownHandle;

	// Callbacks
	std::function<void()> onBuildClick;
	std::function<void(const std::string&)> onActionSelected;
	std::function<void(const std::string&)> onProductionSelected;
	std::function<void(const std::string&)> onFurnitureSelected;

	/// Position all elements
	void positionElements();
};

}  // namespace world_sim
