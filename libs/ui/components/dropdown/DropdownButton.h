#pragma once

// DropdownButton - Button with expandable action menu
//
// A button that displays a dropdown indicator (▾) and expands a menu panel
// when clicked. Used for action categories like [Actions▾] [Build▾].
//
// Note: For controlled form elements (where parent owns selected value),
// use Select instead.
//
// Features:
// - Button with ▾ indicator
// - Menu panel expands below button on click (uses Menu component)
// - Menu items with hover highlighting
// - Closes on item selection or outside click
// - Keyboard navigation when focused

#include "component/Component.h"
#include "components/menu/Menu.h"
#include "focus/FocusableBase.h"
#include "graphics/Color.h"
#include "layer/Layer.h"
#include "theme/Theme.h"

#include <functional>
#include <string>
#include <vector>

namespace UI {

/// A single item in the dropdown menu
struct DropdownItem {
	std::string			  label;
	std::function<void()> onSelect;
	bool				  enabled{true};
};

class DropdownButton : public Component, public FocusableBase<DropdownButton> {
  public:
	struct Args {
		std::string				 label;
		Foundation::Vec2		 position{0.0F, 0.0F};
		Foundation::Vec2		 buttonSize{120.0F, 36.0F};
		std::vector<DropdownItem> items;
		const char*				 id = nullptr;
		int						 tabIndex = -1;
		float					 margin{0.0F};
	};

	explicit DropdownButton(const Args& args);
	~DropdownButton() override = default;

	// Disable copy (owns internal data)
	DropdownButton(const DropdownButton&) = delete;
	DropdownButton& operator=(const DropdownButton&) = delete;

	// Allow move
	DropdownButton(DropdownButton&&) noexcept = default;
	DropdownButton& operator=(DropdownButton&&) noexcept = default;

	// State
	[[nodiscard]] bool isOpen() const { return open; }
	void			   openMenu();
	void			   closeMenu();
	void			   toggle();

	// Menu items
	void setItems(std::vector<DropdownItem> newItems);
	[[nodiscard]] const std::vector<DropdownItem>& getItems() const { return items; }

	// Label
	void setLabel(const std::string& newLabel) { label = newLabel; }
	[[nodiscard]] const std::string& getLabel() const { return label; }

	// IComponent overrides
	void render() override;
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;
	void setPosition(float x, float y) override;

	// ILayer overrides
	void update(float deltaTime) override;

	// IFocusable overrides
	void onFocusGained() override;
	void onFocusLost() override;
	void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void handleCharInput(char32_t codepoint) override;
	bool canReceiveFocus() const override;

  private:
	std::string				  label;
	Foundation::Vec2		  buttonSize;
	std::vector<DropdownItem> items;
	bool					  open{false};
	bool					  focused{false};
	int						  hoveredItemIndex{-1};
	bool					  buttonHovered{false};
	bool					  buttonPressed{false};

	// Menu component (embedded child)
	LayerHandle menuHandle;

	// Get bounds for the button
	[[nodiscard]] Foundation::Rect getButtonBounds() const;

	// Hit testing
	[[nodiscard]] bool isPointInButton(Foundation::Vec2 point) const;

	// Convert DropdownItems to MenuItems
	[[nodiscard]] std::vector<MenuItem> convertToMenuItems() const;

	// Update menu position (below button)
	void updateMenuPosition();

	// Select item and close menu
	void selectItem(size_t index);
};

} // namespace UI
