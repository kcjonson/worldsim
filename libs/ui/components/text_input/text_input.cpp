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

				// Clear selection on click (will be recreated if dragging)
				ClearSelection();

				// Track mouse down for drag selection
				m_mouseDown = true;
			}
		}

		// Mouse drag selection
		if (m_mouseDown && input.IsMouseButtonDown(engine::MouseButton::Left)) {
			Foundation::Vec2 mousePos = input.GetMousePosition();
			float			 localX = mousePos.x - m_position.x - m_style.paddingLeft + m_horizontalScroll;
			size_t			 dragPosition = GetCursorPositionFromMouse(localX);

			// Create/update selection from original cursor position to drag position
			if (dragPosition != m_cursorPosition) {
				// Selection anchor is where mouse was first pressed (already set as m_cursorPosition)
				// Selection head is current drag position
				if (!m_selection.has_value()) {
					// Start new selection
					SetSelection(m_cursorPosition, dragPosition);
				} else {
					// Update existing selection
					m_selection->end = dragPosition;
				}
				m_cursorPosition = dragPosition; // Move cursor to drag position
				UpdateHorizontalScroll();
			}
		}

		// Release mouse
		if (m_mouseDown && !input.IsMouseButtonDown(engine::MouseButton::Left)) {
			m_mouseDown = false;
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
		ClearSelection();
	}

	void TextInput::HandleKeyInput(engine::Key key, bool shift, bool ctrl, bool /*alt*/) {
		if (!m_enabled || !m_focused) {
			return;
		}

		// Clipboard operations (Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+A)
		if (ctrl) {
			switch (key) {
				case engine::Key::C:
					Copy();
					return;
				case engine::Key::X:
					Cut();
					return;
				case engine::Key::V:
					Paste();
					return;
				case engine::Key::A:
					SelectAll();
					return;
				default:
					break;
			}
		}

		// Navigation and editing
		switch (key) {
			case engine::Key::Left:
				if (shift) {
					ExtendSelectionLeft();
				} else {
					ClearSelection();
					MoveCursorLeft();
				}
				break;
			case engine::Key::Right:
				if (shift) {
					ExtendSelectionRight();
				} else {
					ClearSelection();
					MoveCursorRight();
				}
				break;
			case engine::Key::Home:
				ClearSelection();
				MoveCursorHome();
				break;
			case engine::Key::End:
				ClearSelection();
				MoveCursorEnd();
				break;
			case engine::Key::Delete:
				DeleteCharAtCursor();
				break;
			case engine::Key::Backspace:
				DeleteCharBeforeCursor();
				break;
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

		// Delete selection first if active
		if (m_selection.has_value() && !m_selection->IsEmpty()) {
			DeleteSelection();
		}

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
		// Delete selection if active
		if (m_selection.has_value() && !m_selection->IsEmpty()) {
			DeleteSelection();
			return;
		}

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
		// Delete selection if active
		if (m_selection.has_value() && !m_selection->IsEmpty()) {
			DeleteSelection();
			return;
		}

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
	// Phase 2: Selection Operations
	// ============================================================================

	void TextInput::SetSelection(size_t start, size_t end) {
		m_selection = TextSelection{start, end};
		m_cursorBlinkTimer = 0.0F;
	}

	void TextInput::ClearSelection() {
		m_selection.reset();
	}

	std::string TextInput::GetSelectedText() const {
		if (!m_selection.has_value() || m_selection->IsEmpty()) {
			return "";
		}

		size_t start = m_selection->GetMin();
		size_t end = m_selection->GetMax();
		return m_text.substr(start, end - start);
	}

	void TextInput::DeleteSelection() {
		if (!m_selection.has_value() || m_selection->IsEmpty()) {
			return;
		}

		size_t start = m_selection->GetMin();
		size_t end = m_selection->GetMax();

		// Delete the selected range
		m_text.erase(start, end - start);

		// Move cursor to start of deleted range
		m_cursorPosition = start;

		// Clear selection
		ClearSelection();

		// Update and notify
		m_cursorBlinkTimer = 0.0F;
		UpdateHorizontalScroll();
		if (m_onChange) {
			m_onChange(m_text);
		}
	}

	void TextInput::ExtendSelectionLeft() {
		if (m_cursorPosition == 0) {
			return;
		}

		// If no selection exists, create one starting at current cursor
		if (!m_selection.has_value()) {
			m_selection = TextSelection{m_cursorPosition, m_cursorPosition};
		}

		// Move cursor left (selection end)
		size_t charSize = foundation::UTF8::PreviousCharacterSize(m_text, m_cursorPosition);
		m_cursorPosition -= charSize;
		m_selection->end = m_cursorPosition;

		m_cursorBlinkTimer = 0.0F;
		UpdateHorizontalScroll();
	}

	void TextInput::ExtendSelectionRight() {
		if (m_cursorPosition >= m_text.size()) {
			return;
		}

		// If no selection exists, create one starting at current cursor
		if (!m_selection.has_value()) {
			m_selection = TextSelection{m_cursorPosition, m_cursorPosition};
		}

		// Move cursor right (selection end)
		size_t charSize = foundation::UTF8::CharacterSize(static_cast<unsigned char>(m_text[m_cursorPosition]));
		m_cursorPosition += charSize;
		m_selection->end = m_cursorPosition;

		m_cursorBlinkTimer = 0.0F;
		UpdateHorizontalScroll();
	}

	// ============================================================================
	// Phase 2: Clipboard Operations
	// ============================================================================

	void TextInput::Copy() {
		std::string selectedText = GetSelectedText();
		if (selectedText.empty()) {
			return;
		}

		// Set clipboard using GLFW
		GLFWwindow* window = glfwGetCurrentContext();
		if (window != nullptr) {
			glfwSetClipboardString(window, selectedText.c_str());
		}
	}

	void TextInput::Cut() {
		std::string selectedText = GetSelectedText();
		if (selectedText.empty()) {
			return;
		}

		// Copy to clipboard
		GLFWwindow* window = glfwGetCurrentContext();
		if (window != nullptr) {
			glfwSetClipboardString(window, selectedText.c_str());
		}

		// Delete selection
		DeleteSelection();
	}

	void TextInput::Paste() {
		// Get clipboard text using GLFW
		GLFWwindow* window = glfwGetCurrentContext();
		if (window == nullptr) {
			return;
		}

		const char* clipboardText = glfwGetClipboardString(window);
		if (clipboardText == nullptr || clipboardText[0] == '\0') {
			return;
		}

		// Delete selection if active
		if (m_selection.has_value() && !m_selection->IsEmpty()) {
			DeleteSelection();
		}

		// Insert clipboard text at cursor
		// Convert to UTF-8 codepoints and insert each character
		std::string			  pasteText(clipboardText);
		std::vector<char32_t> codepoints = foundation::UTF8::Decode(pasteText);

		for (char32_t codepoint : codepoints) {
			// Filter newlines and control characters
			if (codepoint == '\n' || codepoint == '\r' || (codepoint < 32 && codepoint != '\t')) {
				continue;
			}

			// Validate character if validator provided
			if (m_charValidator && !m_charValidator(codepoint)) {
				continue;
			}

			// Insert character
			std::string utf8 = foundation::UTF8::Encode(codepoint);
			m_text.insert(m_cursorPosition, utf8);
			m_cursorPosition += utf8.size();
		}

		// Update and notify
		m_cursorBlinkTimer = 0.0F;
		UpdateHorizontalScroll();
		if (m_onChange) {
			m_onChange(m_text);
		}
	}

	void TextInput::SelectAll() {
		if (m_text.empty()) {
			return;
		}

		SetSelection(0, m_text.size());
		m_cursorPosition = m_text.size(); // Cursor at end
		UpdateHorizontalScroll();
	}

	// ============================================================================
	// Rendering Helpers
	// ============================================================================

	void TextInput::RenderBackground() const {
		Foundation::Color borderColor = m_focused ? m_style.focusedBorderColor : m_style.borderColor;

		Foundation::RectStyle rectStyle{
			.fill = m_style.backgroundColor,
			.border = Foundation::BorderStyle{.color = borderColor, .width = m_style.borderWidth, .cornerRadius = m_style.cornerRadius}
		};

		Renderer::Primitives::DrawRect(
			{.bounds = {m_position.x, m_position.y, m_size.x, m_size.y}, .style = rectStyle, .id = id, .zIndex = static_cast<int>(zIndex)}
		);
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
			.style =
				{.color = m_style.textColor,
				 .fontSize = m_style.fontSize,
				 .hAlign = Foundation::HorizontalAlign::Left,
				 .vAlign = Foundation::VerticalAlign::Top},
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

		// Don't render cursor when selection is active
		if (m_selection.has_value() && !m_selection->IsEmpty()) {
			return;
		}

		// Blink: visible for first half of cycle
		bool visible = m_cursorBlinkTimer < (m_style.cursorBlinkRate * 0.5F);
		if (!visible) {
			return;
		}

		float cursorX = GetCursorXPosition();
		float textAreaY = m_position.y + m_style.paddingTop;
		float textHeight = m_style.fontSize;

		Renderer::Primitives::DrawLine(
			{.start = {cursorX, textAreaY},
			 .end = {cursorX, textAreaY + textHeight},
			 .style = Foundation::LineStyle{.color = m_style.cursorColor, .width = m_style.cursorWidth},
			 .id = id,
			 .zIndex = static_cast<int>(zIndex + 0.2F)}
		);
	}

	void TextInput::RenderSelection() const {
		if (!m_selection.has_value() || m_selection->IsEmpty()) {
			return;
		}

		size_t start = m_selection->GetMin();
		size_t end = m_selection->GetMax();

		// Get FontRenderer for text measurement
		ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
		if (fontRenderer == nullptr) {
			return;
		}

		// Measure text before selection start
		std::string textBeforeStart = m_text.substr(0, start);
		float		startX = fontRenderer->MeasureText(textBeforeStart, m_style.fontSize).x;

		// Measure text up to selection end
		std::string textBeforeEnd = m_text.substr(0, end);
		float		endX = fontRenderer->MeasureText(textBeforeEnd, m_style.fontSize).x;

		// Calculate selection rectangle
		float selectionX = m_position.x + m_style.paddingLeft + startX - m_horizontalScroll;
		float selectionWidth = endX - startX;
		float selectionY = m_position.y + m_style.paddingTop;
		float selectionHeight = m_style.fontSize;

		// Draw selection background
		Foundation::RectStyle selectionStyle{.fill = m_style.selectionColor, .border = std::nullopt};

		Renderer::Primitives::DrawRect({
			.bounds = {selectionX, selectionY, selectionWidth, selectionHeight},
			.style = selectionStyle,
			.id = id,
			.zIndex = static_cast<int>(zIndex + 0.05F) // Below text but above background
		});
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
			.style =
				{.color = m_style.placeholderColor,
				 .fontSize = m_style.fontSize,
				 .hAlign = Foundation::HorizontalAlign::Left,
				 .vAlign = Foundation::VerticalAlign::Top},
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
		float			  width = fontRenderer ? fontRenderer->MeasureText(textBeforeCursor, m_style.fontSize).x : 0.0F;

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
		for (size_t i = 0; i <= m_text.size();) {
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
		float			  textWidth = fontRenderer ? fontRenderer->MeasureText(m_text, m_style.fontSize).x : 0.0F;
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
