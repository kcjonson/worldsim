#pragma once

// ContextMenu - A right-click popup menu component
//
// Displays a popup menu at a given position. Handles its own
// open/close state, click-outside-to-close, and keyboard navigation.
//
// Usage:
//   ContextMenu menu(ContextMenu::Args{
//       .items = {
//           {.label = "Cut", .onSelect = [&]{ doCut(); }},
//           {.label = "Copy", .onSelect = [&]{ doCopy(); }},
//           {.label = "Paste", .onSelect = [&]{ doPaste(); }, .enabled = hasClipboard},
//       },
//       .onClose = [&]{ /* cleanup */ },
//   });
//
//   // On right-click:
//   menu.openAt(cursorPos, screenWidth, screenHeight);

#include "component/Component.h"
#include "focus/FocusableBase.h"
#include "input/InputTypes.h"
#include "theme/Theme.h"

#include <functional>
#include <string>
#include <vector>

namespace UI {

/// A single item in the context menu
struct ContextMenuItem {
	std::string			  label;
	std::function<void()> onSelect;
	bool				  enabled{true};
};

class ContextMenu : public Component, public FocusableBase<ContextMenu> {
  public:
	struct Args {
		std::vector<ContextMenuItem> items;
		std::function<void()>		 onClose;
	};

	explicit ContextMenu(const Args& args);
	~ContextMenu() override = default;

	// Disable copy
	ContextMenu(const ContextMenu&) = delete;
	ContextMenu& operator=(const ContextMenu&) = delete;

	// Allow move
	ContextMenu(ContextMenu&&) noexcept = default;
	ContextMenu& operator=(ContextMenu&&) noexcept = default;

	/// Open the context menu at the given position
	/// @param pos The cursor position to open at
	/// @param screenWidth Screen width for edge clamping
	/// @param screenHeight Screen height for edge clamping
	void openAt(Foundation::Vec2 pos, float screenWidth, float screenHeight);

	/// Close the context menu
	void close();

	/// Check if the menu is currently open
	[[nodiscard]] bool isOpen() const { return state != State::Closed; }

	/// Get menu items
	[[nodiscard]] const std::vector<ContextMenuItem>& getItems() const { return items; }

	/// Get current hovered item index (-1 if none)
	[[nodiscard]] int getHoveredIndex() const { return hoveredIndex; }

	// IComponent overrides
	void render() override;
	bool handleEvent(InputEvent& event) override;
	void update(float deltaTime) override;

	// IFocusable implementation
	void onFocusGained() override;
	void onFocusLost() override;
	void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void handleCharInput(char32_t codepoint) override;
	bool canReceiveFocus() const override;

  private:
	std::vector<ContextMenuItem> items;
	std::function<void()>		 onClose;

	// State
	enum class State { Closed, Open };
	State state{State::Closed};
	int	  hoveredIndex{-1};

	// Screen bounds for edge clamping
	float screenW{0.0F};
	float screenH{0.0F};

	// Dimensions
	[[nodiscard]] float getMenuWidth() const;
	[[nodiscard]] float getMenuHeight() const;
	[[nodiscard]] Foundation::Rect getItemBounds(size_t index) const;
	[[nodiscard]] int getItemAtPoint(Foundation::Vec2 point) const;

	// Position menu to stay on screen
	[[nodiscard]] Foundation::Vec2 calculatePosition(Foundation::Vec2 cursor) const;

	// Select item at index
	void selectItem(size_t index);
};

} // namespace UI
