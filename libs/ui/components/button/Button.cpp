#include "components/button/Button.h"
#include "font/FontRenderer.h"
#include <input/InputManager.h>
#include <input/InputTypes.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

namespace UI {

	Button::Button(const Args& args)
		: FocusableBase<Button>(args.tabIndex),
		  label(args.label),
		  disabled(args.disabled),
		  onClick(args.onClick),
		  id(args.id) {

		// Initialize base class members (position, size, margin from Component/IComponent)
		position = args.position;
		size = args.size;
		margin = args.margin;

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
		// Use getContentPosition() to account for margin
		const ButtonStyle& style = getCurrentStyle();
		Foundation::Vec2   contentPos = getContentPosition();
		Foundation::Vec2   centerPos = {contentPos.x + size.x * 0.5F, contentPos.y + size.y * 0.5F};

		// Two-phase init (Text is non-aggregate due to IComponent base class with virtual destructor)
		labelText.position = centerPos;
		labelText.text = label;
		labelText.style.color = style.textColor;
		labelText.style.fontSize = style.fontSize;
		labelText.style.hAlign = Foundation::HorizontalAlign::Center;
		labelText.style.vAlign = Foundation::VerticalAlign::Middle;
		labelText.visible = visible;
		labelText.id = id;
		// FocusManager registration handled by FocusableBase constructor
	}

	// Destructor and move operations are = default in header.
	// FocusableBase handles FocusManager registration/unregistration.

	void Button::setPosition(float x, float y) {
		position = {x, y};
		updateTextPosition();
	}

	void Button::update(float /*deltaTime*/) {
		// Update text component to match current button style
		updateTextPosition();
	}

	void Button::updateTextPosition() {
		const ButtonStyle& style = getCurrentStyle();

		// Position text at center of button content area (accounting for margin)
		Foundation::Vec2 contentPos = getContentPosition();
		Foundation::Vec2 centerPos = {contentPos.x + size.x * 0.5F, contentPos.y + size.y * 0.5F};

		labelText.position = centerPos;
		labelText.text = label;
		labelText.style.color = style.textColor;
		labelText.style.fontSize = style.fontSize;
		labelText.style.hAlign = Foundation::HorizontalAlign::Center;
		labelText.style.vAlign = Foundation::VerticalAlign::Middle;
		labelText.visible = visible;
	}

	void Button::render() {
		if (!visible) {
			return;
		}

		// Get current style based on state
		const ButtonStyle& style = getCurrentStyle();

		// Draw background rectangle at content position (accounting for margin)
		Foundation::Vec2 contentPos = getContentPosition();
		Foundation::Rect bounds{contentPos.x, contentPos.y, size.x, size.y};
		Renderer::Primitives::drawRect({.bounds = bounds, .style = style.background, .id = id});

		// Draw label text using Text component
		labelText.render();
	}

	bool Button::containsPoint(Foundation::Vec2 point) const {
		// Hit testing includes the margin area
		return point.x >= position.x && point.x <= position.x + getWidth() && point.y >= position.y && point.y <= position.y + getHeight();
	}

	bool Button::handleEvent(InputEvent& event) {
		if (disabled || !visible) {
			return false;
		}

		switch (event.type) {
			case InputEvent::Type::MouseDown:
				if (containsPoint(event.position) && event.button == engine::MouseButton::Left) {
					state = State::Pressed;
					mouseDown = true;
					event.consume();
					return true;
				}
				break;

			case InputEvent::Type::MouseUp:
				if (mouseDown && event.button == engine::MouseButton::Left) {
					if (containsPoint(event.position)) {
						// Mouse released while over button - fire click!
						if (onClick) {
							onClick();
						}
						state = State::Hover;
					} else {
						state = State::Normal;
					}
					mouseDown = false;
					event.consume();
					return true;
				}
				break;

			case InputEvent::Type::MouseMove:
				// Update hover state - don't consume, allow other components to also update hover
				mouseOver = containsPoint(event.position);
				if (!mouseDown) {
					state = mouseOver ? State::Hover : State::Normal;
				}
				break;

			case InputEvent::Type::Scroll:
				// Buttons don't handle scroll
				break;
		}
		return false;
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

	void Button::onFocusGained() {
		focused = true;
	}

	void Button::onFocusLost() {
		focused = false;
	}

	void Button::handleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
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

	void Button::handleCharInput(char32_t /*codepoint*/) {
		// Button doesn't use character input
	}

	bool Button::canReceiveFocus() const {
		// Only enabled buttons can receive focus
		return !disabled;
	}

} // namespace UI
