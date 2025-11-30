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
		: position(args.position),
		  size(args.size),
		  text(args.text),
		  placeholder(args.placeholder),
		  style(args.style),
		  onChange(args.onChange),
		  id(args.id),
		  enabled(args.enabled),
		  tabIndex(args.tabIndex) {

		// Set cursor to end of text
		cursorPosition = text.size();

		// Register with global FocusManager singleton
		FocusManager::Get().registerFocusable(this, tabIndex);
	}

	TextInput::~TextInput() {
		// Unregister from FocusManager
		FocusManager::Get().unregisterFocusable(this);
	}

	TextInput::TextInput(TextInput&& other) noexcept
		: position(other.position),
		  size(other.size),
		  text(std::move(other.text)),
		  placeholder(std::move(other.placeholder)),
		  style(other.style),
		  onChange(std::move(other.onChange)),
		  cursorPosition(other.cursorPosition),
		  selection(other.selection),
		  cursorBlinkTimer(other.cursorBlinkTimer),
		  horizontalScroll(other.horizontalScroll),
		  visible(other.visible),
		  id(other.id),
		  enabled(other.enabled),
		  focused(other.focused),
		  tabIndex(other.tabIndex),
		  mouseDown(other.mouseDown) {
		// Unregister other from its old address, register this at new address
		FocusManager::Get().unregisterFocusable(&other);
		FocusManager::Get().registerFocusable(this, tabIndex);
		other.tabIndex = -2; // Mark as moved-from to prevent double-unregister
	}

	TextInput& TextInput::operator=(TextInput&& other) noexcept {
		if (this != &other) {
			// Unregister this from FocusManager
			FocusManager::Get().unregisterFocusable(this);

			// Move data
			position = other.position;
			size = other.size;
			text = std::move(other.text);
			placeholder = std::move(other.placeholder);
			style = other.style;
			onChange = std::move(other.onChange);
			cursorPosition = other.cursorPosition;
			selection = other.selection;
			cursorBlinkTimer = other.cursorBlinkTimer;
			horizontalScroll = other.horizontalScroll;
			visible = other.visible;
			id = other.id;
			enabled = other.enabled;
			focused = other.focused;
			mouseDown = other.mouseDown;
			tabIndex = other.tabIndex;

			// Unregister other from its old address, register this at new address
			FocusManager::Get().unregisterFocusable(&other);
			FocusManager::Get().registerFocusable(this, tabIndex);
			other.tabIndex = -2; // Mark as moved-from
		}
		return *this;
	}

	// ============================================================================
	// Lifecycle Methods
	// ============================================================================

	void TextInput::handleInput() {
		if (!enabled) {
			return;
		}

		auto& input = engine::InputManager::Get();

		// Check for mouse click
		// Use IsMouseButtonDown (not IsMouseButtonPressed) like Button does
		if (input.isMouseButtonDown(engine::MouseButton::Left)) {
			bool wasMouseDown = mouseDown;
			if (!wasMouseDown) {
				// Mouse just pressed - process initial click
				Foundation::Vec2 mousePos = input.getMousePosition();

				// Check if click is inside text input
				if (containsPoint(mousePos)) {
					// Grab focus
					FocusManager::Get().setFocus(this);

					// Position cursor from mouse X
					float localX = mousePos.x - position.x - style.paddingLeft + horizontalScroll;
					cursorPosition = getCursorPositionFromMouse(localX);
					cursorBlinkTimer = 0.0F;

					// Clear selection on click (will be recreated if dragging)
					clearSelection();

					// Track mouse down for drag selection
					mouseDown = true;
				}
			}

			// Mouse drag selection
			if (focused && mouseDown && input.isMouseButtonDown(engine::MouseButton::Left)) {
				Foundation::Vec2 mousePos = input.getMousePosition();
				float			 localX = mousePos.x - position.x - style.paddingLeft + horizontalScroll;
				size_t			 dragPosition = getCursorPositionFromMouse(localX);

				// Create/update selection from original cursor position to drag position
				if (dragPosition != cursorPosition) {
					// Selection anchor is where mouse was first pressed (already set as cursorPosition)
					// Selection head is current drag position
					if (!selection.has_value()) {
						// Start new selection
						setSelection(cursorPosition, dragPosition);
					} else {
						// Update existing selection
						selection->end = dragPosition;
					}
					cursorPosition = dragPosition; // Move cursor to drag position
					updateHorizontalScroll();
				}
			}
		}

		// Release mouse (outside the IsMouseButtonDown block)
		if (mouseDown && !input.isMouseButtonDown(engine::MouseButton::Left)) {
			mouseDown = false;
		}
	}

	void TextInput::update(float deltaTime) {
		if (focused) {
			cursorBlinkTimer += deltaTime;
			if (cursorBlinkTimer > style.cursorBlinkRate) {
				cursorBlinkTimer -= style.cursorBlinkRate;
			}
		}
	}

	void TextInput::render() {
		if (!visible) {
			return;
		}

		renderBackground();

		// Scissor test to clip overflowing text
		float textAreaX = position.x + style.paddingLeft;
		float textAreaY = position.y + style.paddingTop;
		float textAreaWidth = size.x - style.paddingLeft - style.paddingRight;
		float textAreaHeight = size.y - style.paddingTop - style.paddingBottom;

		Renderer::Primitives::PushScissor(Foundation::Rect{textAreaX, textAreaY, textAreaWidth, textAreaHeight});

		if (text.empty() && !focused) {
			renderPlaceholder();
		} else {
			renderSelection();
			renderText();
			renderCursor();
		}

		Renderer::Primitives::PopScissor();
	}

	// ============================================================================
	// State Management
	// ============================================================================

	void TextInput::setText(const std::string& newText) {
		text = newText;
		cursorPosition = std::min(cursorPosition, text.size());
		updateHorizontalScroll();
		if (onChange) {
			onChange(text);
		}
	}

	// ============================================================================
	// IFocusable Interface
	// ============================================================================

	void TextInput::onFocusGained() {
		focused = true;
		cursorBlinkTimer = 0.0F; // Reset blink (cursor visible)
	}

	void TextInput::onFocusLost() {
		focused = false;
		mouseDown = false; // Clear mouse state to prevent ghost selections
		clearSelection();
	}

	void TextInput::handleKeyInput(engine::Key key, bool shift, bool ctrl, bool /*alt*/) {
		if (!enabled || !focused) {
			return;
		}

		// Clipboard operations (Ctrl+C, Ctrl+X, Ctrl+V, Ctrl+A)
		if (ctrl) {
			switch (key) {
				case engine::Key::C:
					copy();
					return;
				case engine::Key::X:
					cut();
					return;
				case engine::Key::V:
					paste();
					return;
				case engine::Key::A:
					selectAll();
					return;
				default:
					break;
			}
		}

		// Navigation and editing
		switch (key) {
			case engine::Key::Left:
				if (shift) {
					extendSelectionLeft();
				} else {
					clearSelection();
					moveCursorLeft();
				}
				break;
			case engine::Key::Right:
				if (shift) {
					extendSelectionRight();
				} else {
					clearSelection();
					moveCursorRight();
				}
				break;
			case engine::Key::Home:
				clearSelection();
				moveCursorHome();
				break;
			case engine::Key::End:
				clearSelection();
				moveCursorEnd();
				break;
			case engine::Key::Delete:
				deleteCharAtCursor();
				break;
			case engine::Key::Backspace:
				deleteCharBeforeCursor();
				break;
			default:
				break;
		}
	}

	void TextInput::handleCharInput(char32_t codepoint) {
		if (!enabled || !focused) {
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

		insertChar(codepoint);
	}

	bool TextInput::canReceiveFocus() const {
		return enabled;
	}

	// ============================================================================
	// Phase 1: Core Editing Operations
	// ============================================================================

	void TextInput::insertChar(char32_t codepoint) {
		// Delete selection first if active
		if (selection.has_value() && !selection->isEmpty()) {
			deleteSelection();
		}

		// Convert codepoint to UTF-8
		std::string utf8 = foundation::UTF8::encode(codepoint);

		// Insert at cursor position
		text.insert(cursorPosition, utf8);
		cursorPosition += utf8.size();

		// Reset cursor blink
		cursorBlinkTimer = 0.0F;

		// Update scroll and notify
		updateHorizontalScroll();
		if (onChange) {
			onChange(text);
		}
	}

	void TextInput::deleteCharAtCursor() {
		// Delete selection if active
		if (selection.has_value() && !selection->isEmpty()) {
			deleteSelection();
			return;
		}

		if (cursorPosition >= text.size()) {
			return; // At end
		}

		// Find next UTF-8 character boundary
		size_t charSize = foundation::UTF8::characterSize(static_cast<unsigned char>(text[cursorPosition]));
		text.erase(cursorPosition, charSize);

		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
		if (onChange) {
			onChange(text);
		}
	}

	void TextInput::deleteCharBeforeCursor() {
		// Delete selection if active
		if (selection.has_value() && !selection->isEmpty()) {
			deleteSelection();
			return;
		}

		if (cursorPosition == 0) {
			return; // At start
		}

		// Find previous UTF-8 character boundary
		size_t charSize = foundation::UTF8::previousCharacterSize(text, cursorPosition);
		text.erase(cursorPosition - charSize, charSize);
		cursorPosition -= charSize;

		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
		if (onChange) {
			onChange(text);
		}
	}

	void TextInput::moveCursorLeft() {
		if (cursorPosition == 0) {
			return;
		}

		size_t charSize = foundation::UTF8::previousCharacterSize(text, cursorPosition);
		cursorPosition -= charSize;
		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
	}

	void TextInput::moveCursorRight() {
		if (cursorPosition >= text.size()) {
			return;
		}

		size_t charSize = foundation::UTF8::characterSize(static_cast<unsigned char>(text[cursorPosition]));
		cursorPosition += charSize;
		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
	}

	void TextInput::moveCursorHome() {
		cursorPosition = 0;
		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
	}

	void TextInput::moveCursorEnd() {
		cursorPosition = text.size();
		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
	}

	// ============================================================================
	// Phase 2: Selection Operations
	// ============================================================================

	void TextInput::setSelection(size_t start, size_t end) {
		selection = TextSelection{start, end};
		cursorBlinkTimer = 0.0F;
	}

	void TextInput::clearSelection() {
		selection.reset();
	}

	std::string TextInput::getSelectedText() const {
		if (!selection.has_value() || selection->isEmpty()) {
			return "";
		}

		size_t start = selection->getMin();
		size_t end = selection->getMax();
		return text.substr(start, end - start);
	}

	void TextInput::deleteSelection() {
		if (!selection.has_value() || selection->isEmpty()) {
			return;
		}

		size_t start = selection->getMin();
		size_t end = selection->getMax();

		// Delete the selected range
		text.erase(start, end - start);

		// Move cursor to start of deleted range
		cursorPosition = start;

		// Clear selection
		clearSelection();

		// Update and notify
		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
		if (onChange) {
			onChange(text);
		}
	}

	void TextInput::extendSelectionLeft() {
		if (cursorPosition == 0) {
			return;
		}

		// If no selection exists, create one starting at current cursor
		if (!selection.has_value()) {
			selection = TextSelection{cursorPosition, cursorPosition};
		}

		// Move cursor left (selection end)
		size_t charSize = foundation::UTF8::previousCharacterSize(text, cursorPosition);
		cursorPosition -= charSize;
		selection->end = cursorPosition;

		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
	}

	void TextInput::extendSelectionRight() {
		if (cursorPosition >= text.size()) {
			return;
		}

		// If no selection exists, create one starting at current cursor
		if (!selection.has_value()) {
			selection = TextSelection{cursorPosition, cursorPosition};
		}

		// Move cursor right (selection end)
		size_t charSize = foundation::UTF8::characterSize(static_cast<unsigned char>(text[cursorPosition]));
		cursorPosition += charSize;
		selection->end = cursorPosition;

		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
	}

	// ============================================================================
	// Phase 2: Clipboard Operations
	// ============================================================================

	void TextInput::copy() {
		std::string selectedText = getSelectedText();
		if (selectedText.empty()) {
			return;
		}

		engine::ClipboardManager::Get().setText(selectedText);
	}

	void TextInput::cut() {
		std::string selectedText = getSelectedText();
		if (selectedText.empty()) {
			return;
		}

		// Copy to clipboard
		engine::ClipboardManager::Get().setText(selectedText);

		// Delete selection
		deleteSelection();
	}

	void TextInput::paste() {
		// Get clipboard text
		std::string pasteText = engine::ClipboardManager::Get().getText();
		if (pasteText.empty()) {
			return;
		}

		// Delete selection if active
		if (selection.has_value() && !selection->isEmpty()) {
			deleteSelection();
		}

		// Insert clipboard text at cursor
		// Convert to UTF-8 codepoints and insert each character
		std::vector<char32_t> codepoints = foundation::UTF8::decode(pasteText);

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
			std::string utf8 = foundation::UTF8::encode(codepoint);
			text.insert(cursorPosition, utf8);
			cursorPosition += utf8.size();
		}

		// Update and notify
		cursorBlinkTimer = 0.0F;
		updateHorizontalScroll();
		if (onChange) {
			onChange(text);
		}
	}

	void TextInput::selectAll() {
		if (text.empty()) {
			return;
		}

		setSelection(0, text.size());
		cursorPosition = text.size(); // Cursor at end
		updateHorizontalScroll();
	}

	// ============================================================================
	// Rendering Helpers
	// ============================================================================

	void TextInput::renderBackground() const {
		Foundation::Color borderColor = focused ? style.focusedBorderColor : style.borderColor;

		Foundation::RectStyle rectStyle{
			.fill = style.backgroundColor,
			.border = Foundation::BorderStyle{.color = borderColor, .width = style.borderWidth, .cornerRadius = style.cornerRadius}
		};

		short zIndex = RenderContext::getZIndex();
		Renderer::Primitives::drawRect(
			{.bounds = {position.x, position.y, size.x, size.y}, .style = rectStyle, .id = id, .zIndex = zIndex}
		);
	}

	void TextInput::renderText() const {
		if (text.empty()) {
			return;
		}

		// Get unified batch renderer
		Renderer::BatchRenderer* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer == nullptr) {
			return;
		}

		// Get font renderer for metrics and glyph generation
		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		if (fontRenderer == nullptr) {
			return;
		}

		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = style.fontSize / kBaseFontSize;

		// Calculate horizontal position with scroll
		float textX = position.x + style.paddingLeft - horizontalScroll;

		// Calculate baseline Y position for vertically centered text
		// This matches Text component's bounding box Middle alignment behavior
		float ascent = fontRenderer->getAscent(scale);
		float baselineY = position.y + (size.y - ascent) * 0.5F;

		// Generate glyph quads using FontRenderer
		glm::vec4 glyphColor(style.textColor.r, style.textColor.g, style.textColor.b, style.textColor.a);
		std::vector<ui::FontRenderer::GlyphQuad> glyphs;
		fontRenderer->generateGlyphQuads(text, glm::vec2(textX, baselineY), scale, glyphColor, glyphs);

		// Add each glyph to the unified batch renderer
		Foundation::Color textColor(style.textColor.r, style.textColor.g, style.textColor.b, style.textColor.a);
		for (const auto& glyph : glyphs) {
			batchRenderer->addTextQuad(
				Foundation::Vec2(glyph.position.x, glyph.position.y),
				Foundation::Vec2(glyph.size.x, glyph.size.y),
				Foundation::Vec2(glyph.uvMin.x, glyph.uvMin.y),
				Foundation::Vec2(glyph.uvMax.x, glyph.uvMax.y),
				textColor
			);
		}
	}

	void TextInput::renderCursor() const {
		if (!focused) {
			return;
		}

		// Don't render cursor when selection is active
		if (selection.has_value() && !selection->isEmpty()) {
			return;
		}

		// Blink: visible for first half of cycle
		bool visible = cursorBlinkTimer < (style.cursorBlinkRate * 0.5F);
		if (!visible) {
			return;
		}

		float cursorX = getCursorXPosition();
		float centerY = position.y + (size.y * 0.5F); // Vertical center of input box
		float textHeight = style.fontSize;

		// Position cursor vertically centered with the text
		float cursorStartY = centerY - (textHeight * 0.5F);
		float cursorEndY = centerY + (textHeight * 0.5F);

		short zIndex = RenderContext::getZIndex();
		Renderer::Primitives::drawLine(
			{.start = {cursorX, cursorStartY},
			 .end = {cursorX, cursorEndY},
			 .style = Foundation::LineStyle{.color = style.cursorColor, .width = style.cursorWidth},
			 .id = id,
			 .zIndex = zIndex + 3} // On top of everything
		);
	}

	void TextInput::renderSelection() const {
		if (!selection.has_value() || selection->isEmpty()) {
			return;
		}

		size_t start = selection->getMin();
		size_t end = selection->getMax();

		// Get FontRenderer for text measurement
		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		if (fontRenderer == nullptr) {
			return;
		}

		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = style.fontSize / kBaseFontSize;

		// Measure text before selection start
		std::string textBeforeStart = text.substr(0, start);
		float		startX = fontRenderer->MeasureText(textBeforeStart, scale).x;

		// Measure text up to selection end
		std::string textBeforeEnd = text.substr(0, end);
		float		endX = fontRenderer->MeasureText(textBeforeEnd, scale).x;

		// Calculate selection rectangle (vertically centered in the box)
		float selectionX = position.x + style.paddingLeft + startX - horizontalScroll;
		float selectionWidth = endX - startX;
		float centerY = position.y + (size.y * 0.5F);
		float selectionHeight = style.fontSize;
		float selectionY = centerY - (selectionHeight * 0.5F);

		// Draw selection background
		Foundation::RectStyle selectionStyle{.fill = style.selectionColor, .border = std::nullopt};

		short zIndex = RenderContext::getZIndex();
		Renderer::Primitives::drawRect({
			.bounds = {selectionX, selectionY, selectionWidth, selectionHeight},
			.style = selectionStyle,
			.id = id,
			.zIndex = zIndex + 1 // Above background, below text (+2)
		});
	}

	void TextInput::renderPlaceholder() const {
		if (placeholder.empty()) {
			return;
		}

		// Get unified batch renderer
		Renderer::BatchRenderer* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer == nullptr) {
			return;
		}

		// Get font renderer for metrics and glyph generation
		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		if (fontRenderer == nullptr) {
			return;
		}

		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = style.fontSize / kBaseFontSize;

		// Calculate horizontal position (no scroll for placeholder)
		float textX = position.x + style.paddingLeft;

		// Calculate baseline Y position for vertically centered text
		// This matches Text component's bounding box Middle alignment behavior
		float ascent = fontRenderer->getAscent(scale);
		float baselineY = position.y + (size.y - ascent) * 0.5F;

		// Generate glyph quads using FontRenderer
		glm::vec4 glyphColor(style.placeholderColor.r, style.placeholderColor.g, style.placeholderColor.b, style.placeholderColor.a);
		std::vector<ui::FontRenderer::GlyphQuad> glyphs;
		fontRenderer->generateGlyphQuads(placeholder, glm::vec2(textX, baselineY), scale, glyphColor, glyphs);

		// Add each glyph to the unified batch renderer
		Foundation::Color placeholderColor(style.placeholderColor.r, style.placeholderColor.g, style.placeholderColor.b, style.placeholderColor.a);
		for (const auto& glyph : glyphs) {
			batchRenderer->addTextQuad(
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

	float TextInput::getCursorXPosition() const {
		// Get text up to cursor position
		std::string textBeforeCursor = text.substr(0, cursorPosition);

		// Measure width using FontRenderer
		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = style.fontSize / kBaseFontSize;

		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		float			  width = fontRenderer ? fontRenderer->MeasureText(textBeforeCursor, scale).x : 0.0F;

		// Account for padding and scroll
		return position.x + style.paddingLeft + width - horizontalScroll;
	}

	size_t TextInput::getCursorPositionFromMouse(float localX) const {
		// localX is already relative to text area (with scroll applied)

		// Binary search for closest character boundary
		size_t bestPosition = 0;
		float  bestDistance = std::abs(localX);

		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = style.fontSize / kBaseFontSize;

		// Check each character boundary
		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		for (size_t i = 0; i <= text.size();) {
			std::string textUpToPos = text.substr(0, i);
			float		width = fontRenderer ? fontRenderer->MeasureText(textUpToPos, scale).x : 0.0F;
			float		distance = std::abs(width - localX);

			if (distance < bestDistance) {
				bestDistance = distance;
				bestPosition = i;
			}

			// Move to next character boundary
			if (i >= text.size()) {
				break;
			}
			size_t charSize = foundation::UTF8::characterSize(static_cast<unsigned char>(text[i]));
			i += charSize;
		}

		return bestPosition;
	}

	void TextInput::updateHorizontalScroll() {
		float cursorX = getCursorXPosition() - position.x - style.paddingLeft + horizontalScroll;
		float visibleWidth = size.x - style.paddingLeft - style.paddingRight;

		// Scroll right if cursor is past right edge
		if (cursorX > visibleWidth) {
			horizontalScroll += (cursorX - visibleWidth);
		}

		// Scroll left if cursor is past left edge
		if (cursorX < 0) {
			horizontalScroll += cursorX; // cursorX is negative
		}

		// Clamp to ensure text fills from left when possible
		// Convert fontSize to scale (kBaseFontSize = 1.0 scale)
		float scale = style.fontSize / kBaseFontSize;

		ui::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		float			  textWidth = fontRenderer ? fontRenderer->MeasureText(text, scale).x : 0.0F;
		if (textWidth < visibleWidth) {
			horizontalScroll = 0.0F; // No scroll needed
		} else {
			horizontalScroll = std::max(0.0F, std::min(horizontalScroll, textWidth - visibleWidth));
		}
	}

	// ============================================================================
	// Geometry Queries
	// ============================================================================

	bool TextInput::containsPoint(const Foundation::Vec2& point) const {
		return point.x >= position.x && point.x <= position.x + size.x && point.y >= position.y &&
			   point.y <= position.y + size.y;
	}

} // namespace UI
