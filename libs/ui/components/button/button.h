#pragma once

#include "components/button/button_style.h"
#include "focus/focusable.h"
#include "layer/layer.h"
#include "math/types.h"
#include "shapes/shapes.h"
#include <functional>
#include <string>

// Button Component
//
// Interactive UI button with state management and event handling.
// Supports 5 visual states: Normal, Hover, Pressed, Disabled, Focused
// Uses standard lifecycle: HandleInput() → Update() → Render()
// Implements IFocusable and satisfies Layer/Focusable concepts
//
// Note: Currently inherits IFocusable for FocusManager compatibility.
// The Focusable concept provides compile-time verification.
// See: /docs/technical/ui-framework/architecture.md

namespace UI {

	// Forward declarations
	class FocusManager;

	// Button component - implements IFocusable and satisfies Layer/Focusable concepts
	struct Button : public IFocusable {
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
			float				  zIndex = -1.0F; // -1.0F = auto-assign
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

		// Layer properties (for integration with LayerManager)
		float		zIndex{-1.0F};
		bool		visible{true};
		const char* id = nullptr;

		// --- Public Methods ---

		// Constructor & Destructor
		explicit Button(const Args& args);
		~Button();

		// Disable copy (Button may register with FocusManager)
		Button(const Button&) = delete;
		Button& operator=(const Button&) = delete;

		// Allow move (must unregister from old address and re-register at new address)
		Button(Button&& other) noexcept;
		Button& operator=(Button&& other) noexcept;

		// Standard lifecycle methods
		void HandleInput();			  // Process mouse input, update state
		void Update(float deltaTime); // Apply state changes to visual appearance
		void Render() const;		  // Draw button using Primitives API

		// State management
		void SetFocused(bool focused) { m_focused = focused; } // For manual focus (backward compat)
		void SetDisabled(bool disabled) { m_disabled = disabled; }
		bool IsFocused() const { return m_focused; }
		bool IsDisabled() const { return m_disabled; }

		// IFocusable interface implementation (also satisfies Focusable concept)
		void OnFocusGained() override;
		void OnFocusLost() override;
		void HandleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
		void HandleCharInput(char32_t codepoint) override;
		bool CanReceiveFocus() const override;

		// Geometry queries
		bool			 ContainsPoint(const Foundation::Vec2& point) const;
		Foundation::Vec2 GetCenter() const { return Foundation::Vec2{m_position.x + m_size.x * 0.5F, m_position.y + m_size.y * 0.5F}; }

	  private:
		// Internal state tracking
		bool m_mouseOver{false};
		bool m_mouseDown{false};

		// Text label component (centered within button)
		Text m_labelText;

		// Focus management
		int m_tabIndex{-1}; // Preserved for move operations

		// Get current style based on state/flags
		const ButtonStyle& GetCurrentStyle() const;
	};

	// Compile-time verification that Button satisfies concepts
	static_assert(Layer<Button>, "Button must satisfy Layer concept");
	static_assert(Focusable<Button>, "Button must satisfy Focusable concept");

} // namespace UI
