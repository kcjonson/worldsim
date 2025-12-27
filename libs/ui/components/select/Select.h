#pragma once

// Select - A controlled form element for choosing from a list of options
//
// Select displays a button showing the currently selected value and opens
// a dropdown menu when clicked. It follows the controlled component pattern:
// - Parent provides the current value
// - Component fires onChange when user selects a different option
// - Component manages its own UI state (open/closed, hover)
//
// Features:
// - Controlled value (parent provides selected value)
// - Dropdown menu with keyboard navigation
// - Focus ring when focused
// - Uses Menu component internally

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

/// A single option in the select list
struct SelectOption {
	std::string label;
	std::string value; // Can be same as label if desired
};

class Select : public Component, public FocusableBase<Select> {
  public:
	struct Args {
		Foundation::Vec2					  position{0.0F, 0.0F};
		Foundation::Vec2					  size{150.0F, 36.0F};
		std::vector<SelectOption>			  options;
		std::string							  value;	  // Controlled: currently selected value
		std::string							  placeholder = "Select..."; // Shown when no value
		std::function<void(const std::string&)> onChange; // Fires when selection changes
		const char*							  id = nullptr;
		int									  tabIndex = -1;
		float								  margin{0.0F};
	};

	explicit Select(const Args& args);
	~Select() override = default;

	// Disable copy
	Select(const Select&) = delete;
	Select& operator=(const Select&) = delete;

	// Allow move
	Select(Select&&) noexcept = default;
	Select& operator=(Select&&) noexcept = default;

	// Controlled value
	void						   setValue(const std::string& newValue);
	[[nodiscard]] const std::string& getValue() const { return value; }

	// Options
	void									   setOptions(std::vector<SelectOption> newOptions);
	[[nodiscard]] const std::vector<SelectOption>& getOptions() const { return options; }

	// UI State (read-only to external code)
	[[nodiscard]] bool isOpen() const { return open; }

	// Get selected option label (for display)
	[[nodiscard]] std::string getSelectedLabel() const;

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
	std::vector<SelectOption>			  options;
	std::string							  value;
	std::string							  placeholder;
	std::function<void(const std::string&)> onChange;

	// UI state (internal)
	bool open{false};
	bool focused{false};
	int	 hoveredItemIndex{-1};
	bool buttonHovered{false};
	bool buttonPressed{false};

	// Menu component (embedded child)
	LayerHandle menuHandle;

	// Get bounds for the select button
	[[nodiscard]] Foundation::Rect getButtonBounds() const;

	// Hit testing
	[[nodiscard]] bool isPointInButton(Foundation::Vec2 point) const;

	// Open/close menu
	void openMenu();
	void closeMenu();
	void toggle();

	// Convert options to menu items
	[[nodiscard]] std::vector<MenuItem> convertToMenuItems();

	// Update menu position (below button)
	void updateMenuPosition();

	// Find index of currently selected value
	[[nodiscard]] int findSelectedIndex() const;

	// Select option by index (fires onChange if different)
	void selectOption(size_t index);

	// Select option by value (fires onChange if different)
	void selectOptionByValue(const std::string& optionValue);
};

} // namespace UI
