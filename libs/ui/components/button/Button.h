#pragma once

#include "components/button/ButtonStyle.h"
#include "component/Component.h"
#include "focus/FocusableBase.h"
#include "math/Types.h"
#include "shapes/Shapes.h"
#include <functional>
#include <string>

// Button Component
//
// Interactive UI button with state management and event handling.
// Supports 5 visual states: Normal, Hover, Pressed, Disabled, Focused
// Extends Component (for AddChild capability) and IFocusable (for keyboard focus).
//
// See: /docs/technical/ui-framework/architecture.md

namespace UI {

// Button component - extends Component and uses FocusableBase for auto-registration
class Button : public Component, public FocusableBase<Button> {
  public:
	// Button type enum for predefined styles
	enum class Type { Primary, Secondary, Custom };

	// Visual interaction state (mouse-driven)
	enum class State { Normal, Hover, Pressed };

	// Constructor arguments struct (C++20 designated initializers)
	struct Args {
		std::string			  label;
		Foundation::Vec2	  position{0.0F, 0.0F};
		Foundation::Vec2	  size{120.0F, 40.0F};
		Type				  type = Type::Primary;
		ButtonAppearance*	  customAppearance = nullptr; // Only used if type == Custom
		bool				  disabled = false;
		std::function<void()> onClick = nullptr;
		const char*			  id = nullptr;
		int					  tabIndex = -1; // Tab order (-1 for auto-assign)
		float				  margin{0.0F};
	};

	// --- Public Members ---

	// Geometry: position and size inherited from Component base class
	std::string label;

	// State
	State state{State::Normal};
	bool  disabled{false};
	bool  focused{false};

	// Visual appearance (all 5 state styles)
	ButtonAppearance appearance;

	// Callback
	std::function<void()> onClick;

	// Properties: visible inherited from IComponent base class
	const char* id = nullptr;

	// --- Public Methods ---

	// Constructor & Destructor
	explicit Button(const Args& args);
	~Button() override = default;

	// Disable copy (Button owns arena memory and is registered with FocusManager)
	Button(const Button&) = delete;
	Button& operator=(const Button&) = delete;

	// Allow move (FocusableBase handles FocusManager re-registration)
	Button(Button&&) noexcept = default;
	Button& operator=(Button&&) noexcept = default;

	// ILayer implementation (overrides Component)
	void update(float deltaTime) override;
	void render() override;

	// IComponent event handling (new event system)
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	// Position override to update text position
	void setPosition(float x, float y) override;

	// State management
	void setFocused(bool newFocused) { focused = newFocused; }
	void setDisabled(bool newDisabled) { disabled = newDisabled; }
	bool isFocused() const { return focused; }
	bool isDisabled() const { return disabled; }

	// IFocusable implementation
	void onFocusGained() override;
	void onFocusLost() override;
	void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void handleCharInput(char32_t codepoint) override;
	bool canReceiveFocus() const override;

	// Geometry queries
	Foundation::Vec2 getCenter() const {
		Foundation::Vec2 contentPos = getContentPosition();
		return Foundation::Vec2{contentPos.x + size.x * 0.5F, contentPos.y + size.y * 0.5F};
	}

  private:
	// Internal state tracking
	bool mouseOver{false};
	bool mouseDown{false};

	// Text label (owned directly for simplicity)
	Text labelText;

	// Get current style based on state/flags
	const ButtonStyle& getCurrentStyle() const;

	// Update text position when button moves
	void updateTextPosition();
};

} // namespace UI
