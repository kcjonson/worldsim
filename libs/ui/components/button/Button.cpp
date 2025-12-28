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
		  id(args.id),
		  iconSize(args.iconSize) {

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

		// Create icon if path provided
		if (!args.iconPath.empty()) {
			icon = std::make_unique<Icon>(Icon::Args{
				.position = {0.0F, 0.0F},  // Will be positioned in updateIconPosition
				.size = iconSize,
				.svgPath = args.iconPath,
				.tint = getCurrentStyle().textColor,
			});
		}

		// Initialize text label component
		const ButtonStyle& style = getCurrentStyle();
		labelText.text = label;
		labelText.style.color = style.textColor;
		labelText.style.fontSize = style.fontSize;
		labelText.style.hAlign = Foundation::HorizontalAlign::Center;
		labelText.style.vAlign = Foundation::VerticalAlign::Middle;
		labelText.visible = visible && !label.empty();
		labelText.id = id;

		// Position text and icon
		updateTextPosition();
		updateIconPosition();
		// FocusManager registration handled by FocusableBase constructor
	}

	// Destructor and move operations are = default in header.
	// FocusableBase handles FocusManager registration/unregistration.

	void Button::setPosition(float x, float y) {
		position = {x, y};
		updateTextPosition();
		updateIconPosition();
	}

	void Button::update(float /*deltaTime*/) {
		// Text position is updated in setPosition() - no per-frame work needed
	}

	void Button::updateTextPosition() {
		const ButtonStyle& style = getCurrentStyle();
		Foundation::Vec2 contentPos = getContentPosition();

		if (label.empty()) {
			// Icon-only button - no text to position
			labelText.visible = false;
			return;
		}

		if (icon) {
			// Icon + Label: position text to the right of icon
			constexpr float kIconLabelGap = 6.0F;
			float totalWidth = iconSize + kIconLabelGap + static_cast<float>(label.length()) * 7.0F;
			float startX = contentPos.x + (size.x - totalWidth) / 2.0F;
			float textX = startX + iconSize + kIconLabelGap + static_cast<float>(label.length()) * 3.5F;
			float centerY = contentPos.y + size.y * 0.5F;

			labelText.position = {textX, centerY};
		} else {
			// Label-only: center text
			Foundation::Vec2 centerPos = {contentPos.x + size.x * 0.5F, contentPos.y + size.y * 0.5F};
			labelText.position = centerPos;
		}

		labelText.text = label;
		labelText.style.color = style.textColor;
		labelText.style.fontSize = style.fontSize;
		labelText.style.hAlign = Foundation::HorizontalAlign::Center;
		labelText.style.vAlign = Foundation::VerticalAlign::Middle;
		labelText.visible = visible;
	}

	void Button::updateIconPosition() {
		if (!icon) {
			return;
		}

		Foundation::Vec2 contentPos = getContentPosition();
		float centerY = contentPos.y + (size.y - iconSize) / 2.0F;

		if (label.empty()) {
			// Icon-only: center the icon
			float centerX = contentPos.x + (size.x - iconSize) / 2.0F;
			icon->setPosition(centerX, centerY);
		} else {
			// Icon + Label: position icon to the left
			constexpr float kIconLabelGap = 6.0F;
			float totalWidth = iconSize + kIconLabelGap + static_cast<float>(label.length()) * 7.0F;
			float startX = contentPos.x + (size.x - totalWidth) / 2.0F;
			icon->setPosition(startX, centerY);
		}
	}

	void Button::updateIconTint() {
		if (!icon) {
			return;
		}
		const ButtonStyle& style = getCurrentStyle();
		icon->setTint(style.textColor);
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

		// Draw icon if present
		if (icon) {
			icon->setTint(style.textColor);
			icon->render();
		}

		// Draw label text using Text component (if visible)
		if (!label.empty()) {
			labelText.render();
		}
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
