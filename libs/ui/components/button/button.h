#pragma once

#include "components/button/button_style.h"
#include "component/component.h"
#include "focus/focusable.h"
#include "math/types.h"
#include "shapes/shapes.h"
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

// Forward declarations
class FocusManager;

// Button component - extends Component and implements IFocusable
class Button : public Component, public IFocusable {
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
	};

	// --- Public Members ---

	// Geometry
	Foundation::Vec2 m_position{0.0F, 0.0F};
	Foundation::Vec2 m_size{120.0F, 40.0F};
	std::string		 m_label;

	// State
	State m_state{State::Normal};
	bool  m_disabled{false};
	bool  m_focused{false};

	// Visual appearance (all 5 state styles)
	ButtonAppearance m_appearance;

	// Callback
	std::function<void()> m_onClick;

	// Properties
	bool		visible{true};
	const char* id = nullptr;

	// --- Public Methods ---

	// Constructor & Destructor
	explicit Button(const Args& args);
	~Button() override;

	// Disable copy (Button owns arena memory and registers with FocusManager)
	Button(const Button&) = delete;
	Button& operator=(const Button&) = delete;

	// Allow move
	Button(Button&& other) noexcept;
	Button& operator=(Button&& other) noexcept;

	// ILayer implementation (overrides Component)
	void HandleInput() override;
	void Update(float deltaTime) override;
	void Render() override;

	// State management
	void SetFocused(bool focused) { m_focused = focused; }
	void SetDisabled(bool disabled) { m_disabled = disabled; }
	bool IsFocused() const { return m_focused; }
	bool IsDisabled() const { return m_disabled; }

	// IFocusable implementation
	void OnFocusGained() override;
	void OnFocusLost() override;
	void HandleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void HandleCharInput(char32_t codepoint) override;
	bool CanReceiveFocus() const override;

	// Geometry queries
	bool			 ContainsPoint(const Foundation::Vec2& point) const;
	Foundation::Vec2 GetCenter() const {
		return Foundation::Vec2{m_position.x + m_size.x * 0.5F, m_position.y + m_size.y * 0.5F};
	}

  private:
	// Internal state tracking
	bool m_mouseOver{false};
	bool m_mouseDown{false};

	// Text label (owned directly for simplicity)
	Text m_labelText;

	// Focus management
	int m_tabIndex{-1};

	// Get current style based on state/flags
	const ButtonStyle& GetCurrentStyle() const;
};

} // namespace UI
