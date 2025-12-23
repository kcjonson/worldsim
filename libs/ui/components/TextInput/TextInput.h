#pragma once

#include "component/Component.h"
#include "focus/Focusable.h"
#include "graphics/Color.h"
#include "math/Types.h"
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
// Implements IFocusable and satisfies Layer/Focusable concepts
//
// See: /docs/technical/ui-framework/architecture.md

namespace UI {

// Forward declarations
class FocusManager;

// Text selection range (Phase 2)
struct TextSelection {
	size_t start; // Byte offset where selection started (anchor)
	size_t end;	  // Byte offset where cursor is (head)

	size_t getMin() const { return std::min(start, end); }
	size_t getMax() const { return std::max(start, end); }
	bool   isEmpty() const { return start == end; }
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

// TextInput component - extends Component and implements IFocusable for keyboard focus
class TextInput : public Component, public IFocusable {
  public:
	// Constructor arguments struct
	struct Args {
		Foundation::Vec2  position{0.0F, 0.0F};
		Foundation::Vec2  size{200.0F, 32.0F};
		std::string		  text;
		std::string		  placeholder;
		TextInputStyle	  style;
		int				  tabIndex = -1; // Tab order (-1 for auto-assign)
		const char*		  id = nullptr;
		bool			  enabled = true;
		std::function<void(const std::string&)> onChange; // Called when text changes
	};

	// --- Public Members ---

	// Geometry
	Foundation::Vec2 position{0.0F, 0.0F};
	Foundation::Vec2 size{200.0F, 32.0F};

	// Text content
	std::string text;
	std::string placeholder;

	// Style
	TextInputStyle style;

	// Callbacks
	std::function<void(const std::string&)> onChange;

	// State
	size_t						cursorPosition{0};	 // Byte offset in UTF-8 string
	std::optional<TextSelection> selection;			 // Active selection (Phase 2)
	float						cursorBlinkTimer{0.0F}; // For cursor blink animation
	float						horizontalScroll{0.0F}; // Scroll offset for overflow text

	// Properties
	bool		visible{true};
	const char* id = nullptr;
	bool		enabled{true};
	bool		focused{false};

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

	// ILayer implementation (overrides Component)
	void update(float deltaTime) override; // Update cursor blink, etc.
	void render() override;			  // Draw text input

	// IComponent event handling
	bool handleEvent(InputEvent& event) override;
	bool containsPoint(Foundation::Vec2 point) const override;

	// State management
	void setEnabled(bool newEnabled) { enabled = newEnabled; }
	bool isEnabled() const { return enabled; }
	void setText(const std::string& newText);
	const std::string& getText() const { return text; }

	// IFocusable interface implementation
	void onFocusGained() override;
	void onFocusLost() override;
	void handleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) override;
	void handleCharInput(char32_t codepoint) override;
	bool canReceiveFocus() const override;

  private:
	// Focus management
	int tabIndex{-1}; // Preserved for move operations

	// Internal state tracking
	bool mouseDown{false};
	size_t selectionAnchor{0};  // Anchor point for drag selection

	// --- Phase 1: Core Editing Operations ---

	void insertChar(char32_t c);
	void deleteCharAtCursor();		 // Delete key
	void deleteCharBeforeCursor();	 // Backspace key
	void moveCursorLeft();
	void moveCursorRight();
	void moveCursorHome();
	void moveCursorEnd();

	// --- Phase 2: Selection Operations ---

	void setSelection(size_t start, size_t end);
	void clearSelection();
	std::string getSelectedText() const;
	void deleteSelection();
	void extendSelectionLeft();	 // Shift+Left
	void extendSelectionRight(); // Shift+Right

	// --- Phase 2: Clipboard Operations ---

	void copy();	  // Ctrl+C
	void cut();		  // Ctrl+X
	void paste();	  // Ctrl+V
	void selectAll(); // Ctrl+A

	// --- Rendering Helpers ---

	void renderBackground() const;
	void renderText() const;
	void renderCursor() const;
	void renderSelection() const;	 // Phase 2
	void renderPlaceholder() const;

	// --- Utilities ---

	float  getCursorXPosition() const;						   // Pixel X for cursor position
	size_t getCursorPositionFromMouse(float mouseX) const; // Index from pixel X
	void   updateHorizontalScroll();						   // Ensure cursor is visible
};

} // namespace UI
