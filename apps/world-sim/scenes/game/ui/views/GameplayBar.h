#pragma once

// GameplayBar - Main gameplay action bar at bottom of screen.
//
// Layout:
// ┌─────────────────────────────────────────────────────────────────┐
// │                   [Production▾]  [Furniture▾]                    │
// └─────────────────────────────────────────────────────────────────┘
//
// Each dropdown expands to show the entities it can place. Both menus are
// populated dynamically from the asset/recipe registries, so the bar only
// ever shows actions that actually work.
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

	/// Set the items in the Production dropdown
	/// @param items Vector of {defName, label} pairs for placeable production stations
	void setProductionItems(const std::vector<std::pair<std::string, std::string>>& items);

	/// Set the items in the Furniture dropdown
	/// @param items Vector of {defName, label} pairs for placeable furniture (storage, etc.)
	void setFurnitureItems(const std::vector<std::pair<std::string, std::string>>& items);

  private:
	// Layout constants
	static constexpr int   kButtonCount = 2;  // Production, Furniture
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
	UI::LayerHandle productionDropdownHandle;
	UI::LayerHandle furnitureDropdownHandle;

	// Callbacks
	std::function<void(const std::string&)> onProductionSelected;
	std::function<void(const std::string&)> onFurnitureSelected;

	/// Position all elements
	void positionElements();

	/// Populate a dropdown handle with {defName, label} placement items
	void setDropdownItems(
		UI::LayerHandle handle,
		const std::vector<std::pair<std::string, std::string>>& items,
		const std::function<void(const std::string&)>& callback
	);
};

}  // namespace world_sim
