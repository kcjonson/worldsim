#include "components/text_input/text_input.h"
#include "core/render_context.h"
#include "focus/focus_manager.h"
#include "font/font_renderer.h"
#include "shapes/shapes.h"
#include "utils/utf8.h"
#include <algorithm>
#include <clipboard/clipboard_manager.h>
#include <input/input_manager.h>
#include <primitives/batch_renderer.h>
#include <primitives/primitives.h>

namespace {
	// Base font size for scale calculations (matches SDF atlas generation)
	constexpr float kBaseFontSize = 16.0F;
} // namespace

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
		  id(args.id),
		  m_enabled(args.enabled),
		  m_tabIndex(args.tabIndex) {

		// Set cursor to end of text
		m_cursorPosition = m_text.size();

		// Register with global FocusManager singleton
		FocusManager::Get().RegisterFocusable(this, m_tabIndex);
	}

	TextInput::~TextInput() {
		// Unregister from FocusManager
		FocusManager::Get().UnregisterFocusable(this);
	}

	TextInput::TextInput(TextInput&& other) noexcept
		: m_position(other.m_position),
		  m_size(other.m_size),
		  m_text(std::move(other.m_text)),
		  m_placeholder(std::move(other.m_placeholder)),
		  m_style(other.m_style),
		  m_onChange(std::move(other.m_onChange)),
		  m_cursorPosition(other.m_cursorPosition),
		  m_selection(other.m_selection),
		  m_cursorBlinkTimer(other.m_cursorBlinkTimer),
		  m_horizontalScroll(other.m_horizontalScroll),
		  visible(other.visible),
		  id(other.id),
		  m_enabled(other.m_enabled),
		  m_focused(other.m_focused),
		  m_tabIndex(other.m_tabIndex),
		  m_mouseDown(other.m_mouseDown) {
		// Unregister other from its old address, register this at new address
		FocusManager::Get().UnregisterFocusable(&other);
		FocusManager::Get().RegisterFocusable(this, m_tabIndex);
		other.m_tabIndex = -2; // Mark as moved-from to prevent double-unregister
	}

	TextInput& TextInput::operator=(TextInput&& other) noexcept {
		if (this != &other) {
			// Unregister this from FocusManager
			FocusManager::Get().UnregisterFocusable(this);

			// Move data
			m_position = other.m_position;
			m_size = other.m_size;
			m_text = std::move(other.m_text);
			m_placeholder = std::move(other.m_placeholder);
			m_style = other.m_style;
			m_onChange = std::move(other.m_onChange);
			m_cursorPosition = other.m_cursorPosition;
			m_selection = other.m_selection;
			m_cursorBlinkTimer = other.m_cursorBlinkTimer;
			m_horizontalScroll = other.m_horizontalScroll;
			visible = other.visible;
			id = other.id;
			m_enabled = other.m_enabled;
			m_focused = other.m_focused;
			m_mouseDown = other.m_mouseDown;
			m_tabIndex = other.m_tabIndex;

			// Unregister other from its old address, register this at new address
			FocusManager::Get().UnregisterFocusable(&other);
			FocusManager::Get().RegisterFocusable(this, m_tabIndex);
			other.m_tabIndex = -2; // Mark as moved-from
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
		// Use IsMouseButtonDown (not IsMouseButtonPressed) like Button does
		if (input.IsMouseButtonDown(engine::MouseButton::Left)) {
			bool wasMouseDown = m_mouseDown;
			if (!wasMouseDown) {
				// Mouse just pressed - process initial click
				Foundation::Vec2 mousePos = input.GetMousePosition();

				// Check if click is inside text input
				if (ContainsPoint(mousePos)) {
					// Grab focus
					FocusManager::Get().SetFocus(this);

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
			if (m_focused && m_mouseDown && input.IsMouseButtonDown(engine::MouseButton::Left)) {
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
		}

		// Release mouse (outside the IsMouseButtonDown block)
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

	void TextInput::Render() {
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
			RenderSelection();
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
		m_mouseDown = false; // Clear mouse state to prevent ghost selections
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

		// Filter control characters
		if (codepoint < 32 || codepoint == 127) {
			return;
		}

		// Block emoji ranges (font atlas doesn't support them)
		// Main emoji blocks: U+1F300-U+1F9FF, U+2600-U+27BF (misc symbols/dingbats)
		if ((codepoint >= 0x1F300 && codepoint <= 0x1FAFF) || // Emoji and pictographs
			(codepoint >= 0x2600 && codepoint <= 0x27BF) ||	  // Misc symbols, dingbats
			(codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||	  // Variation selectors
			(codepoint >= 0x1F000 && codepoint <= 0x1F02F)) { // Mahjong, dominos
			return;
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

		engine::ClipboardManager::Get().SetText(selectedText);
	}

	void TextInput::Cut() {
		std::string selectedText = GetSelectedText();
		if (selectedText.empty()) {
			return;
		}

		// Copy to clipboard
		engine::ClipboardManager::Get().SetText(selectedText);

		// Delete selection
		DeleteSelection();
	}

	void TextInput::Paste() {
		// Get clipboard text
		std::string pasteText = engine::ClipboardManager::Get().GetText();
		if (pasteText.empty()) {
			return;
		}

		// Delete selection if active
		if (m_selection.has_value() && !m_selection->IsEmpty()) {
			DeleteSelection();
		}

		// Insert clipboard text at cursor
		// Convert to UTF-8 codepoints and insert each character
		std::vector<char32_t> codepoints = foundation::UTF8::Decode(pasteText);

		for (char32_t codepoint : codepoints) {
			// Filter newlines and control characters
			if (codepoint == '\n' || codepoint == '\r' || (codepoint < 32 && codepoint != '\t')) {
				continue;
			}

			// Block emoji ranges (font atlas doesn't support them)
			if ((codepoint >= 0x1F300 && codepoint <= 0x1FAFF) || // Emoji and pictographs
				(codepoint >= 0x2600 && codepoint <= 0x27BF) ||	  // Misc symbols, dingbats
				(codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||	  // Variation selectors
				(codepoint >= 0x1F000 && codepoint <= 0x1F02F)) { // Mahjong, dominos
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

		short zIndex = RenderContext::GetZIndex();
		Renderer::Primitives::DrawRect(
			{.bounds = {m_position.x, m_position.y, m_size.x, m_size.y}, .style = rectStyle, .id = id, .zIndex = zIndex}
		);
	}

	void TextInput::RenderText() const {
		if (m_text.empty()) {
			return;
		}

		// Get unified batch renderer
		Renderer::BatchRenderer* batchRenderer = Renderer::Primitives::GetBatchRenderer();
		if (batchRenderer == nullptr) {
			return;
		}

		// Get font renderer for metrics and glyph generation
		ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
		if (fontRenderer == nullptr) {
			return;
		}

		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = m_style.fontSize / kBaseFontSize;

		// Calculate horizontal position with scroll
		float textX = m_position.x + m_style.paddingLeft - m_horizontalScroll;

		// Calculate baseline Y position for vertically centered text
		// This matches Text component's bounding box Middle alignment behavior
		float ascent = fontRenderer->GetAscent(scale);
		float baselineY = m_position.y + (m_size.y - ascent) * 0.5F;

		// Generate glyph quads using FontRenderer
		glm::vec4 glyphColor(m_style.textColor.r, m_style.textColor.g, m_style.textColor.b, m_style.textColor.a);
		std::vector<ui::FontRenderer::GlyphQuad> glyphs;
		fontRenderer->GenerateGlyphQuads(m_text, glm::vec2(textX, baselineY), scale, glyphColor, glyphs);

		// Add each glyph to the unified batch renderer
		Foundation::Color textColor(m_style.textColor.r, m_style.textColor.g, m_style.textColor.b, m_style.textColor.a);
		for (const auto& glyph : glyphs) {
			batchRenderer->AddTextQuad(
				Foundation::Vec2(glyph.position.x, glyph.position.y),
				Foundation::Vec2(glyph.size.x, glyph.size.y),
				Foundation::Vec2(glyph.uvMin.x, glyph.uvMin.y),
				Foundation::Vec2(glyph.uvMax.x, glyph.uvMax.y),
				textColor
			);
		}
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
		float centerY = m_position.y + (m_size.y * 0.5F); // Vertical center of input box
		float textHeight = m_style.fontSize;

		// Position cursor vertically centered with the text
		float cursorStartY = centerY - (textHeight * 0.5F);
		float cursorEndY = centerY + (textHeight * 0.5F);

		short zIndex = RenderContext::GetZIndex();
		Renderer::Primitives::DrawLine(
			{.start = {cursorX, cursorStartY},
			 .end = {cursorX, cursorEndY},
			 .style = Foundation::LineStyle{.color = m_style.cursorColor, .width = m_style.cursorWidth},
			 .id = id,
			 .zIndex = zIndex + 3} // On top of everything
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

		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = m_style.fontSize / kBaseFontSize;

		// Measure text before selection start
		std::string textBeforeStart = m_text.substr(0, start);
		float		startX = fontRenderer->MeasureText(textBeforeStart, scale).x;

		// Measure text up to selection end
		std::string textBeforeEnd = m_text.substr(0, end);
		float		endX = fontRenderer->MeasureText(textBeforeEnd, scale).x;

		// Calculate selection rectangle (vertically centered in the box)
		float selectionX = m_position.x + m_style.paddingLeft + startX - m_horizontalScroll;
		float selectionWidth = endX - startX;
		float centerY = m_position.y + (m_size.y * 0.5F);
		float selectionHeight = m_style.fontSize;
		float selectionY = centerY - (selectionHeight * 0.5F);

		// Draw selection background
		Foundation::RectStyle selectionStyle{.fill = m_style.selectionColor, .border = std::nullopt};

		short zIndex = RenderContext::GetZIndex();
		Renderer::Primitives::DrawRect({
			.bounds = {selectionX, selectionY, selectionWidth, selectionHeight},
			.style = selectionStyle,
			.id = id,
			.zIndex = zIndex + 1 // Above background, below text (+2)
		});
	}

	void TextInput::RenderPlaceholder() const {
		if (m_placeholder.empty()) {
			return;
		}

		// Get unified batch renderer
		Renderer::BatchRenderer* batchRenderer = Renderer::Primitives::GetBatchRenderer();
		if (batchRenderer == nullptr) {
			return;
		}

		// Get font renderer for metrics and glyph generation
		ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
		if (fontRenderer == nullptr) {
			return;
		}

		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = m_style.fontSize / kBaseFontSize;

		// Calculate horizontal position (no scroll for placeholder)
		float textX = m_position.x + m_style.paddingLeft;

		// Calculate baseline Y position for vertically centered text
		// This matches Text component's bounding box Middle alignment behavior
		float ascent = fontRenderer->GetAscent(scale);
		float baselineY = m_position.y + (m_size.y - ascent) * 0.5F;

		// Generate glyph quads using FontRenderer
		glm::vec4 glyphColor(m_style.placeholderColor.r, m_style.placeholderColor.g, m_style.placeholderColor.b, m_style.placeholderColor.a);
		std::vector<ui::FontRenderer::GlyphQuad> glyphs;
		fontRenderer->GenerateGlyphQuads(m_placeholder, glm::vec2(textX, baselineY), scale, glyphColor, glyphs);

		// Add each glyph to the unified batch renderer
		Foundation::Color placeholderColor(m_style.placeholderColor.r, m_style.placeholderColor.g, m_style.placeholderColor.b, m_style.placeholderColor.a);
		for (const auto& glyph : glyphs) {
			batchRenderer->AddTextQuad(
				Foundation::Vec2(glyph.position.x, glyph.position.y),
				Foundation::Vec2(glyph.size.x, glyph.size.y),
				Foundation::Vec2(glyph.uvMin.x, glyph.uvMin.y),
				Foundation::Vec2(glyph.uvMax.x, glyph.uvMax.y),
				placeholderColor
			);
		}
	}

	// ============================================================================
	// Utilities
	// ============================================================================

	float TextInput::GetCursorXPosition() const {
		// Get text up to cursor position
		std::string textBeforeCursor = m_text.substr(0, m_cursorPosition);

		// Measure width using FontRenderer
		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = m_style.fontSize / kBaseFontSize;

		ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
		float			  width = fontRenderer ? fontRenderer->MeasureText(textBeforeCursor, scale).x : 0.0F;

		// Account for padding and scroll
		return m_position.x + m_style.paddingLeft + width - m_horizontalScroll;
	}

	size_t TextInput::GetCursorPositionFromMouse(float localX) const {
		// localX is already relative to text area (with scroll applied)

		// Binary search for closest character boundary
		size_t bestPosition = 0;
		float  bestDistance = std::abs(localX);

		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = m_style.fontSize / kBaseFontSize;

		// Check each character boundary
		ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
		for (size_t i = 0; i <= m_text.size();) {
			std::string textUpToPos = m_text.substr(0, i);
			float		width = fontRenderer ? fontRenderer->MeasureText(textUpToPos, scale).x : 0.0F;
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
		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = m_style.fontSize / kBaseFontSize;

		ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
		float			  textWidth = fontRenderer ? fontRenderer->MeasureText(m_text, scale).x : 0.0F;
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
