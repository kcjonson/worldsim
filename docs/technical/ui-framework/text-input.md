# TextInput Component

## Overview

The TextInput component provides a production-ready single-line text editing widget for worldsim's UI framework. It supports cursor positioning, text editing, keyboard navigation, text selection, and clipboard operations.

**Key Features:**
- Single-line text editing with scrolling overflow support
- Blinking cursor with keyboard and mouse positioning
- Text selection via mouse drag and Shift+Arrow keys
- Clipboard integration (Copy, Cut, Paste via Ctrl+C/X/V)
- Focus visual feedback (border highlighting)
- Placeholder text support
- Input validation and character filtering
- onChange callbacks for reactive UIs
- IFocusable integration for Tab navigation

**Phased Implementation:**
- **Phase 1 (MVP)**: Cursor, text editing, keyboard navigation, mouse click-to-focus
- **Phase 2 (Clipboard)**: Text selection, copy/cut/paste, visual selection highlighting
- **Phase 3 (Future)**: Undo/redo, word-boundary navigation, double-click selection

This document describes Phase 1 + 2 implementation (production-ready for most use cases).

---

## Architecture

### Component Structure

```cpp
namespace UI {
    struct TextInput : IFocusable {
        // Position and size
        Foundation::Vec2 position;
        Foundation::Vec2 size;

        // Text content
        std::string text;
        std::string placeholder;  // Shown when text is empty and not focused

        // Style
        TextInputStyle style;

        // Callbacks
        std::function<void(const std::string&)> onChange;    // Called when text changes
        std::function<bool(char32_t)> charValidator;         // Return false to reject character

        // State
        size_t cursorPosition{0};                             // Index in UTF-8 string (byte offset)
        std::optional<TextSelection> selection;               // Start/end byte offsets (Phase 2)
        float cursorBlinkTimer{0.0f};                         // For cursor blink animation
        float horizontalScroll{0.0f};                         // Scroll offset for overflow text

        // Lifecycle
        TextInput(const Foundation::Vec2& pos, const Foundation::Vec2& sz);
        ~TextInput();

        void Update(float deltaTime);
        void Render() const;
        void HandleInput();  // Mouse click detection (called before HandleInput in scene)

        // IFocusable implementation
        void OnFocusGained() override;
        void OnFocusLost() override;
        void HandleKeyInput(Engine::Key key, bool shift, bool ctrl, bool alt) override;
        void HandleCharInput(char32_t codepoint) override;
        bool CanReceiveFocus() const override;

    private:
        // Editing operations (Phase 1)
        void InsertChar(char32_t c);
        void DeleteCharAtCursor();
        void DeleteCharBeforeCursor();
        void MoveCursorLeft();
        void MoveCursorRight();
        void MoveCursorHome();
        void MoveCursorEnd();

        // Selection operations (Phase 2)
        void SetSelection(size_t start, size_t end);
        void ClearSelection();
        std::string GetSelectedText() const;
        void DeleteSelection();
        void ExtendSelectionLeft();   // Shift+Left
        void ExtendSelectionRight();  // Shift+Right

        // Clipboard operations (Phase 2)
        void Copy();   // Ctrl+C
        void Cut();    // Ctrl+X
        void Paste();  // Ctrl+V
        void SelectAll();  // Ctrl+A

        // Rendering helpers
        void RenderBackground() const;
        void RenderText() const;
        void RenderCursor() const;
        void RenderSelection() const;  // Phase 2
        void RenderPlaceholder() const;

        // Utilities
        float GetCursorXPosition() const;         // Pixel X for cursor position
        size_t GetCursorPositionFromMouse(float mouseX) const;  // Index from pixel X
        void UpdateHorizontalScroll();            // Ensure cursor is visible
    };

    struct TextSelection {
        size_t start;  // Byte offset of selection start
        size_t end;    // Byte offset of selection end (can be < start if selecting backwards)

        size_t GetMin() const { return std::min(start, end); }
        size_t GetMax() const { return std::max(start, end); }
        bool IsEmpty() const { return start == end; }
    };

    struct TextInputStyle {
        // Background and border
        Renderer::Color backgroundColor{0.15f, 0.15f, 0.15f, 1.0f};
        Renderer::Color borderColor{0.4f, 0.4f, 0.4f, 1.0f};
        Renderer::Color focusedBorderColor{0.2f, 0.5f, 0.9f, 1.0f};
        float borderWidth{1.0f};
        float cornerRadius{4.0f};

        // Text
        Renderer::Color textColor{1.0f, 1.0f, 1.0f, 1.0f};
        Renderer::Color placeholderColor{0.5f, 0.5f, 0.5f, 1.0f};
        float fontSize{16.0f};
        TextAlign textAlign{TextAlign::Left};

        // Cursor
        Renderer::Color cursorColor{1.0f, 1.0f, 1.0f, 1.0f};
        float cursorWidth{1.0f};
        float cursorBlinkRate{0.5f};  // Seconds per blink cycle

        // Selection (Phase 2)
        Renderer::Color selectionColor{0.2f, 0.4f, 0.8f, 0.5f};

        // Padding
        float paddingLeft{8.0f};
        float paddingRight{8.0f};
        float paddingTop{6.0f};
        float paddingBottom{6.0f};
    };
}
```

### State Machine

```
┌─────────────┐
│  Unfocused  │  ◄─── Initial state
└──────┬──────┘
       │ Mouse Click / Tab to focus
       ▼
┌─────────────┐
│   Focused   │  ◄─── Receives keyboard input
│  (No Sel)   │       Cursor visible & blinking
└──────┬──────┘
       │ Shift+Arrow / Mouse Drag
       ▼
┌─────────────┐
│   Focused   │  ◄─── Selection active
│  (With Sel) │       Selection highlighted, cursor at selection end
└──────┬──────┘
       │ Arrow key (no Shift) / Type character
       ▼
┌─────────────┐
│   Focused   │  ◄─── Selection cleared
│  (No Sel)   │       Cursor moved to selection start/end
└─────────────┘
```

---

## Phase 1: Core Text Editing

### Cursor Management

**Cursor Rendering:**
- Vertical line at cursor position (1px width by default)
- Blinks with 0.5s period (0.25s visible, 0.25s hidden)
- Always visible when cursor moves or text changes
- Hidden when component loses focus

**Cursor Positioning:**
- `cursorPosition` is a byte offset into the UTF-8 string (not character index)
- Range: 0 to `text.size()` (inclusive, can be at end of string)
- Movement operations use UTF-8 character boundaries

**Cursor Blink Animation:**
```cpp
void TextInput::Update(float deltaTime) {
    if (focused) {
        cursorBlinkTimer += deltaTime;
        if (cursorBlinkTimer > style.cursorBlinkRate) {
            cursorBlinkTimer -= style.cursorBlinkRate;
        }
    }
}

void TextInput::RenderCursor() const {
    if (!focused) return;
    if (selection.has_value()) return;  // No cursor when selection active

    // Blink: visible for first half of cycle
    bool visible = cursorBlinkTimer < (style.cursorBlinkRate * 0.5f);
    if (!visible) return;

    float cursorX = GetCursorXPosition();
    float textAreaY = position.y + style.paddingTop;
    float textHeight = style.fontSize;

    Primitives::DrawLine(
        {cursorX, textAreaY},
        {cursorX, textAreaY + textHeight},
        style.cursorColor,
        style.cursorWidth
    );
}
```

### Text Editing Operations

**Character Insertion:**
```cpp
void TextInput::InsertChar(char32_t codepoint) {
    // Validate character (if validator provided)
    if (charValidator && !charValidator(codepoint)) {
        return;  // Reject character
    }

    // Delete selection first (if active)
    if (selection.has_value()) {
        DeleteSelection();
    }

    // Convert codepoint to UTF-8
    std::string utf8 = UTF8::Encode(codepoint);

    // Insert at cursor position
    text.insert(cursorPosition, utf8);
    cursorPosition += utf8.size();

    // Reset cursor blink
    cursorBlinkTimer = 0.0f;

    // Update scroll and notify
    UpdateHorizontalScroll();
    if (onChange) onChange(text);
}
```

**Deletion:**
```cpp
void TextInput::DeleteCharAtCursor() {
    if (selection.has_value()) {
        DeleteSelection();
        return;
    }

    if (cursorPosition >= text.size()) return;  // At end

    // Find next UTF-8 character boundary
    size_t charSize = UTF8::CharacterSize(text[cursorPosition]);
    text.erase(cursorPosition, charSize);

    cursorBlinkTimer = 0.0f;
    UpdateHorizontalScroll();
    if (onChange) onChange(text);
}

void TextInput::DeleteCharBeforeCursor() {
    if (selection.has_value()) {
        DeleteSelection();
        return;
    }

    if (cursorPosition == 0) return;  // At start

    // Find previous UTF-8 character boundary
    size_t charSize = UTF8::PreviousCharacterSize(text, cursorPosition);
    text.erase(cursorPosition - charSize, charSize);
    cursorPosition -= charSize;

    cursorBlinkTimer = 0.0f;
    UpdateHorizontalScroll();
    if (onChange) onChange(text);
}
```

### Keyboard Navigation

**Arrow Keys:**
```cpp
void TextInput::HandleKeyInput(Engine::Key key, bool shift, bool ctrl, bool alt) {
    // Phase 2: Shift extends selection
    if (shift) {
        if (key == Engine::Key::Left) { ExtendSelectionLeft(); return; }
        if (key == Engine::Key::Right) { ExtendSelectionRight(); return; }
    }

    // Clear selection when moving without Shift
    if (selection.has_value() && !shift) {
        if (key == Engine::Key::Left || key == Engine::Key::Right) {
            // Move cursor to selection start/end
            cursorPosition = (key == Engine::Key::Left) ?
                selection->GetMin() : selection->GetMax();
            ClearSelection();
            UpdateHorizontalScroll();
            return;
        }
    }

    switch (key) {
        case Engine::Key::Left:
            MoveCursorLeft();
            break;
        case Engine::Key::Right:
            MoveCursorRight();
            break;
        case Engine::Key::Home:
            MoveCursorHome();
            break;
        case Engine::Key::End:
            MoveCursorEnd();
            break;
        case Engine::Key::Delete:
            DeleteCharAtCursor();
            break;
        case Engine::Key::Backspace:
            DeleteCharBeforeCursor();
            break;
        // Phase 2: Clipboard operations
        case Engine::Key::C:
            if (ctrl) Copy();
            break;
        case Engine::Key::X:
            if (ctrl) Cut();
            break;
        case Engine::Key::V:
            if (ctrl) Paste();
            break;
        case Engine::Key::A:
            if (ctrl) SelectAll();
            break;
    }
}
```

### Mouse Click-to-Focus

**Click Detection:**
```cpp
void TextInput::HandleInput() {
    auto& input = Application::GetInputManager();

    // Check for mouse click
    if (input.IsMouseButtonPressed(Engine::MouseButton::Left)) {
        auto mousePos = input.GetMousePosition();

        // Check if click is inside text input
        if (IsPointInside(mousePos)) {
            // Grab focus
            Application::GetFocusManager().SetFocus(this);

            // Position cursor from mouse X
            float localX = mousePos.x - position.x - style.paddingLeft + horizontalScroll;
            cursorPosition = GetCursorPositionFromMouse(localX);
            cursorBlinkTimer = 0.0f;
            ClearSelection();
        }
    }
}

size_t TextInput::GetCursorPositionFromMouse(float localX) const {
    // Use FontRenderer to measure text up to click position
    auto& fontRenderer = Application::GetFontRenderer();

    size_t bestPosition = 0;
    float bestDistance = std::abs(localX);

    for (size_t i = 0; i <= text.size(); ++i) {
        float x = fontRenderer.MeasureText(text.substr(0, i), style.fontSize);
        float distance = std::abs(x - localX);

        if (distance < bestDistance) {
            bestDistance = distance;
            bestPosition = i;
        }
    }

    return bestPosition;
}
```

### Horizontal Scrolling

**Scroll to Keep Cursor Visible:**
```cpp
void TextInput::UpdateHorizontalScroll() {
    float cursorX = GetCursorXPosition();  // Relative to text area
    float visibleWidth = size.x - style.paddingLeft - style.paddingRight;

    // Scroll right if cursor is past right edge
    if (cursorX - horizontalScroll > visibleWidth) {
        horizontalScroll = cursorX - visibleWidth;
    }

    // Scroll left if cursor is past left edge
    if (cursorX - horizontalScroll < 0) {
        horizontalScroll = cursorX;
    }

    // Clamp to ensure text fills from left when possible
    float textWidth = Application::GetFontRenderer().MeasureText(text, style.fontSize);
    if (textWidth < visibleWidth) {
        horizontalScroll = 0.0f;  // No scroll needed
    } else {
        horizontalScroll = std::max(0.0f, std::min(horizontalScroll, textWidth - visibleWidth));
    }
}
```

---

## Phase 2: Selection & Clipboard

### Text Selection

**Selection State:**
```cpp
struct TextSelection {
    size_t start;  // Byte offset where selection started (anchor)
    size_t end;    // Byte offset where cursor is (head)

    size_t GetMin() const { return std::min(start, end); }
    size_t GetMax() const { return std::max(start, end); }
    bool IsEmpty() const { return start == end; }
};

std::optional<TextSelection> selection;  // None if no selection
```

**Selection via Shift+Arrow:**
```cpp
void TextInput::ExtendSelectionLeft() {
    // Start selection if none exists
    if (!selection.has_value()) {
        selection = TextSelection{cursorPosition, cursorPosition};
    }

    // Move cursor (selection end) left
    if (cursorPosition > 0) {
        size_t charSize = UTF8::PreviousCharacterSize(text, cursorPosition);
        cursorPosition -= charSize;
        selection->end = cursorPosition;
    }

    // Clear selection if start == end
    if (selection->IsEmpty()) {
        selection = std::nullopt;
    }

    cursorBlinkTimer = 0.0f;
    UpdateHorizontalScroll();
}

void TextInput::ExtendSelectionRight() {
    if (!selection.has_value()) {
        selection = TextSelection{cursorPosition, cursorPosition};
    }

    if (cursorPosition < text.size()) {
        size_t charSize = UTF8::CharacterSize(text[cursorPosition]);
        cursorPosition += charSize;
        selection->end = cursorPosition;
    }

    if (selection->IsEmpty()) {
        selection = std::nullopt;
    }

    cursorBlinkTimer = 0.0f;
    UpdateHorizontalScroll();
}
```

**Selection via Mouse Drag:**
```cpp
void TextInput::HandleInput() {
    auto& input = Application::GetInputManager();

    // Start selection on mouse press
    if (input.IsMouseButtonPressed(Engine::MouseButton::Left)) {
        auto mousePos = input.GetMousePosition();
        if (IsPointInside(mousePos)) {
            Application::GetFocusManager().SetFocus(this);

            float localX = mousePos.x - position.x - style.paddingLeft + horizontalScroll;
            cursorPosition = GetCursorPositionFromMouse(localX);
            selection = TextSelection{cursorPosition, cursorPosition};
            cursorBlinkTimer = 0.0f;
        }
    }

    // Extend selection while dragging
    if (input.IsMouseButtonDown(Engine::MouseButton::Left) && selection.has_value()) {
        auto mousePos = input.GetMousePosition();
        float localX = mousePos.x - position.x - style.paddingLeft + horizontalScroll;
        cursorPosition = GetCursorPositionFromMouse(localX);
        selection->end = cursorPosition;

        // Clear selection if start == end
        if (selection->IsEmpty()) {
            selection = std::nullopt;
        }

        UpdateHorizontalScroll();
    }
}
```

**Selection Rendering:**
```cpp
void TextInput::RenderSelection() const {
    if (!selection.has_value()) return;

    size_t selStart = selection->GetMin();
    size_t selEnd = selection->GetMax();

    auto& fontRenderer = Application::GetFontRenderer();

    // Measure text positions
    float startX = fontRenderer.MeasureText(text.substr(0, selStart), style.fontSize);
    float endX = fontRenderer.MeasureText(text.substr(0, selEnd), style.fontSize);

    // Account for scroll
    startX -= horizontalScroll;
    endX -= horizontalScroll;

    // Clamp to visible area
    float visibleLeft = 0.0f;
    float visibleRight = size.x - style.paddingLeft - style.paddingRight;
    startX = std::max(visibleLeft, std::min(startX, visibleRight));
    endX = std::max(visibleLeft, std::min(endX, visibleRight));

    // Draw selection rectangle
    float selX = position.x + style.paddingLeft + startX;
    float selY = position.y + style.paddingTop;
    float selWidth = endX - startX;
    float selHeight = style.fontSize;

    Primitives::DrawRect(
        {selX, selY},
        {selWidth, selHeight},
        RectStyle{.fillColor = style.selectionColor, .borderWidth = 0.0f}
    );
}
```

### Clipboard Operations

**GLFW Clipboard Integration:**
```cpp
void TextInput::Copy() {
    if (!selection.has_value()) return;

    std::string selectedText = GetSelectedText();
    if (!selectedText.empty()) {
        glfwSetClipboardString(Application::GetWindow(), selectedText.c_str());
    }
}

void TextInput::Cut() {
    if (!selection.has_value()) return;

    Copy();
    DeleteSelection();
}

void TextInput::Paste() {
    const char* clipboardText = glfwGetClipboardString(Application::GetWindow());
    if (!clipboardText) return;

    // Delete selection first
    if (selection.has_value()) {
        DeleteSelection();
    }

    // Insert clipboard text at cursor
    std::string textToPaste(clipboardText);

    // Filter to single line (replace newlines with spaces)
    std::replace(textToPaste.begin(), textToPaste.end(), '\n', ' ');
    std::replace(textToPaste.begin(), textToPaste.end(), '\r', ' ');

    // Apply character validator to each character
    if (charValidator) {
        std::string validated;
        for (char32_t c : UTF8::Decode(textToPaste)) {
            if (charValidator(c)) {
                validated += UTF8::Encode(c);
            }
        }
        textToPaste = validated;
    }

    // Insert text
    text.insert(cursorPosition, textToPaste);
    cursorPosition += textToPaste.size();

    cursorBlinkTimer = 0.0f;
    UpdateHorizontalScroll();
    if (onChange) onChange(text);
}

void TextInput::SelectAll() {
    if (text.empty()) return;

    selection = TextSelection{0, text.size()};
    cursorPosition = text.size();
    UpdateHorizontalScroll();
}

std::string TextInput::GetSelectedText() const {
    if (!selection.has_value()) return "";

    size_t start = selection->GetMin();
    size_t end = selection->GetMax();
    return text.substr(start, end - start);
}
```

---

## Rendering Pipeline

Rendering order (back to front):
1. Background rectangle with border
2. Selection highlight (if any)
3. Text (visible portion, clipped by scissor test)
4. Cursor (if focused and no selection)
5. Placeholder text (if text empty and not focused)

```cpp
void TextInput::Render() const {
    RenderBackground();

    // Scissor test to clip overflowing text
    Primitives::PushScissor(
        position.x + style.paddingLeft,
        position.y + style.paddingTop,
        size.x - style.paddingLeft - style.paddingRight,
        size.y - style.paddingTop - style.paddingBottom
    );

    if (text.empty() && !focused) {
        RenderPlaceholder();
    } else {
        RenderSelection();  // Phase 2
        RenderText();
        RenderCursor();
    }

    Primitives::PopScissor();
}

void TextInput::RenderBackground() const {
    auto borderColor = focused ? style.focusedBorderColor : style.borderColor;

    Primitives::DrawRect(
        position,
        size,
        RectStyle{
            .fillColor = style.backgroundColor,
            .borderColor = borderColor,
            .borderWidth = style.borderWidth,
            .cornerRadius = style.cornerRadius
        }
    );
}

void TextInput::RenderText() const {
    float textX = position.x + style.paddingLeft - horizontalScroll;
    float textY = position.y + style.paddingTop;

    Primitives::DrawText(
        text,
        {textX, textY},
        TextStyle{
            .color = style.textColor,
            .fontSize = style.fontSize,
            .align = TextAlign::Left
        }
    );
}
```

---

## Focus Integration

TextInput implements IFocusable for Tab navigation:

```cpp
void TextInput::OnFocusGained() {
    focused = true;
    cursorBlinkTimer = 0.0f;  // Reset blink (cursor visible)
}

void TextInput::OnFocusLost() {
    focused = false;
    ClearSelection();  // Clear selection when focus lost
}

bool TextInput::CanReceiveFocus() const {
    return enabled;  // Disabled inputs can't receive focus
}

// Constructor
TextInput::TextInput(const Foundation::Vec2& pos, const Foundation::Vec2& sz)
    : position(pos), size(sz) {
    Application::GetFocusManager().RegisterFocusable(this, -1);
}

// Destructor
TextInput::~TextInput() {
    Application::GetFocusManager().UnregisterFocusable(this);
}
```

---

## Usage Examples

### Example 1: Basic Text Input

```cpp
// Create text input
TextInput nameInput{{100, 100}, {200, 32}};
nameInput.placeholder = "Enter your name";

// Scene update and render
void Update(float deltaTime) {
    nameInput.Update(deltaTime);
}

void Render() {
    nameInput.Render();
}
```

### Example 2: Text Input with Validation

```cpp
// Numbers-only text input
TextInput numberInput{{100, 150}, {200, 32}};
numberInput.placeholder = "Enter a number";
numberInput.charValidator = [](char32_t c) {
    return (c >= '0' && c <= '9') || c == '.';
};
```

### Example 3: Text Input with onChange

```cpp
// Username input with real-time validation
TextInput usernameInput{{100, 200}, {200, 32}};
usernameInput.placeholder = "Username";
usernameInput.onChange = [](const std::string& text) {
    bool isValid = text.length() >= 3 && text.length() <= 20;
    // Update UI to show validation status
    LOG_INFO("Username: {}, Valid: {}", text, isValid);
};
```

### Example 4: Form with Multiple Inputs

```cpp
struct LoginForm {
    TextInput usernameInput{{100, 100}, {300, 40}};
    TextInput passwordInput{{100, 160}, {300, 40}};
    Button submitButton{{100, 220}, {300, 40}};

    LoginForm() {
        // Configure inputs
        usernameInput.placeholder = "Username";
        passwordInput.placeholder = "Password";
        passwordInput.charValidator = [](char32_t c) {
            return c != ' ';  // No spaces in password
        };

        // Tab order: username (0) -> password (1) -> submit (2)
        auto& fm = Application::GetFocusManager();
        fm.RegisterFocusable(&usernameInput, 0);
        fm.RegisterFocusable(&passwordInput, 1);
        fm.RegisterFocusable(&submitButton, 2);

        // Focus first input
        fm.SetFocus(&usernameInput);
    }

    void HandleInput() {
        usernameInput.HandleInput();
        passwordInput.HandleInput();
        submitButton.HandleInput();
    }

    void Update(float dt) {
        usernameInput.Update(dt);
        passwordInput.Update(dt);
        submitButton.Update(dt);
    }

    void Render() const {
        usernameInput.Render();
        passwordInput.Render();
        submitButton.Render();
    }
};
```

---

## Design Decisions

### Why Byte Offsets for Cursor Position?

**UTF-8 String Representation:**

Worldsim uses `std::string` for text, which stores UTF-8 encoded bytes. Multi-byte characters (emoji, accented characters, etc.) can be 1-4 bytes each.

**Byte Offset vs Character Index:**
- `cursorPosition` is a byte offset (0 to `text.size()`)
- NOT a character index (which would require decoding)

**Rationale:**
- `std::string::insert/erase` operate on byte offsets
- No need to decode entire string to find cursor position
- Efficient for ASCII text (1 byte = 1 character)
- UTF-8 iteration utilities handle multi-byte characters at boundaries

**Helper Functions:**
```cpp
namespace UTF8 {
    size_t CharacterSize(char firstByte);  // Returns 1-4
    size_t PreviousCharacterSize(const std::string& str, size_t offset);
}
```

### Why std::optional for Selection?

**Null Selection Pattern:**

Three possible states:
1. No selection: `selection = std::nullopt`
2. Empty selection (cursor moving): `selection = {pos, pos}` (cleared immediately)
3. Active selection: `selection = {start, end}`

**Alternative: Always have selection = {cursor, cursor}**

Rejected because:
- Cursor and selection are mutually exclusive visually
- Empty selection is confusing state (is there a selection or not?)
- `std::optional` makes state explicit (has_value() check)

### Why Clear Selection on Arrow Keys?

**Web/Desktop Text Input Behavior:**

Standard behavior:
- Arrow keys (without Shift) collapse selection and move cursor
- Shift+Arrow extends or starts selection
- Typing deletes selection and inserts character

**Rationale:**
- Matches user expectations from all text editors
- Prevents accidental deletion of selected text
- Clear visual feedback (selection disappears, cursor visible)

### Why Mouse Drag Selection?

**Expected UX Pattern:**

- Click positions cursor (no selection)
- Click and drag selects text
- Release mouse commits selection

**Implementation:**
- `IsMouseButtonPressed()` - Start selection (anchor at click position)
- `IsMouseButtonDown()` - Extend selection (head follows mouse)
- Release - Selection remains until cleared

**Alternative: Shift+Click**

Supports both patterns:
- Drag for quick selection (common use case)
- Shift+Click for precise character selection (power user feature)

### Why GLFW Clipboard?

**Platform Clipboard Integration:**

GLFW provides cross-platform clipboard API:
- `glfwSetClipboardString(window, text)` - Copy to system clipboard
- `glfwGetClipboardString(window)` - Paste from system clipboard

**Limitations:**
- Text only (no rich text, images, etc.)
- Synchronous API (can block on some platforms)
- No clipboard history or monitoring

**Rationale:**
- Simple API, works across Windows/Mac/Linux
- Sufficient for text input use case
- No additional dependencies

**Future: Platform-Specific Clipboard:**
- macOS: NSPasteboard (rich text, images)
- Windows: Windows Clipboard API
- Linux: X11 selection / Wayland data device

---

## Future Enhancements (Phase 3)

### Undo/Redo

**Command Pattern:**
```cpp
struct TextEditCommand {
    std::string textBefore;
    std::string textAfter;
    size_t cursorBefore;
    size_t cursorAfter;
};

std::vector<TextEditCommand> undoStack;
size_t undoPosition{0};

void Undo();  // Ctrl+Z
void Redo();  // Ctrl+Y or Ctrl+Shift+Z
```

**Features:**
- Configurable undo depth (default: 50 commands)
- Command coalescing (typing "hello" is one command, not 5)
- Undo after paste, cut, delete selection

### Word-Boundary Navigation

**Ctrl+Left/Right:**
- Jump to previous/next word boundary
- Word = alphanumeric sequence separated by whitespace/punctuation

**Ctrl+Backspace/Delete:**
- Delete entire word backward/forward
- Common in IDE text editors

### Double-Click Selection

**UX Pattern:**
- Single click: position cursor
- Double click: select word under cursor
- Triple click: select all (future)

**Implementation:**
- Track time between clicks
- If < 500ms, treat as double-click
- Select word boundaries around click position

### Input Masking

**Use Cases:**
- Phone numbers: (123) 456-7890
- Dates: MM/DD/YYYY
- Credit cards: 1234 5678 9012 3456

**API:**
```cpp
textInput.inputMask = "(###) ###-####";
textInput.maskPlaceholder = "_";  // Shown for unfilled positions
```

### Password Mode

**Masked Input:**
```cpp
textInput.isPassword = true;  // Render dots instead of characters
```

**Features:**
- Render '•' for each character
- Clipboard copy disabled (security)
- Selection still works (but shows dots)

### IME Support (Internationalization)

**Complex Input Methods:**

For CJK (Chinese, Japanese, Korean) languages:
- Composition window for candidate characters
- Pre-edit text underline
- Candidate selection UI

**GLFW Support:**
- GLFW does not provide IME support directly
- Would require platform-specific APIs (Windows IMM, macOS NSTextInputClient, Linux ibus/fcitx)

---

## Performance Considerations

### Text Measurement Caching

**Problem:**
- Measuring text is expensive (FontRenderer glyph metrics)
- Called frequently (cursor positioning, scrolling, click detection)

**Solution:**
- Cache measured widths for text up to cursor position
- Invalidate cache when text changes

**Future Optimization:**
```cpp
mutable std::optional<float> cachedTextWidth;
mutable std::string cachedText;

float GetTextWidth() const {
    if (!cachedTextWidth.has_value() || cachedText != text) {
        cachedTextWidth = fontRenderer.MeasureText(text, style.fontSize);
        cachedText = text;
    }
    return *cachedTextWidth;
}
```

### Glyph Quad Caching

**Batched Text Rendering:**

TextInput rendering already uses batched DrawText (see batched-text-rendering.md):
- Single draw call for all TextInput instances in scene
- Glyph quads cached by TextBatchRenderer
- Z-ordering ensures correct layering

**No Additional Optimization Needed**

---

## Testing Strategy

### Unit Tests

**Text Editing:**
- Insert character at various positions
- Delete at start, middle, end
- Backspace at various positions
- UTF-8 multi-byte character handling

**Cursor Movement:**
- Arrow keys move by character (not byte)
- Home/End move to string boundaries
- Cursor clamped to valid range

**Selection:**
- Shift+Arrow extends selection correctly
- Selection min/max calculation
- Delete selection removes correct range
- Typing replaces selection

**Clipboard:**
- Copy selection to clipboard (mock GLFW)
- Cut deletes selection after copy
- Paste inserts at cursor / replaces selection
- Newlines in paste converted to spaces

### Integration Tests

**Focus Management:**
- Tab moves focus between TextInputs
- Mouse click gives focus
- Focus visual indicator appears
- Keyboard input routed to focused TextInput

**Visual Tests:**
- Cursor blinks correctly
- Selection highlighting visible
- Text scrolls to keep cursor visible
- Placeholder text shown when appropriate

### Manual Testing

**User Experience:**
- Test with keyboard only (no mouse)
- Test with mouse only (no keyboard)
- Test clipboard with external apps
- Test various text lengths (empty, short, long overflow)
- Test multi-byte characters (emoji, accented characters)

---

## References

**Related Worldsim Docs:**
- `/docs/technical/ui-framework/focus-management.md` - FocusManager and IFocusable
- `/docs/technical/ui-framework/batched-text-rendering.md` - Text rendering system
- `/docs/technical/ui-framework/primitive-rendering-api.md` - Primitives API

**Industry Standards:**
- [W3C Input Element Specification](https://html.spec.whatwg.org/multipage/input.html)
- [ARIA Textbox Pattern](https://www.w3.org/WAI/ARIA/apg/patterns/textbox/)
- [Qt QLineEdit Documentation](https://doc.qt.io/qt-6/qlineedit.html)

**Platform APIs:**
- [GLFW Input Guide](https://www.glfw.org/docs/latest/input_guide.html)
- [GLFW Clipboard](https://www.glfw.org/docs/latest/input_guide.html#clipboard)
