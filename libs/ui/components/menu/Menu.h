#pragma once

// Menu - A "dumb" visual component for rendering menu items
//
// The Menu component displays a list of items with hover highlighting and
// click handling, but does NOT manage its own open/close state or focus.
// This is controlled by the parent component (e.g., DropdownButton, Select).
//
// Features:
// - Renders menu background with floating panel style
// - Items with hover highlighting
// - Disabled item styling
// - Click fires onSelect callback
// - Parent controls visibility via setVisible()

#include "component/Component.h"
#include "graphics/Color.h"
#include "theme/Theme.h"

#include <functional>
#include <string>
#include <vector>

namespace UI {

/// A single item in the menu
struct MenuItem {
	std::string			  label;
	std::function<void()> onSelect;
	bool				  enabled{true};
};

class Menu : public Component {
  public:
	struct Args {
		Foundation::Vec2	   position{0.0F, 0.0F};
		float				   width{150.0F};
		std::vector<MenuItem>  items;
		int					   hoveredIndex{-1}; // Parent can set highlighted item
		const char*			   id = nullptr;
	};

	explicit Menu(const Args& args);
	~Menu() override = default;

	// Disable copy (owns internal data)
	Menu(const Menu&) = delete;
	Menu& operator=(const Menu&) = delete;

	// Allow move
	Menu(Menu&&) noexcept = default;
	Menu& operator=(Menu&&) noexcept = default;

	// Items
	void									setItems(std::vector<MenuItem> newItems);
	[[nodiscard]] const std::vector<MenuItem>& getItems() const { return items; }
	[[nodiscard]] size_t					getItemCount() const { return items.size(); }

	// Hovered item (parent can set this for keyboard navigation)
	void setHoveredIndex(int index) { hoveredItemIndex = index; }
	[[nodiscard]] int getHoveredIndex() const { return hoveredItemIndex; }

	// Dimensions
	void setWidth(float newWidth);
	[[nodiscard]] float getMenuWidth() const { return menuWidth; }
	[[nodiscard]] float getMenuHeight() const;
	[[nodiscard]] Foundation::Rect getBounds() const;

	// Hit testing - parent uses this for event delegation
	[[nodiscard]] Foundation::Rect getItemBounds(size_t index) const;
	[[nodiscard]] int			   getItemAtPoint(Foundation::Vec2 point) const;
	[[nodiscard]] bool			   containsPoint(Foundation::Vec2 point) const override;

	// IComponent overrides
	void render() override;
	bool handleEvent(InputEvent& event) override;

	// ILayer overrides
	void update(float deltaTime) override;

	// Select item at index (fires onSelect callback)
	void selectItem(size_t index);

  private:
	std::vector<MenuItem> items;
	float				  menuWidth;
	int					  hoveredItemIndex{-1};

	// Menu dimensions
	static constexpr float kMenuItemHeight = Theme::Dropdown::menuItemHeight;
	static constexpr float kMenuPadding = 4.0F;
};

} // namespace UI
