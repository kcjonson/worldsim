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
#include <components/button/Button.h>
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
		std::function<void(const std::string&)> onStructureSelected = nullptr; ///< Activates a structure tool (e.g. "foundation")
		std::function<void()>					onRoomsToggle = nullptr; ///< Toggles the rooms overlay (same state the R hotkey flips)
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

	/// Reflect the rooms-overlay active state on the Rooms toggle button (highlighted
	/// fill when on). Pushed by GameUI so the button and the R hotkey stay in sync.
	void setRoomsActive(bool active);

  private:
	// Layout constants
	static constexpr float kBarHeight = 48.0F;
	static constexpr float kButtonWidth = 100.0F;
	static constexpr float kButtonHeight = 36.0F;
	static constexpr float kButtonSpacing = 8.0F;
	static constexpr float kBottomMargin = 12.0F;
	static constexpr float kHorizontalPadding = 12.0F;  // Padding on each side of buttons
	static constexpr float kRoomsButtonWidth = 72.0F;	// Narrower than the dropdowns (single word)

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
	UI::LayerHandle roomsButtonHandle;

	// Callbacks
	std::function<void()> onBuildClick;
	std::function<void(const std::string&)> onActionSelected;
	std::function<void(const std::string&)> onProductionSelected;
	std::function<void(const std::string&)> onFurnitureSelected;
	std::function<void(const std::string&)> onStructureSelected;
	std::function<void()>					onRoomsToggle;

	bool roomsActive = false;

	/// Position all elements
	void positionElements();

	/// The two appearances the Rooms toggle swaps between (active = highlighted fill).
	static UI::ButtonAppearance roomsButtonAppearance(bool active);
};

}  // namespace world_sim
