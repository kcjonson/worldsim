#include "components/button/button.h"
#include "font/font_renderer.h"
#include <input/input_manager.h>
#include <input/input_types.h>
#include <primitives/primitives.h>
#include <utils/log.h>

namespace UI {

	Button::Button(const Args& args)
		: m_position(args.position),
		  m_size(args.size),
		  m_label(args.label),
		  m_disabled(args.disabled),
		  m_onClick(args.onClick),
		  zIndex(args.zIndex),
		  id(args.id) {

		// Set appearance based on type
		if (args.type == Type::Primary) {
			m_appearance = ButtonStyles::Primary();
		} else if (args.type == Type::Secondary) {
			m_appearance = ButtonStyles::Secondary();
		} else if (args.type == Type::Custom && args.customAppearance != nullptr) {
			m_appearance = *args.customAppearance;
		} else {
			// Default to Primary if Custom but no appearance provided
			m_appearance = ButtonStyles::Primary();
		}

		// Initialize text label component centered in button
		const ButtonStyle& style = GetCurrentStyle();
		Foundation::Vec2   centerPos = Foundation::Vec2{m_position.x + m_size.x * 0.5F, m_position.y + m_size.y * 0.5F};

		m_labelText = Text{
			.position = centerPos,
			.text = m_label,
			.style =
				{.color = style.textColor,
				 .fontSize = style.fontSize,
				 .hAlign = Foundation::HorizontalAlign::Center,
				 .vAlign = Foundation::VerticalAlign::Middle},
			.zIndex = zIndex + 0.1F, // Slightly above button background
			.visible = visible,
			.id = id
		};
	}

	void Button::HandleInput() {
		// Skip input processing if disabled
		if (m_disabled) {
			m_state = State::Normal;
			m_mouseOver = false;
			m_mouseDown = false;
			return;
		}

		// Get input state from InputManager
		auto&			 input = engine::InputManager::Get();
		Foundation::Vec2 mousePos = input.GetMousePosition();

		// Update mouse-over state
		bool wasMouseOver = m_mouseOver;
		m_mouseOver = ContainsPoint(mousePos);

		// Handle mouse button state
		bool isLeftButtonDown = input.IsMouseButtonDown(engine::MouseButton::Left);
		bool wasMouseDown = m_mouseDown;

		// State transitions based on mouse input
		if (m_mouseOver) {
			if (isLeftButtonDown) {
				// Mouse pressed while over button
				m_state = State::Pressed;
				m_mouseDown = true;
			} else if (wasMouseDown && !isLeftButtonDown) {
				// Mouse released while over button - FIRE CLICK!
				if (m_onClick) {
					m_onClick();
				}
				m_state = State::Hover;
				m_mouseDown = false;
			} else {
				// Just hovering
				m_state = State::Hover;
				m_mouseDown = false;
			}
		} else {
			// Mouse not over button
			m_state = State::Normal;
			m_mouseDown = false;
		}

		// Handle keyboard input for focused buttons
		if (m_focused && !m_disabled) {
			// Enter or Space activates the button
			if (input.IsKeyPressed(engine::Key::Enter) || input.IsKeyPressed(engine::Key::Space)) {
				if (m_onClick) {
					m_onClick();
				}
			}
		}
	}

	void Button::Update(float /*deltaTime*/) {
		// Update text component to match current button style
		const ButtonStyle& style = GetCurrentStyle();

		// Position text at center of button - Text::Render() will handle Center/Middle alignment
		Foundation::Vec2 centerPos = Foundation::Vec2{m_position.x + m_size.x * 0.5F, m_position.y + m_size.y * 0.5F};

		m_labelText.position = centerPos;
		m_labelText.text = m_label;
		m_labelText.style.color = style.textColor;
		m_labelText.style.fontSize = style.fontSize;
		m_labelText.style.hAlign = Foundation::HorizontalAlign::Center;
		m_labelText.style.vAlign = Foundation::VerticalAlign::Middle;
		m_labelText.visible = visible;
	}

	void Button::Render() const {
		if (!visible) {
			return;
		}

		// Get current style based on state
		const ButtonStyle& style = GetCurrentStyle();

		// Draw background rectangle (batched)
		Foundation::Rect bounds{m_position.x, m_position.y, m_size.x, m_size.y};
		Renderer::Primitives::DrawRect({.bounds = bounds, .style = style.background, .id = id});

		// Draw label text using Text component
		m_labelText.Render();
	}

	bool Button::ContainsPoint(const Foundation::Vec2& point) const {
		return point.x >= m_position.x && point.x <= m_position.x + m_size.x && point.y >= m_position.y &&
			   point.y <= m_position.y + m_size.y;
	}

	const ButtonStyle& Button::GetCurrentStyle() const {
		// Priority: Disabled > Focused > Pressed > Hover > Normal
		if (m_disabled) {
			return m_appearance.disabled;
		}
		if (m_focused) {
			return m_appearance.focused;
		}
		switch (m_state) {
			case State::Pressed:
				return m_appearance.pressed;
			case State::Hover:
				return m_appearance.hover;
			case State::Normal:
			default:
				return m_appearance.normal;
		}
	}

} // namespace UI
