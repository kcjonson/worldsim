# UI Framework Architecture

Created: 2025-11-26
Last Updated: 2025-11-28
Status: Active

## Core Principle: Interface-Based Component Hierarchy

The worldsim UI framework uses a **component hierarchy with explicit interfaces**. Components form a tree where:

- **IComponent** - Base interface for anything that can render
- **ILayer** - Extension of IComponent with full lifecycle (HandleInput, Update, Render)
- **Component** - Base class for elements that can have children (uses MemoryArena for contiguous storage)
- **IFocusable** - Interface for components that can receive keyboard focus

```
Scene
└── Container (Component)
    ├── DialogLayer (Component)
    │   ├── TitleBar (Component)
    │   │   ├── Text (IComponent)
    │   │   └── CloseButton (Button extends Component + IFocusable)
    │   ├── TextInput (Component + IFocusable)
    │   └── Button (Component + IFocusable)
    └── Rectangle (IComponent)
```

## Interface Hierarchy

### IComponent (Render Only)

The minimal interface for anything that can be drawn. Shapes implement this:

```cpp
struct IComponent {
    virtual ~IComponent() = default;
    virtual void Render() = 0;
};

// Shapes are leaf nodes - render only, no children
struct Rectangle : public IComponent {
    Foundation::Vec2 position{0.0F, 0.0F};
    Foundation::Vec2 size{100.0F, 100.0F};
    Foundation::RectStyle style;

    void Render() override;
};
```

### ILayer (Full Lifecycle)

For components that need input handling and updates:

```cpp
struct ILayer : public IComponent {
    virtual void HandleInput() = 0;
    virtual void Update(float deltaTime) = 0;
    // void Render() inherited from IComponent
};
```

### Component (Base Class with Children)

The base class for any element that can contain children:

```cpp
class Component : public ILayer {
protected:
    MemoryArena m_arena{64 * 1024};      // 64KB contiguous child storage
    std::vector<IComponent*> m_children; // Child pointers into arena
    uint16_t m_generation{0};            // For handle validation

public:
    // Add any IComponent as a child
    template <typename T>
    LayerHandle AddChild(T&& child) {
        static_assert(std::is_base_of_v<IComponent, std::decay_t<T>>,
            "Child must implement IComponent");
        auto* ptr = m_arena.Allocate<std::decay_t<T>>(std::forward<T>(child));
        m_children.push_back(ptr);
        return LayerHandle::Make(static_cast<uint16_t>(m_children.size() - 1), m_generation);
    }

    // Lifecycle propagates to children
    void HandleInput() override;
    void Update(float deltaTime) override;
    void Render() override;
};
```

### IFocusable (Keyboard Focus)

For components that participate in Tab navigation:

```cpp
class IFocusable {
public:
    virtual ~IFocusable() = default;
    virtual void OnFocusGained() = 0;
    virtual void OnFocusLost() = 0;
    virtual void HandleKeyInput(engine::Key key, bool shift, bool ctrl, bool alt) = 0;
    virtual void HandleCharInput(char32_t codepoint) = 0;
    virtual bool CanReceiveFocus() const = 0;
};
```

## Why This Design?

### Problem: Visibility of Relationships

The original design used C++20 concepts which provide compile-time verification but aren't visible in class definitions:

```cpp
// Old approach - can't tell at a glance what Button implements
struct Button {
    void HandleInput();
    void Update(float dt);
    void Render();
    // ...
};
static_assert(Layer<Button>);  // Verification hidden at bottom
```

### Solution: Explicit Interface Inheritance

With virtual interfaces, the relationship is explicit:

```cpp
// New approach - immediately visible
class Button : public Component, public IFocusable {
    void HandleInput() override;    // From ILayer (via Component)
    void Update(float dt) override;
    void Render() override;

    void OnFocusGained() override;  // From IFocusable
    // ...
};
```

## Memory Arena for Contiguous Storage

Children are allocated in a contiguous memory arena instead of scattered heap allocations:

```cpp
class MemoryArena {
    std::unique_ptr<char[]> m_buffer;
    size_t m_capacity;
    size_t m_offset{0};

public:
    explicit MemoryArena(size_t capacity = 64 * 1024);  // 64KB default

    template <typename T, typename... Args>
    T* Allocate(Args&&... args) {
        // Align and allocate in contiguous buffer
        size_t aligned = (m_offset + alignof(T) - 1) & ~(alignof(T) - 1);
        if (aligned + sizeof(T) > m_capacity) {
            throw std::runtime_error("MemoryArena capacity exceeded");
        }
        T* ptr = new (m_buffer.get() + aligned) T(std::forward<Args>(args)...);
        m_offset = aligned + sizeof(T);
        return ptr;
    }
};
```

**Benefits:**
- Cache-friendly iteration over children
- No individual heap allocations per child
- Memory locality for better performance with thousands of layers

## Handle-Based References

Children are referenced by handle (index + generation), not raw pointers:

**See:** [Resource Handles](../resource-handles.md) for complete documentation.

```cpp
struct LayerHandle {
    uint32_t value{kInvalidHandle};
    static constexpr uint32_t kInvalidHandle = 0xFFFFFFFF;

    bool IsValid() const;
    uint16_t GetIndex() const;      // Lower 16 bits
    uint16_t GetGeneration() const; // Upper 16 bits
    static LayerHandle Make(uint16_t index, uint16_t generation);
};
```

## Component Types

### Shapes (IComponent)

Leaf nodes with no children. Render-only:

| Shape | Description |
|-------|-------------|
| `Rectangle` | Filled/bordered rectangle |
| `Circle` | Filled/bordered circle |
| `Line` | Line segment |
| `Text` | Text with font rendering |

Usage:
```cpp
// Shapes use Args constructor (non-aggregate due to IComponent base)
UI::Text title(UI::Text::Args{
    .position = {50.0F, 40.0F},
    .text = "Hello World",
    .style = {.color = {1.0F, 1.0F, 1.0F, 1.0F}, .fontSize = 24.0F}
});
title.Render();
```

### Container (Component)

Pure organizational component with no visual representation:

```cpp
class Container : public Component {
public:
    Container() = default;
    // Inherits AddChild, lifecycle methods from Component
};
```

### Interactive Components (Component + IFocusable)

Full-featured UI widgets:

| Component | Description |
|-----------|-------------|
| `Button` | Interactive button with states |
| `TextInput` | Text editing with cursor, selection, clipboard |

Usage:
```cpp
auto button = std::make_unique<UI::Button>(UI::Button::Args{
    .label = "Click Me",
    .position = {100.0F, 100.0F},
    .size = {120.0F, 40.0F},
    .onClick = []() { LOG_INFO(UI, "Clicked!"); }
});

// Lifecycle called each frame
button->HandleInput();
button->Update(deltaTime);
button->Render();
```

## Component Lifecycle

All ILayer components implement the same lifecycle:

```
HandleInput()  →  Update(deltaTime)  →  Render()
```

### Lifecycle Propagation

Component's base implementation propagates to children:

```cpp
void Component::HandleInput() {
    for (IComponent* child : m_children) {
        if (auto* layer = dynamic_cast<ILayer*>(child)) {
            layer->HandleInput();
        }
    }
}

void Component::Render() {
    for (IComponent* child : m_children) {
        child->Render();
    }
}
```

## FocusManager Integration

FocusManager handles Tab navigation using the IFocusable interface:

```cpp
// Register focusable components
FocusManager::Get().RegisterFocusable(&button, tabIndex);

// Tab navigation
FocusManager::Get().FocusNext();     // Tab
FocusManager::Get().FocusPrevious(); // Shift+Tab

// Route keyboard input to focused component
FocusManager::Get().RouteKeyInput(key, shift, ctrl, alt);
```

Components automatically register/unregister in constructor/destructor.

## Implementation Files

| File | Purpose |
|------|---------|
| `/libs/ui/component/component.h` | IComponent, ILayer, MemoryArena, Component base |
| `/libs/ui/component/container.h` | Container class |
| `/libs/ui/layer/layer.h` | LayerHandle definition |
| `/libs/ui/shapes/shapes.h` | Rectangle, Circle, Line, Text |
| `/libs/ui/focus/focusable.h` | IFocusable interface |
| `/libs/ui/focus/focus_manager.h` | FocusManager singleton |
| `/libs/ui/components/button/` | Button component |
| `/libs/ui/components/text_input/` | TextInput component |

## Comparison with Previous Design

| Aspect | Previous (Concepts) | Current (Interfaces) |
|--------|--------------------|-----------------------|
| Interface visibility | Hidden static_asserts | Explicit inheritance |
| Compile-time check | Concept satisfaction | Virtual override |
| Runtime overhead | Zero (concepts) | Minimal (vtable) |
| Child storage | std::variant | MemoryArena |
| Scalability | Limited by variant types | Unlimited IComponent |

The current design trades minimal runtime overhead (~2-5ns per virtual call) for explicit interface visibility and unlimited scalability of component types.

## Related Documentation

- [Resource Handles](../resource-handles.md) - Handle pattern for safe references
- [Batched Text Rendering](./batched-text-rendering.md) - How text shapes render
- [SDF Text Rendering](./sdf-text-rendering.md) - Font rendering pipeline
