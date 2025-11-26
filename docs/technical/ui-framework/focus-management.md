# Keyboard Focus Management System

## Overview

The Focus Management System provides centralized keyboard focus tracking and navigation for UI components in worldsim. It enables Tab-based navigation between focusable elements (buttons, text inputs, etc.) and supports focus scopes for modal dialogs and other UI containers that trap focus.

**Key Features:**
- Centralized focus state management (no static component state)
- Tab/Shift+Tab navigation with customizable tab order
- Focus scope support for modal focus trapping
- Works with any component type via `IFocusable` interface
- Event-driven focus changes with lifecycle callbacks
- Keyboard-accessible UI (no mouse required)

**Design Goals:**
- Simple integration for new focusable components
- Support worldsim's value semantics architecture
- Enable accessible keyboard-only navigation
- Clean separation of concerns (focus logic separate from rendering)

---

## Architecture

### Core Components

1. **FocusManager** - Centralized service managing focus state
2. **IFocusable** - Interface for components that can receive focus
3. **FocusScope** - Stack-based focus containment for modals/dialogs

### Component Relationships

```
┌──────────────────────────────────────────────────┐
│                  Application                      │
│  ┌────────────────────────────────────────────┐  │
│  │           InputManager                     │  │
│  │  - Detects Tab/Shift+Tab key presses      │  │
│  │  - Routes keyboard events                 │  │
│  └─────────────┬──────────────────────────────┘  │
│                │                                  │
│                ▼                                  │
│  ┌──────────────────────────────────────────┐    │
│  │         FocusManager                     │    │
│  │  - Maintains focus state                 │    │
│  │  - Manages tab order                     │    │
│  │  - Focus scope stack                     │    │
│  │  - Routes events to focused component    │    │
│  └───────┬──────────────────────────────────┘    │
│          │                                        │
│          ▼                                        │
│  ┌─────────────────┐  ┌──────────────────────┐   │
│  │  Button         │  │  TextInput           │   │
│  │  (IFocusable)   │  │  (IFocusable)        │   │
│  │                 │  │                      │   │
│  │ OnFocusGained() │  │  OnFocusGained()     │   │
│  │ OnFocusLost()   │  │  OnFocusLost()       │   │
│  │ HandleKeyInput()│  │  HandleKeyInput()    │   │
│  └─────────────────┘  └──────────────────────┘   │
└──────────────────────────────────────────────────┘
```

---

## IFocusable Interface

Components that can receive keyboard focus implement this interface:

```cpp
namespace UI {
    class IFocusable {
    public:
        virtual ~IFocusable() = default;

        // Lifecycle callbacks
        virtual void OnFocusGained() = 0;
        virtual void OnFocusLost() = 0;

        // Input handling
        virtual void HandleKeyInput(Engine::Key key, bool shift, bool ctrl, bool alt) = 0;
        virtual void HandleCharInput(char32_t codepoint) = 0;

        // Focus query (for determining if component can accept focus)
        virtual bool CanReceiveFocus() const = 0;
    };
}
```

**Lifecycle Callbacks:**
- `OnFocusGained()` - Called when component receives focus (visual update, cursor enable, etc.)
- `OnFocusLost()` - Called when component loses focus (hide cursor, stop blinking, etc.)

**Input Handling:**
- `HandleKeyInput()` - Process key presses (arrow keys, Enter, Escape, etc.)
- `HandleCharInput()` - Process text character input (for text editing)

**Focus Query:**
- `CanReceiveFocus()` - Return false to skip component in tab order (e.g., disabled button)

---

## FocusManager

The FocusManager is a centralized service that tracks focus state and manages navigation.

### Class Interface

```cpp
namespace UI {
    class FocusManager {
    public:
        FocusManager() = default;
        ~FocusManager() = default;

        // Registration
        void RegisterFocusable(IFocusable* component, int tabIndex = -1);
        void UnregisterFocusable(IFocusable* component);

        // Focus control
        void SetFocus(IFocusable* component);
        void ClearFocus();
        void FocusNext();     // Tab key
        void FocusPrevious(); // Shift+Tab

        // Focus scope (for modals)
        void PushFocusScope(const std::vector<IFocusable*>& components);
        void PopFocusScope();

        // Query
        IFocusable* GetFocused() const;
        bool HasFocus(IFocusable* component) const;

        // Input routing (called by InputManager)
        void RouteKeyInput(Engine::Key key, bool shift, bool ctrl, bool alt);
        void RouteCharInput(char32_t codepoint);

    private:
        struct FocusEntry {
            IFocusable* component;
            int tabIndex;
        };

        std::vector<FocusEntry> m_focusables;      // All registered components
        IFocusable* m_currentFocus{nullptr};        // Currently focused component

        // Focus scope stack (for modal dialogs)
        struct FocusScope {
            std::vector<IFocusable*> components;
            IFocusable* previousFocus;  // Focus to restore when scope pops
        };
        std::vector<FocusScope> m_scopeStack;

        // Internal helpers
        void SortFocusables();
        std::vector<IFocusable*> GetActiveFocusables() const;
        int FindFocusIndex(IFocusable* component) const;
    };
}
```

### Key Behaviors

**Tab Order:**
- Components registered with explicit `tabIndex` are sorted numerically
- Components with `tabIndex = -1` are auto-assigned based on registration order
- Tab order is stable-sorted (equal tabIndex preserves registration order)
- `FocusNext()` moves to next component in tab order, wrapping to first
- `FocusPrevious()` moves to previous component, wrapping to last

**Focus Scopes:**
- When modal opens: `PushFocusScope({modalButtons, modalTextInputs})`
- Tab navigation restricted to scope's components only
- When modal closes: `PopFocusScope()` restores previous focus
- Scopes can nest (modal within modal)

**CanReceiveFocus() Filtering:**
- During navigation, components where `CanReceiveFocus() == false` are skipped
- Useful for disabled buttons, invisible components, etc.

---

## Focus Scopes (Modal Support)

Focus scopes enable modal dialogs to trap keyboard focus, preventing Tab from escaping the modal.

### Use Case: Modal Dialog

```cpp
// Modal opens
std::vector<IFocusable*> modalComponents = {
    &okButton,
    &cancelButton,
    &textInput
};
focusManager.PushFocusScope(modalComponents);
focusManager.SetFocus(&textInput);  // Focus first input

// User presses Tab - cycles within modal only
// User presses Escape - modal closes

// Modal closes
focusManager.PopFocusScope();  // Restores focus to element that opened modal
```

### Scope Stack Behavior

- Scopes stack like layers (modal can open another modal)
- Focus navigation always uses the topmost scope's component list
- Popping a scope restores the `previousFocus` from that scope
- Empty scope stack means global focus (all registered components)

---

## Integration with InputManager

The InputManager detects keyboard events and routes them to FocusManager:

```cpp
// In InputManager::KeyCallback (GLFW callback)
if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
    if (mods & GLFW_MOD_SHIFT) {
        Application::GetFocusManager().FocusPrevious();
    } else {
        Application::GetFocusManager().FocusNext();
    }
    return;  // Tab consumed, don't propagate
}

// Route other keys to focused component
IFocusable* focused = Application::GetFocusManager().GetFocused();
if (focused) {
    bool shift = mods & GLFW_MOD_SHIFT;
    bool ctrl  = mods & GLFW_MOD_CONTROL;
    bool alt   = mods & GLFW_MOD_ALT;

    Application::GetFocusManager().RouteKeyInput(
        ConvertGLFWKey(key), shift, ctrl, alt
    );
}

// In InputManager::CharCallback
IFocusable* focused = Application::GetFocusManager().GetFocused();
if (focused) {
    Application::GetFocusManager().RouteCharInput(codepoint);
}
```

**Key Points:**
- Tab key is intercepted and handled by FocusManager (doesn't reach components)
- All other key events routed to focused component via `HandleKeyInput()`
- Character input routed via `HandleCharInput()` for text editing
- InputManager converts GLFW key codes to worldsim `Engine::Key` enum

---

## Ownership and Lifetime

**FocusManager Ownership:**
- Single `FocusManager` instance owned by `Application` class
- Accessible via `Application::GetFocusManager()`
- Lives for entire application lifetime

**Component Registration:**
- Components register themselves in constructor (or when added to scene)
- Components unregister in destructor (critical for cleanup)
- FocusManager stores raw pointers (components responsible for lifetime)

**Safety:**
- Components MUST unregister before destruction
- FocusManager does not own components (no memory management)
- If focused component is destroyed without unregistering, behavior is undefined

**Example Component Pattern:**

```cpp
struct Button : IFocusable {
    Button() {
        Application::GetFocusManager().RegisterFocusable(this, -1);
    }

    ~Button() {
        Application::GetFocusManager().UnregisterFocusable(this);
    }

    // IFocusable implementation...
};
```

---

## API Reference

### FocusManager Methods

#### `void RegisterFocusable(IFocusable* component, int tabIndex = -1)`
Register a component for focus management.

**Parameters:**
- `component` - Pointer to component implementing IFocusable
- `tabIndex` - Explicit tab order (-1 for auto-assign based on registration order)

**Notes:**
- Components with same tabIndex are sorted by registration order (stable sort)
- Auto-assigned indices start after highest explicit index

#### `void UnregisterFocusable(IFocusable* component)`
Unregister a component (call in destructor).

**Parameters:**
- `component` - Pointer to previously registered component

**Notes:**
- If component currently has focus, focus is cleared
- Safe to call if component not registered (no-op)

#### `void SetFocus(IFocusable* component)`
Give focus to specific component.

**Parameters:**
- `component` - Component to focus (must be registered)

**Behavior:**
- Calls `OnFocusLost()` on previously focused component
- Calls `OnFocusGained()` on new component
- If component is in a scope, must be within active scope

#### `void ClearFocus()`
Remove focus from current component.

**Behavior:**
- Calls `OnFocusLost()` on currently focused component
- Sets focused component to nullptr
- Next Tab press will focus first component in tab order

#### `void FocusNext()`
Move focus to next component in tab order (Tab key).

**Behavior:**
- Skips components where `CanReceiveFocus() == false`
- Wraps from last to first component
- If no components can receive focus, clears focus

#### `void FocusPrevious()`
Move focus to previous component in tab order (Shift+Tab).

**Behavior:**
- Same as `FocusNext()` but in reverse direction

#### `void PushFocusScope(const std::vector<IFocusable*>& components)`
Push a focus scope onto the stack (for modal dialogs).

**Parameters:**
- `components` - List of components in this scope

**Behavior:**
- Saves current focus to restore when scope pops
- Tab navigation restricted to components in this scope
- Scopes can nest (scope within scope)

#### `void PopFocusScope()`
Pop the topmost focus scope and restore previous focus.

**Behavior:**
- Restores focus to component that had focus before scope was pushed
- If previous component no longer exists, focus is cleared
- Asserts if scope stack is empty (must match Push/Pop)

#### `IFocusable* GetFocused() const`
Get currently focused component.

**Returns:**
- Pointer to focused component, or nullptr if no focus

#### `bool HasFocus(IFocusable* component) const`
Check if specific component has focus.

**Parameters:**
- `component` - Component to check

**Returns:**
- True if component currently has focus

#### `void RouteKeyInput(Engine::Key key, bool shift, bool ctrl, bool alt)`
Route keyboard input to focused component (called by InputManager).

**Parameters:**
- `key` - Key pressed (from Engine::Key enum)
- `shift/ctrl/alt` - Modifier key states

**Behavior:**
- Calls `HandleKeyInput()` on focused component
- If no component has focus, does nothing

#### `void RouteCharInput(char32_t codepoint)`
Route character input to focused component (called by InputManager).

**Parameters:**
- `codepoint` - Unicode codepoint of character typed

**Behavior:**
- Calls `HandleCharInput()` on focused component
- If no component has focus, does nothing

---

## Usage Examples

### Example 1: Basic Button Focus

```cpp
// Button implements IFocusable
struct Button : IFocusable {
    bool focused{false};

    Button() {
        // Register with auto tab index
        Application::GetFocusManager().RegisterFocusable(this, -1);
    }

    ~Button() {
        Application::GetFocusManager().UnregisterFocusable(this);
    }

    void OnFocusGained() override {
        focused = true;  // Update visual state
    }

    void OnFocusLost() override {
        focused = false;
    }

    bool CanReceiveFocus() const override {
        return enabled;  // Disabled buttons skip focus
    }

    void HandleKeyInput(Engine::Key key, bool shift, bool ctrl, bool alt) override {
        if (key == Engine::Key::Enter || key == Engine::Key::Space) {
            // Activate button
            if (onClick) onClick();
        }
    }

    void HandleCharInput(char32_t codepoint) override {
        // Buttons don't handle char input
    }

    void Render() const {
        auto style = focused ? focusedStyle : normalStyle;
        Primitives::DrawRect(position, size, style);
    }
};
```

### Example 2: Modal Dialog with Focus Scope

```cpp
struct ConfirmDialog {
    Button okButton;
    Button cancelButton;

    void Open() {
        // Push focus scope (traps Tab within dialog)
        std::vector<IFocusable*> modalComponents = {
            &cancelButton,
            &okButton
        };
        Application::GetFocusManager().PushFocusScope(modalComponents);

        // Focus first button
        Application::GetFocusManager().SetFocus(&okButton);
    }

    void Close() {
        // Pop scope (restores focus to previous element)
        Application::GetFocusManager().PopFocusScope();
    }
};
```

### Example 3: Explicit Tab Order

```cpp
// Scene with specific tab order
struct FormScene : IScene {
    Button submitButton;
    TextInput nameInput;
    TextInput emailInput;
    Button cancelButton;

    void Init() override {
        auto& fm = Application::GetFocusManager();

        // Explicit tab order: name -> email -> submit -> cancel
        fm.RegisterFocusable(&nameInput, 0);
        fm.RegisterFocusable(&emailInput, 1);
        fm.RegisterFocusable(&submitButton, 2);
        fm.RegisterFocusable(&cancelButton, 3);

        // Focus first input
        fm.SetFocus(&nameInput);
    }
};
```

---

## Design Decisions

### Why Centralized FocusManager?

**Alternative: Static focus state on components (colonysim approach)**

Colonysim used `static std::shared_ptr<Text> Text::focusedTextInput` to track focus. This has several problems:
- Only supports one component type (Text inputs)
- Cannot handle buttons, sliders, or other focusable elements
- No Tab navigation support
- Tight coupling between component types

**Our Approach: Centralized service**

FocusManager decouples focus logic from components:
- Any component can be focusable (implement IFocusable)
- Single source of truth for focus state
- Tab navigation built-in
- Easy to add new focusable component types

### Why IFocusable Interface?

**Value Semantics Compatibility:**

Worldsim uses value semantics for components (structs, not shared_ptr). The IFocusable interface allows components to opt-in to focus management without requiring inheritance or shared_ptr:

```cpp
struct Button : IFocusable {  // Optional interface
    // Component data (value semantics)
    Vec2 position;
    Vec2 size;
    // ...
};
```

**Type Erasure:**

FocusManager stores `IFocusable*` pointers, allowing any component type to participate in focus management without templates or runtime type checks.

### Why Focus Scopes?

Modal dialogs and dropdown menus need to trap keyboard focus. Without scopes:
- Tab could escape modal and focus background elements
- Confusing user experience (keyboard doesn't match visual hierarchy)
- Violates accessibility guidelines (ARIA modal pattern)

Focus scopes provide:
- Stack-based containment (modal within modal support)
- Automatic focus restoration when scope pops
- Clean API (`PushFocusScope` / `PopFocusScope`)

### Why Tab Index System?

**Explicit Control When Needed:**

Most components use auto tab indices (registration order), but some UIs need explicit control:
- Forms with specific field order
- Complex layouts where visual order != DOM order
- Skip links and accessibility features

**Stable Sort:**

Components with equal tabIndex are sorted by registration order (stable sort). This provides predictable behavior and allows grouping:

```cpp
// All buttons have tabIndex 0 (sorted by registration order)
// All inputs have tabIndex 1 (sorted by registration order)
// Tab order: button1, button2, button3, input1, input2
```

---

## Future Enhancements

### Directional Navigation (Arrow Keys)

For grid layouts and game-style navigation:
- Arrow keys move focus to nearest component in direction
- Explicit neighbor relationships (up/down/left/right)
- Automatic spatial calculation based on component positions

### Focus History

Track focus history for "back" navigation:
- Stack of previously focused components
- Restore focus after closing dialogs
- Useful for complex multi-step workflows

### Accessibility Attributes

ARIA-like attributes for screen readers:
- role, aria-label, aria-describedby
- Announce focus changes
- Support for screen reader testing

### Focus Indicators

Standardized focus indicator rendering:
- Configurable focus ring styles
- Respect system preferences (Windows high contrast mode)
- Animated focus transitions

---

## References

**Industry Standards:**
- [W3C ARIA Authoring Practices - Keyboard Navigation](https://www.w3.org/WAI/ARIA/apg/practices/keyboard-interface/)
- [WebAIM - Keyboard Accessibility](https://webaim.org/techniques/keyboard/)
- [Qt Focus Management](https://doc.qt.io/qt-6/focus.html)

**Related Worldsim Docs:**
- `/docs/technical/ui-framework/text-input.md` - TextInput component spec
- `/docs/technical/ui-framework/batched-text-rendering.md` - Text rendering system
- `/docs/technical/ui-framework/component-architecture.md` - UI component patterns
