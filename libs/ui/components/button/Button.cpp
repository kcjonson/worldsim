#include "components/button/Button.h"

#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "theme/Tokens.h"
#include "theme/Variants.h"

#include <input/InputManager.h>
#include <input/InputTypes.h>
#include <primitives/Primitives.h>

namespace UI {

	namespace {

		// drawText scale is relative to a 16px base.
		constexpr float kTextBasePx = 16.0F;

		float textScale(float sizePx) { return sizePx / kTextBasePx; }

		// Label font size derived from the button height (sm/md/lg ~ 26/34/46).
		float fontPxFor(float height) {
			if (height >= 42.0F) {
				return fs_md;
			}
			if (height >= 30.0F) {
				return fs_sm;
			}
			return fs_xs;
		}

		struct VariantStyle {
			Foundation::Color fill;
			Foundation::Color label;
			Foundation::Color border;
			bool			  hasBorder;
			bool			  gradientFill = false; // Primary only: accent_bright -> accent
		};

		VariantStyle styleFor(Button::Type type) {
			switch (type) {
				case Button::Type::Primary:
					return {accent, accent_contrast, accent_bright, true, true};
				case Button::Type::Ghost:
					return {Foundation::Color::transparent(), text_dim, {}, false};
				case Button::Type::Danger:
					return {Foundation::Color::transparent(), status_crit, status_crit, true};
				case Button::Type::Data:
					return {withAlpha(data, 0.12F), data_bright, data, true};
				case Button::Type::Secondary:
				case Button::Type::Custom: // resolved before styleFor is called
					break;
			}
			return {Foundation::Color::transparent(), text, line_edge, true};
		}

		const ButtonStyle& customStyleFor(const ButtonAppearance& a, Button::State state, bool disabled, bool focused) {
			if (disabled) {
				return a.disabled;
			}
			if (focused) {
				return a.focused;
			}
			switch (state) {
				case Button::State::Pressed:
					return a.pressed;
				case Button::State::Hover:
					return a.hover;
				case Button::State::Normal:
					break;
			}
			return a.normal;
		}

	} // namespace

	Button::Button(const Args& args)
		: FocusableBase<Button>(args.tabIndex)
		, label(args.label)
		, disabled(args.disabled)
		, type(args.type)
		, onClick(args.onClick)
		, id(args.id)
		, iconSize(args.iconSize) {

		position = args.position;
		size = args.size;
		margin = args.margin;

		if (type == Type::Custom && args.customAppearance != nullptr) {
			appearance = *args.customAppearance;
		}

		if (!args.iconPath.empty()) {
			icon = std::make_unique<Icon>(Icon::Args{
				.position = {0.0F, 0.0F},
				.size = iconSize,
				.svgPath = args.iconPath,
				.tint = text_bright,
			});
		}

		updateIconPosition();
	}

	void Button::setPosition(float x, float y) {
		position = {x, y};
		updateIconPosition();
	}

	void Button::setLabel(const std::string& newLabel) {
		label = newLabel;
		updateIconPosition();
	}

	void Button::update(float /*deltaTime*/) {}

	void Button::updateIconPosition() {
		if (!icon) {
			return;
		}
		Foundation::Vec2 contentPos = getContentPosition();
		float			 centerY = contentPos.y + ((size.y - iconSize) / 2.0F);
		if (label.empty()) {
			// Icon-only: center the icon.
			float centerX = contentPos.x + ((size.x - iconSize) / 2.0F);
			icon->setPosition(centerX, centerY);
		} else {
			// Icon sits just left of the centered label block.
			constexpr float kIconLabelGap = 6.0F;
			float			centerX = contentPos.x + (size.x * 0.5F) - iconSize - kIconLabelGap;
			icon->setPosition(centerX, centerY);
		}
	}

	void Button::render() {
		if (!visible) {
			return;
		}
		using Renderer::Primitives::drawRect;
		using Renderer::Primitives::drawText;

		const Foundation::Vec2 contentPos = getContentPosition();
		const Foundation::Rect bounds{contentPos.x, contentPos.y, size.x, size.y};

		Foundation::Color textColor;
		float			  fontPx = fontPxFor(size.y);

		if (type == Type::Custom) {
			const ButtonStyle& cs = customStyleFor(appearance, state, disabled, focused);
			drawRect({.bounds = bounds, .style = cs.background, .id = id});
			textColor = cs.textColor;
			fontPx = cs.fontSize;
		} else {
			const VariantStyle vs = styleFor(type);

			Foundation::RectStyle rs{.fill = vs.fill};
			if (vs.gradientFill) {
				rs.gradient = Foundation::LinearGradient{.from = accent_bright, .to = accent, .horizontal = false};
			}
			if (vs.hasBorder) {
				rs.border = Foundation::BorderStyle{.color = vs.border, .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside};
			}
			drawRect({.bounds = bounds, .style = rs, .id = id});

			// State overlays, rounded to match the button corners.
			const auto overlay = [&](Foundation::Color c) {
				drawRect({.bounds = bounds,
						  .style = {.fill = c,
									.border = Foundation::BorderStyle{
										.color = c, .width = 0.0F, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Inside}}});
			};
			if (disabled) {
				overlay(withAlpha(bg_void, 0.45F));
			} else if (state == State::Hover) {
				overlay(bg_hover);
			} else if (state == State::Pressed) {
				overlay(withAlpha(bg_void, 0.22F));
			}
			if (focused && !disabled) {
				drawRect({.bounds = bounds,
						  .style = {.fill = Foundation::Color::transparent(),
									.border = Foundation::BorderStyle{
										.color = accent_bright, .width = bw, .cornerRadius = r_sm, .position = Foundation::BorderPosition::Outside}}});
			}

			textColor = disabled ? text_disabled : vs.label;
		}

		if (icon) {
			icon->setTint(textColor);
			updateIconPosition();
			icon->render();
		}

		if (!label.empty()) {
			drawText({.text = label,
					  .position = bounds.position(),
					  .scale = textScale(fontPx),
					  .color = textColor,
					  .font = fontDisplay,
					  .hAlign = Foundation::HorizontalAlign::Center,
					  .vAlign = Foundation::VerticalAlign::Middle,
					  .boxWidth = bounds.width,
					  .boxHeight = bounds.height,
					  .letterSpacing = fontPx * ls_wide,
					  .transform = Foundation::TextTransform::Uppercase,
					  .id = id});
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
				mouseOver = containsPoint(event.position);
				if (!mouseDown) {
					state = mouseOver ? State::Hover : State::Normal;
				}
				break;

			case InputEvent::Type::Scroll:
				break;
		}
		return false;
	}

	// IFocusable interface implementation

	void Button::onFocusGained() { focused = true; }

	void Button::onFocusLost() { focused = false; }

	void Button::handleKeyInput(engine::Key key, bool /*shift*/, bool /*ctrl*/, bool /*alt*/) {
		if (disabled) {
			return;
		}
		if (key == engine::Key::Enter || key == engine::Key::Space) {
			if (onClick) {
				onClick();
			}
		}
	}

	void Button::handleCharInput(char32_t /*codepoint*/) {}

	bool Button::canReceiveFocus() const { return !disabled; }

} // namespace UI
