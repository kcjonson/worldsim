#include "components/text_input/text_input.h"
#include "focus/focus_manager.h"
#include "font/font_renderer.h"
#include "shapes/shapes.h"
#include "utils/utf8.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <input/input_manager.h>
#include <primitives/primitives.h>
#include <utils/log.h>

namespace UI {

// ============================================================================
// Constructor / Destructor
// ============================================================================

TextInput::TextInput(const Args& args)
	: m_position(args.position),
	  m_size(args.size),
	  m_text(args.text),
	  m_placeholder(args.placeholder),
	  m_style(args.style),
	  m_onChange(args.onChange),
	  m_charValidator(args.charValidator),
	  zIndex(args.zIndex),
	  id(args.id),
	  m_enabled(args.enabled),
	  m_focusManager(args.focusManager) {

	// Set cursor to end of text
	m_cursorPosition = m_text.size();

	// Register with FocusManager if provided
	if (m_focusManager != nullptr) {
		m_focusManager->RegisterFocusable(this, args.tabIndex);
	}
}

TextInput::~TextInput() {
	// Unregister from FocusManager if registered
	if (m_focusManager != nullptr) {
		m_focusManager->UnregisterFocusable(this);
	}
}

TextInput::TextInput(TextInput&& other) noexcept
	: m_position(other.m_position),
	  m_size(other.m_size),
	  m_text(std::move(other.m_text)),
	  m_placeholder(std::move(other.m_placeholder)),
	  m_style(other.m_style),
	  m_onChange(std::move(other.m_onChange)),
	  m_charValidator(std::move(other.m_charValidator)),
	  m_cursorPosition(other.m_cursorPosition),
	  m_selection(other.m_selection),
	  m_cursorBlinkTimer(other.m_cursorBlinkTimer),
	  m_horizontalScroll(other.m_horizontalScroll),
	  zIndex(other.zIndex),
	  visible(other.visible),
	  id(other.id),
	  m_enabled(other.m_enabled),
	  m_focused(other.m_focused),
	  m_focusManager(other.m_focusManager),
	  m_mouseDown(other.m_mouseDown) {
	// Unregister other from its old address, register this at new address
	if (m_focusManager != nullptr) {
		m_focusManager->UnregisterFocusable(&other);
		m_focusManager->RegisterFocusable(this, -1);
	}
	other.m_focusManager = nullptr; // Prevent double-unregister
}

TextInput& TextInput::operator=(TextInput&& other) noexcept {
	if (this != &other) {
		// Unregister this from old FocusManager
		if (m_focusManager != nullptr) {
			m_focusManager->UnregisterFocusable(this);
		}

		// Move data
		m_position = other.m_position;
		m_size = other.m_size;
		m_text = std::move(other.m_text);
		m_placeholder = std::move(other.m_placeholder);
		m_style = other.m_style;
		m_onChange = std::move(other.m_onChange);
		m_charValidator = std::move(other.m_charValidator);
		m_cursorPosition = other.m_cursorPosition;
		m_selection = other.m_selection;
		m_cursorBlinkTimer = other.m_cursorBlinkTimer;
		m_horizontalScroll = other.m_horizontalScroll;
		zIndex = other.zIndex;
		visible = other.visible;
		id = other.id;
		m_enabled = other.m_enabled;
		m_focused = other.m_focused;
		m_mouseDown = other.m_mouseDown;
		m_focusManager = other.m_focusManager;

		// Unregister other from its old address, register this at new address
		if (m_focusManager != nullptr) {
			m_focusManager->UnregisterFocusable(&other);
			m_focusManager->RegisterFocusable(this, -1);
		}
		other.m_focusManager = nullptr;
	}
	return *this;
}

// ============================================================================
// Lifecycle Methods
// ============================================================================

void TextInput::HandleInput() {
	if (!m_enabled) {
		return;
	}

	auto& input = engine::InputManager::Get();

	// Check for mouse click
	if (input.IsMouseButtonPressed(engine::MouseButton::Left)) {
		Foundation::Vec2 mousePos = input.GetMousePosition();

		// Check if click is inside text input
		if (ContainsPoint(mousePos)) {
			// Grab focus
			if (m_focusManager != nullptr) {
				m_focusManager->SetFocus(this);
			}

			// Position cursor from mouse X
			float localX = mousePos.x - m_position.x - m_style.paddingLeft + m_horizontalScroll;
			m_cursorPosition = GetCursorPositionFromMouse(localX);
			m_cursorBlinkTimer = 0.0F;

			// Phase 2: Selection will be handled here
		}
	}
}

void TextInput::Update(float deltaTime) {
	if (m_focused) {
		m_cursorBlinkTimer += deltaTime;
		if (m_cursorBlinkTimer > m_style.cursorBlinkRate) {
			m_cursorBlinkTimer -= m_style.cursorBlinkRate;
		}
	}
}

void TextInput::Render() const {
	if (!visible) {
		return;
	}

	RenderBackground();

	// Scissor test to clip overflowing text
	float textAreaX = m_position.x + m_style.paddingLeft;
	float textAreaY = m_position.y + m_style.paddingTop;
	float textAreaWidth = m_size.x - m_style.paddingLeft - m_style.paddingRight;
	float textAreaHeight = m_size.y - m_style.paddingTop - m_style.paddingBottom;

	Renderer::Primitives::PushScissor(Foundation::Rect{textAreaX, textAreaY, textAreaWidth, textAreaHeight});

	if (m_text.empty() && !m_focused) {
		RenderPlaceholder();
	} else {
		RenderSelection(); // Phase 2 (currently stub)
		RenderText();
		RenderCursor();
	}

	Renderer::Primitives::PopScissor();
}

// ============================================================================
// State Management
// ============================================================================

void TextInput::SetText(const std::string& text) {
	m_text = text;
	m_cursorPosition = std::min(m_cursorPosition, m_text.size());
	UpdateHorizontalScroll();
	if (m_onChange) {
		m_onChange(m_text);
	}
}

// ============================================================================
// IFocusable Interface
// ============================================================================

void TextInput::OnFocusGained() {
	m_focused = true;
	m_cursorBlinkTimer = 0.0F; // Reset blink (cursor visible)
}

void TextInput::OnFocusLost() {
	m_focused = false;
	// Phase 2: Clear selection when focus lost
}

void TextInput::HandleKeyInput(engine::Key key, bool shift, bool ctrl, bool /*alt*/) {
	if (!m_enabled || !m_focused) {
		return;
	}

	// Phase 2: Shift extends selection
	// For now, just handle basic navigation

	switch (key) {
		case engine::Key::Left:
			MoveCursorLeft();
			break;
		case engine::Key::Right:
			MoveCursorRight();
			break;
		case engine::Key::Home:
			MoveCursorHome();
			break;
		case engine::Key::End:
			MoveCursorEnd();
			break;
		case engine::Key::Delete:
			DeleteCharAtCursor();
			break;
		case engine::Key::Backspace:
			DeleteCharBeforeCursor();
			break;
		// Phase 2: Clipboard operations
		default:
			break;
	}
}

void TextInput::HandleCharInput(char32_t codepoint) {
	if (!m_enabled || !m_focused) {
		return;
	}

	// Filter control characters (except Tab which may be used)
	if (codepoint < 32 || codepoint == 127) {
		return; // Skip control characters
	}

	InsertChar(codepoint);
}

bool TextInput::CanReceiveFocus() const {
	return m_enabled;
}

// ============================================================================
// Phase 1: Core Editing Operations
// ============================================================================

void TextInput::InsertChar(char32_t codepoint) {
	// Validate character (if validator provided)
	if (m_charValidator && !m_charValidator(codepoint)) {
		return; // Reject character
	}

	// Phase 2: Delete selection first if active
	// (Not implemented in Phase 1)

	// Convert codepoint to UTF-8
	std::string utf8 = foundation::UTF8::Encode(codepoint);

	// Insert at cursor position
	m_text.insert(m_cursorPosition, utf8);
	m_cursorPosition += utf8.size();

	// Reset cursor blink
	m_cursorBlinkTimer = 0.0F;

	// Update scroll and notify
	UpdateHorizontalScroll();
	if (m_onChange) {
		m_onChange(m_text);
	}
}

void TextInput::DeleteCharAtCursor() {
	// Phase 2: Delete selection if active
	// (Not implemented in Phase 1)

	if (m_cursorPosition >= m_text.size()) {
		return; // At end
	}

	// Find next UTF-8 character boundary
	size_t charSize = foundation::UTF8::CharacterSize(static_cast<unsigned char>(m_text[m_cursorPosition]));
	m_text.erase(m_cursorPosition, charSize);

	m_cursorBlinkTimer = 0.0F;
	UpdateHorizontalScroll();
	if (m_onChange) {
		m_onChange(m_text);
	}
}

void TextInput::DeleteCharBeforeCursor() {
	// Phase 2: Delete selection if active
	// (Not implemented in Phase 1)

	if (m_cursorPosition == 0) {
		return; // At start
	}

	// Find previous UTF-8 character boundary
	size_t charSize = foundation::UTF8::PreviousCharacterSize(m_text, m_cursorPosition);
	m_text.erase(m_cursorPosition - charSize, charSize);
	m_cursorPosition -= charSize;

	m_cursorBlinkTimer = 0.0F;
	UpdateHorizontalScroll();
	if (m_onChange) {
		m_onChange(m_text);
	}
}

void TextInput::MoveCursorLeft() {
	if (m_cursorPosition == 0) {
		return;
	}

	size_t charSize = foundation::UTF8::PreviousCharacterSize(m_text, m_cursorPosition);
	m_cursorPosition -= charSize;
	m_cursorBlinkTimer = 0.0F;
	UpdateHorizontalScroll();
}

void TextInput::MoveCursorRight() {
	if (m_cursorPosition >= m_text.size()) {
		return;
	}

	size_t charSize = foundation::UTF8::CharacterSize(static_cast<unsigned char>(m_text[m_cursorPosition]));
	m_cursorPosition += charSize;
	m_cursorBlinkTimer = 0.0F;
	UpdateHorizontalScroll();
}

void TextInput::MoveCursorHome() {
	m_cursorPosition = 0;
	m_cursorBlinkTimer = 0.0F;
	UpdateHorizontalScroll();
}

void TextInput::MoveCursorEnd() {
	m_cursorPosition = m_text.size();
	m_cursorBlinkTimer = 0.0F;
	UpdateHorizontalScroll();
}

// ============================================================================
// Phase 2: Selection Operations (STUBS - will implement in Phase 2)
// ============================================================================

void TextInput::SetSelection(size_t /*start*/, size_t /*end*/) {
	// TODO: Implement in Phase 2
}

void TextInput::ClearSelection() {
	// TODO: Implement in Phase 2
}

std::string TextInput::GetSelectedText() const {
	// TODO: Implement in Phase 2
	return "";
}

void TextInput::DeleteSelection() {
	// TODO: Implement in Phase 2
}

void TextInput::ExtendSelectionLeft() {
	// TODO: Implement in Phase 2
}

void TextInput::ExtendSelectionRight() {
	// TODO: Implement in Phase 2
}

// ============================================================================
// Phase 2: Clipboard Operations (STUBS - will implement in Phase 2)
// ============================================================================

void TextInput::Copy() {
	// TODO: Implement in Phase 2
}

void TextInput::Cut() {
	// TODO: Implement in Phase 2
}

void TextInput::Paste() {
	// TODO: Implement in Phase 2
}

void TextInput::SelectAll() {
	// TODO: Implement in Phase 2
}

// ============================================================================
// Rendering Helpers
// ============================================================================

void TextInput::RenderBackground() const {
	Foundation::Color borderColor = m_focused ? m_style.focusedBorderColor : m_style.borderColor;

	Foundation::RectStyle rectStyle{
		.fill = m_style.backgroundColor,
		.border = Foundation::BorderStyle{
			.color = borderColor,
			.width = m_style.borderWidth,
			.cornerRadius = m_style.cornerRadius
		}
	};

	Renderer::Primitives::DrawRect({.bounds = {m_position.x, m_position.y, m_size.x, m_size.y},
									.style = rectStyle,
									.id = id,
									.zIndex = static_cast<int>(zIndex)});
}

void TextInput::RenderText() const {
	if (m_text.empty()) {
		return;
	}

	float textX = m_position.x + m_style.paddingLeft - m_horizontalScroll;
	float textY = m_position.y + m_style.paddingTop;

	// Use Text component for rendering
	UI::Text textComponent{
		.position = {textX, textY},
		.text = m_text,
		.style = {
			.color = m_style.textColor,
			.fontSize = m_style.fontSize,
			.hAlign = Foundation::HorizontalAlign::Left,
			.vAlign = Foundation::VerticalAlign::Top
		},
		.zIndex = zIndex + 0.1F,
		.visible = true,
		.id = id
	};

	textComponent.Render();
}

void TextInput::RenderCursor() const {
	if (!m_focused) {
		return;
	}

	// Phase 2: Don't render cursor when selection active
	// (Not implemented in Phase 1)

	// Blink: visible for first half of cycle
	bool visible = m_cursorBlinkTimer < (m_style.cursorBlinkRate * 0.5F);
	if (!visible) {
		return;
	}

	float cursorX = GetCursorXPosition();
	float textAreaY = m_position.y + m_style.paddingTop;
	float textHeight = m_style.fontSize;

	Renderer::Primitives::DrawLine({.start = {cursorX, textAreaY},
									.end = {cursorX, textAreaY + textHeight},
									.style = Foundation::LineStyle{
										.color = m_style.cursorColor,
										.width = m_style.cursorWidth
									},
									.id = id,
									.zIndex = static_cast<int>(zIndex + 0.2F)});
}

void TextInput::RenderSelection() const {
	// TODO: Implement in Phase 2
}

void TextInput::RenderPlaceholder() const {
	if (m_placeholder.empty()) {
		return;
	}

	float textX = m_position.x + m_style.paddingLeft;
	float textY = m_position.y + m_style.paddingTop;

	UI::Text placeholderComponent{
		.position = {textX, textY},
		.text = m_placeholder,
		.style = {
			.color = m_style.placeholderColor,
			.fontSize = m_style.fontSize,
			.hAlign = Foundation::HorizontalAlign::Left,
			.vAlign = Foundation::VerticalAlign::Top
		},
		.zIndex = zIndex + 0.1F,
		.visible = true,
		.id = id
	};

	placeholderComponent.Render();
}

// ============================================================================
// Utilities
// ============================================================================

float TextInput::GetCursorXPosition() const {
	// Get text up to cursor position
	std::string textBeforeCursor = m_text.substr(0, m_cursorPosition);

	// Measure width using FontRenderer
	ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
	float width = fontRenderer ? fontRenderer->MeasureText(textBeforeCursor, m_style.fontSize).x : 0.0F;

	// Account for padding and scroll
	return m_position.x + m_style.paddingLeft + width - m_horizontalScroll;
}

size_t TextInput::GetCursorPositionFromMouse(float localX) const {
	// localX is already relative to text area (with scroll applied)

	// Binary search for closest character boundary
	size_t bestPosition = 0;
	float  bestDistance = std::abs(localX);

	// Check each character boundary
	ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
	for (size_t i = 0; i <= m_text.size(); ) {
		std::string textUpToPos = m_text.substr(0, i);
		float		width = fontRenderer ? fontRenderer->MeasureText(textUpToPos, m_style.fontSize).x : 0.0F;
		float		distance = std::abs(width - localX);

		if (distance < bestDistance) {
			bestDistance = distance;
			bestPosition = i;
		}

		// Move to next character boundary
		if (i >= m_text.size()) {
			break;
		}
		size_t charSize = foundation::UTF8::CharacterSize(static_cast<unsigned char>(m_text[i]));
		i += charSize;
	}

	return bestPosition;
}

void TextInput::UpdateHorizontalScroll() {
	float cursorX = GetCursorXPosition() - m_position.x - m_style.paddingLeft + m_horizontalScroll;
	float visibleWidth = m_size.x - m_style.paddingLeft - m_style.paddingRight;

	// Scroll right if cursor is past right edge
	if (cursorX > visibleWidth) {
		m_horizontalScroll += (cursorX - visibleWidth);
	}

	// Scroll left if cursor is past left edge
	if (cursorX < 0) {
		m_horizontalScroll += cursorX; // cursorX is negative
	}

	// Clamp to ensure text fills from left when possible
	ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
	float textWidth = fontRenderer ? fontRenderer->MeasureText(m_text, m_style.fontSize).x : 0.0F;
	if (textWidth < visibleWidth) {
		m_horizontalScroll = 0.0F; // No scroll needed
	} else {
		m_horizontalScroll = std::max(0.0F, std::min(m_horizontalScroll, textWidth - visibleWidth));
	}
}

// ============================================================================
// Geometry Queries
// ============================================================================

bool TextInput::ContainsPoint(const Foundation::Vec2& point) const {
	return point.x >= m_position.x && point.x <= m_position.x + m_size.x && point.y >= m_position.y &&
		   point.y <= m_position.y + m_size.y;
}

} // namespace UI
