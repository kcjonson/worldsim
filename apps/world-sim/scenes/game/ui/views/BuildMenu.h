#pragma once

// BuildMenu - Popup panel displaying placeable items.
// Shows items the player can place in the world (e.g., CraftingSpot).
// Initially shows only innate recipes; will expand as colonists learn more.

#include <components/button/Button.h>
#include <input/InputEvent.h>
#include <layout/LayoutContainer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace world_sim {

/// A single item that can be built/placed
struct BuildMenuItem {
	std::string defName; ///< Definition name (e.g., "CraftingSpot")
	std::string label;	 ///< Display name (e.g., "Crafting Spot")
};

/// Popup menu for selecting items to build.
class BuildMenu {
  public:
	/// Callback when an item is selected
	using SelectCallback = std::function<void(const std::string& defName)>;

	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		SelectCallback onSelect = nullptr;
		std::function<void()> onClose = nullptr;
		std::string id = "build_menu";
	};

	explicit BuildMenu(const Args& args);

	/// Set the items to display in the menu
	void setItems(const std::vector<BuildMenuItem>& items);

	/// Update position
	void setPosition(Foundation::Vec2 position);

	/// Handle input event, returns true if consumed
	bool handleEvent(UI::InputEvent& event);

	/// Render the menu
	void render();

	/// Get menu bounds for layout calculations
	[[nodiscard]] Foundation::Rect bounds() const;

  private:
	Foundation::Vec2 m_position;
	SelectCallback m_onSelect;
	std::function<void()> m_onClose;

	std::vector<BuildMenuItem> m_items;
	std::unique_ptr<UI::LayoutContainer> m_buttonLayout;
	std::unique_ptr<UI::Text> m_titleText;

	float m_menuWidth = 180.0F;
	float m_menuHeight = 0.0F;

	void rebuildButtons();
};

} // namespace world_sim
