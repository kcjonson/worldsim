#include "components/button/button.h"
#include "focus/focus_manager.h"
#include "font/font_renderer.h"
#include <input/input_manager.h>
#include <input/input_types.h>
#include <primitives/primitives.h>
#include <utils/log.h>

namespace UI {

	Button::Button(const Args& args)
		: position(args.position),
		  size(args.size),
		  label(args.label),
		  disabled(args.disabled),
		  onClick(args.onClick),
		  id(args.id),
		  tabIndex(args.tabIndex) {

		// Set appearance based on type
		if (args.type == Type::Primary) {
			appearance = ButtonStyles::primary();
		} else if (args.type == Type::Secondary) {
			appearance = ButtonStyles::secondary();
		} else if (args.type == Type::Custom && args.customAppearance != nullptr) {
			appearance = *args.customAppearance;
		} else {
			// Default to Primary if Custom but no appearance provided
			appearance = ButtonStyles::primary();
		}

		// Initialize text label component centered in button
		const ButtonStyle& style = getCurrentStyle();
		Foundation::Vec2   centerPos = Foundation::Vec2{position.x + size.x * 0.5F, position.y + size.y * 0.5F};

		// Two-phase init (Text is non-aggregate due to IComponent base class with virtual destructor)
		labelText.position = centerPos;
		labelText.text = label;
		labelText.style.color = style.textColor;
		labelText.style.fontSize = style.fontSize;
		labelText.style.hAlign = Foundation::HorizontalAlign::Center;
		labelText.style.vAlign = Foundation::VerticalAlign::Middle;
		labelText.visible = visible;
		labelText.id = id;

		// Register with global FocusManager singleton
		FocusManager::Get().registerFocusable(this, tabIndex);
	}

	Button::~Button() {
		// Unregister from FocusManager
		FocusManager::Get().unregisterFocusable(this);
	}

	Button::Button(Button&& other) noexcept
		: position(other.position),
		  size(other.size),
		  label(std::move(other.label)),
		  state(other.state),
		  disabled(other.disabled),
		  focused(other.focused),
		  appearance(other.appearance),
		  onClick(std::move(other.onClick)),
		  visible(other.visible),
		  id(other.id),
		  mouseOver(other.mouseOver),
		  mouseDown(other.mouseDown),
		  labelText(other.labelText),
		  tabIndex(other.tabIndex) {
		// Unregister other from its old address, register this at new address
		FocusManager::Get().unregisterFocusable(&other);
		FocusManager::Get().registerFocusable(this, tabIndex);
		other.tabIndex = -2; // Mark as moved-from to prevent double-unregister
	}

	Button& Button::operator=(Button&& other) noexcept {
		if (this != &other) {
			// Unregister this from FocusManager
			FocusManager::Get().unregisterFocusable(this);

			// Move data
			position = other.position;
			size = other.size;
			label = std::move(other.label);
			state = other.state;
			disabled = other.disabled;
			focused = other.focused;
			appearance = other.appearance;
			onClick = std::move(other.onClick);
			visible = other.visible;
			id = other.id;
			mouseOver = other.mouseOver;
			mouseDown = other.mouseDown;
			labelText = other.labelText;
			tabIndex = other.tabIndex;

			// Unregister other from its old address, register this at new address
			FocusManager::Get().unregisterFocusable(&other);
			FocusManager::Get().registerFocusable(this, tabIndex);
			other.tabIndex = -2; // Mark as moved-from
		}
		return *this;
	}

	void Button::HandleInput() {
		// Skip input processing if disabled
		if (disabled) {
			state = State::Normal;
			mouseOver = false;
			mouseDown = false;
			return;
		}

		// Get input state from InputManager
		auto&			 input = engine::InputManager::Get();
		Foundation::Vec2 mousePos = input.getMousePosition();

		// Update mouse-over state
		bool wasMouseOver = mouseOver;
		mouseOver = containsPoint(mousePos);

		// Handle mouse button state
		bool isLeftButtonDown = input.isMouseButtonDown(engine::MouseButton::Left);
		bool wasMouseDown = mouseDown;

		// State transitions based on mouse input
		if (mouseOver) {
			if (isLeftButtonDown) {
				// Mouse pressed while over button
				state = State::Pressed;
				mouseDown = true;
			} else if (wasMouseDown && !isLeftButtonDown) {
				// Mouse released while over button - FIRE CLICK!
				if (onClick) {
					onClick();
				}
				state = State::Hover;
				mouseDown = false;
			} else {
				// Just hovering
				state = State::Hover;
				mouseDown = false;
			}
		} else {
			// Mouse not over button
			state = State::Normal;
			mouseDown = false;
		}
	}

	void Button::Update(float /*deltaTime*/) {
		// Update text component to match current button style
		const ButtonStyle& style = getCurrentStyle();

		// Position text at center of button - Text::Render() will handle Center/Middle alignment
		Foundation::Vec2 centerPos = Foundation::Vec2{position.x + size.x * 0.5F, position.y + size.y * 0.5F};

		labelText.position = centerPos;
		labelText.text = label;
		labelText.style.color = style.textColor;
		labelText.style.fontSize = style.fontSize;
		labelText.style.hAlign = Foundation::HorizontalAlign::Center;
		labelText.style.vAlign = Foundation::VerticalAlign::Middle;
		labelText.visible = visible;
	}

	void Button::Render() {
		if (!visible) {
			return;
		}

		// Get current style based on state
		const ButtonStyle& style = getCurrentStyle();

		// Draw background rectangle (batched)
		Foundation::Rect bounds{position.x, position.y, size.x, size.y};
		Renderer::Primitives::DrawRect({.bounds = bounds, .style = style.background, .id = id});

		// Draw label text using Text component
		labelText.Render();
	}

	bool Button::containsPoint(const Foundation::Vec2& point) const {
		return point.x >= position.x && point.x <= position.x + size.x && point.y >= position.y &&
			   point.y <= position.y + size.y;
	}

	const ButtonStyle& Button::getCurrentStyle() const {
		// Priority: Disabled > Focused > Pressed > Hover > Normal
		if (disabled) {
			return appearance.disabled;
		}
		if (focused) {
			return appearance.focused;
		}
		switch (state) {
			case State::Pressed:
				return appearance.pressed;
			case State::Hover:
				return appearance.hover;
			case State::Normal:
			default:
				return appearance.normal;
		}
	}

	// IFocusable interface implementation

	void Button::OnFocusGained() {
		focused = true;
	}

	void Button::OnFocusLost() {
		focused = false;
	}

	void Button::HandleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
		// Disabled buttons don't respond to keyboard input
		if (disabled) {
			return;
		}

		// Enter or Space activates the button
		if (key == engine::Key::Enter || key == engine::Key::Space) {
			if (onClick) {
				onClick();
			}
		}
	}

	void Button::HandleCharInput(char32_t /*codepoint*/) {
		// Button doesn't use character input
	}

	bool Button::CanReceiveFocus() const {
		// Only enabled buttons can receive focus
		return !disabled;
	}

} // namespace UI
