#pragma once

#include "focus/focusable.h"
#include "graphics/color.h"
#include "math/types.h"
#include <functional>
#include <optional>
#include <string>

// TextInput Component (Phase 1 + 2)
//
// Single-line text editing widget with cursor, selection, and clipboard support.
// Phase 1: Core editing (cursor, insert/delete, keyboard navigation, mouse click)
// Phase 2: Selection & clipboard (Shift+Arrow, mouse drag, Copy/Cut/Paste)
//
// Lifecycle: HandleInput() → Update(deltaTime) → Render()
// Implements IFocusable for Tab navigation and keyboard input routing

namespace UI {

// Forward declarations
class FocusManager;

// Text selection range (Phase 2)
struct TextSelection {
	size_t start; // Byte offset where selection started (anchor)
	size_t end;	  // Byte offset where cursor is (head)

	size_t GetMin() const { return std::min(start, end); }
	size_t GetMax() const { return std::max(start, end); }
	bool   IsEmpty() const { return start == end; }
};

// TextInput visual style
struct TextInputStyle {
	// Background and border
	Foundation::Color backgroundColor{0.15F, 0.15F, 0.15F, 1.0F};
	Foundation::Color borderColor{0.4F, 0.4F, 0.4F, 1.0F};
	Foundation::Color focusedBorderColor{0.2F, 0.5F, 0.9F, 1.0F};
	float			  borderWidth{1.0F};
	float			  cornerRadius{4.0F};

	// Text
	Foundation::Color textColor{1.0F, 1.0F, 1.0F, 1.0F};
	Foundation::Color placeholderColor{0.5F, 0.5F, 0.5F, 1.0F};
	float			  fontSize{16.0F};

	// Cursor
	Foundation::Color cursorColor{1.0F, 1.0F, 1.0F, 1.0F};
	float			  cursorWidth{1.0F};
	float			  cursorBlinkRate{0.5F}; // Seconds per blink cycle

	// Selection (Phase 2)
	Foundation::Color selectionColor{0.2F, 0.4F, 0.8F, 0.5F};

	// Padding
	float paddingLeft{8.0F};
	float paddingRight{8.0F};
	float paddingTop{6.0F};
	float paddingBottom{6.0F};
};

// TextInput component - implements IFocusable for keyboard focus
struct TextInput : public IFocusable {
	// Constructor arguments struct
	struct Args {
		Foundation::Vec2  position{0.0F, 0.0F};
		Foundation::Vec2  size{200.0F, 32.0F};
		std::string		  text;
		std::string		  placeholder;
		TextInputStyle	  style;
		FocusManager*	  focusManager = nullptr; // Optional: auto-register for keyboard focus
		int				  tabIndex = -1;		  // Tab order (-1 for auto-assign)
		const char*		  id = nullptr;
		float			  zIndex = -1.0F;
		bool			  enabled = true;
		std::function<void(const std::string&)> onChange;		 // Called when text changes
		std::function<bool(char32_t)>			charValidator; // Return false to reject character
	};

	// --- Public Members ---

	// Geometry
	Foundation::Vec2 m_position{0.0F, 0.0F};
	Foundation::Vec2 m_size{200.0F, 32.0F};

	// Text content
	std::string m_text;
	std::string m_placeholder;

	// Style
	TextInputStyle m_style;

	// Callbacks
	std::function<void(const std::string&)> m_onChange;
	std::function<bool(char32_t)>			m_charValidator;

	// State
	size_t						m_cursorPosition{0};	 // Byte offset in UTF-8 string
	std::optional<TextSelection> m_selection;			 // Active selection (Phase 2)
	float						m_cursorBlinkTimer{0.0F}; // For cursor blink animation
	float						m_horizontalScroll{0.0F}; // Scroll offset for overflow text

	// Layer properties
	float		zIndex{-1.0F};
	bool		visible{true};
	const char* id = nullptr;
	bool		m_enabled{true};
	bool		m_focused{false};

	// --- Public Methods ---

	// Constructor & Destructor
	explicit TextInput(const Args& args);
	~TextInput();

	// Disable copy (TextInput registers with FocusManager)
	TextInput(const TextInput&) = delete;
	TextInput& operator=(const TextInput&) = delete;

	// Allow move (must unregister from old address and re-register at new address)
	TextInput(TextInput&& other) noexcept;
	TextInput& operator=(TextInput&& other) noexcept;

	// Standard lifecycle methods
	void HandleInput();			 // Mouse click detection (called before Update in scene)
	void Update(float deltaTime); // Update cursor blink, etc.
	void Render() const;		 // Draw text input

	// State management
	void SetEnabled(bool enabled) { m_enabled = enabled; }
	bool IsEnabled() const { return m_enabled; }
	void SetText(const std::string& text);
	const std::string& GetText() const { return m_text; }

	// IFocusable interface implementation
	void OnFocusGained() override;
	void OnFocusLost() override;
	void HandleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void HandleCharInput(char32_t codepoint) override;
	bool CanReceiveFocus() const override;

	// Geometry queries
	bool ContainsPoint(const Foundation::Vec2& point) const;

  private:
	// Focus management
	FocusManager* m_focusManager{nullptr};

	// Internal state tracking
	bool m_mouseDown{false};

	// --- Phase 1: Core Editing Operations ---

	void InsertChar(char32_t c);
	void DeleteCharAtCursor();		 // Delete key
	void DeleteCharBeforeCursor();	 // Backspace key
	void MoveCursorLeft();
	void MoveCursorRight();
	void MoveCursorHome();
	void MoveCursorEnd();

	// --- Phase 2: Selection Operations ---

	void SetSelection(size_t start, size_t end);
	void ClearSelection();
	std::string GetSelectedText() const;
	void DeleteSelection();
	void ExtendSelectionLeft();	 // Shift+Left
	void ExtendSelectionRight(); // Shift+Right

	// --- Phase 2: Clipboard Operations ---

	void Copy();	  // Ctrl+C
	void Cut();		  // Ctrl+X
	void Paste();	  // Ctrl+V
	void SelectAll(); // Ctrl+A

	// --- Rendering Helpers ---

	void RenderBackground() const;
	void RenderText() const;
	void RenderCursor() const;
	void RenderSelection() const;	 // Phase 2
	void RenderPlaceholder() const;

	// --- Utilities ---

	float  GetCursorXPosition() const;						   // Pixel X for cursor position
	size_t GetCursorPositionFromMouse(float mouseX) const; // Index from pixel X
	void   UpdateHorizontalScroll();						   // Ensure cursor is visible
};

} // namespace UI
