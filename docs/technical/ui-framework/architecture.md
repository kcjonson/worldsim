# UI Framework Architecture

Created: 2025-11-26
Last Updated: 2025-11-26
Status: Active

## Core Principle: Everything Is a Layer

The worldsim UI framework uses a **unified layer model**. There is no distinction between "shapes" and "components" - they are all layers in a hierarchical tree. The only difference is behavior:

- **Simple layers** (shapes) - Minimal `HandleInput`/`Update`, just `Render`
- **Interactive layers** (components) - Full lifecycle with input handling
- **All layers** can contain children and propagate lifecycle calls

```
Scene
└── RootLayer
    ├── DialogLayer (interactive)
    │   ├── TitleBar (interactive)
    │   │   ├── Text (shape)
    │   │   └── CloseButton (interactive)
    │   ├── TextInput (interactive)
    │   └── Button (interactive)
    └── BackgroundRect (shape)
```

## Why This Design?

### Problem with Separate Hierarchies

Many UI frameworks separate "primitives" (shapes) from "widgets" (components), creating two parallel systems:

```cpp
// Anti-pattern: two separate managers
ShapeManager shapeManager;     // Rectangles, circles, text
WidgetManager widgetManager;   // Buttons, text inputs

// Problems:
// - Which manager handles z-ordering?
// - How do widgets contain shapes?
// - Duplicated hierarchy code
```

### Unified Layer Solution

Everything participates in the same hierarchy:

```cpp
// Single manager for all layers
LayerManager layerManager;

// Button is just a layer that contains shape children
LayerHandle button = layerManager.Add(Button{...});
// Button internally creates shape children (background rect, label text)

// Lifecycle propagates uniformly
void Layer::HandleInput() {
    // 1. Own input handling
    // 2. Propagate to children
    for (auto& childHandle : m_children) {
        auto* child = layerManager.Get(childHandle);
        if (child) child->HandleInput();
    }
}
```

## Design Pillars

### 1. C++20 Concepts for Interface Enforcement

Instead of virtual interfaces with runtime overhead, we use C++20 concepts for compile-time enforcement:

```cpp
// Compile-time interface - zero overhead
template <typename T>
concept Layer = requires(T& l, float deltaTime) {
    { l.HandleInput() } -> std::same_as<void>;
    { l.Update(deltaTime) } -> std::same_as<void>;
    { l.Render() } -> std::same_as<void>;
};

// Fails at compile time if missing methods
static_assert(Layer<Button>);
static_assert(Layer<Rectangle>);
```

### 2. Value Semantics with Variant Storage

Layers are stored contiguously using `std::variant`, not scattered on the heap:

```cpp
using LayerData = std::variant<
    Container, Rectangle, Circle, Text, Line,  // Shapes
    Button, TextInput                          // Components
>;

std::vector<LayerNode> m_nodes;  // Contiguous storage
```

### 3. Handle-Based References

Children are referenced by handle (index + generation), not pointers. This prevents dangling references when layers are removed.

**See:** [Resource Handles](../resource-handles.md) for complete documentation of this pattern.

```cpp
// LayerHandle uses same pattern as ResourceHandle
using LayerHandle = ResourceHandle;  // 16-bit index + 16-bit generation

// Safe child access
LayerHandle label = layerManager.AddChild(parent, Text{...});

// Later: check if still valid
if (auto* text = layerManager.Get<Text>(label)) {
    text->position.x += 10;  // Safe to mutate
}
// If layer was removed, Get returns nullptr
```

## Why Not Other Approaches?

### Comparison: Colonysim vs Worldsim

Colonysim uses virtual inheritance + shared_ptr. Worldsim differs intentionally for performance:

| Aspect | Colonysim | Worldsim |
|--------|-----------|----------|
| Hierarchy | `std::shared_ptr<Layer>` children | Handle-based (index + generation) |
| Polymorphism | Virtual methods, vtable | C++20 concepts, zero overhead |
| Storage | Heap-allocated, scattered | Contiguous vector with variant |
| Memory | 8+ bytes per reference | 4 bytes per handle |
| Safety | Reference counting | Generation check for stale handles |

**Same conceptual model** (unified hierarchy), **different implementation** for performance.

### Comparison: Virtual Interface vs Concepts

| Aspect | Virtual Interface | C++20 Concepts |
|--------|------------------|----------------|
| Runtime overhead | VTable lookup per call | Zero |
| Storage | Requires pointers/heap | Value types, contiguous |
| Compile-time check | No (runtime crash on pure virtual) | Yes (compiler error) |
| Code pattern | Inheritance hierarchy | Duck typing |

### Comparison: Pointers vs Handles

| Problem with Pointers | Handle Solution |
|----------------------|-----------------|
| Dangling pointers when layer removed | Generation check detects stale handles |
| Can't tell if pointer is valid | `Get()` returns nullptr for invalid |
| Memory scattered across heap | Contiguous vector storage |
| 8 bytes per reference | 4 bytes per handle |

## Child Composition Patterns

Components choose how to reference children based on whether they need to mutate them:

### Pattern A: Static Children (Fire and Forget)

For children that never change after creation:

```cpp
void Button::Initialize() {
    // Don't store handle - child is immutable
    layerManager.AddChild(m_selfHandle, Rectangle{
        .position = m_position,
        .size = m_size,
        .style = m_appearance.normalStyle
    });
}
```

### Pattern B: Dynamic Children (Need Mutation)

For children that need runtime updates:

```cpp
struct Button {
    LayerHandle m_backgroundRect;  // Keep handle for mutation

    void Initialize() {
        m_backgroundRect = layerManager.AddChild(m_selfHandle, Rectangle{...});
    }

    void Update(float dt) {
        // Update background color based on hover state
        if (auto* bg = layerManager.Get<Rectangle>(m_backgroundRect)) {
            bg->style.fillColor = GetCurrentColor();
        }
    }
};
```

## Layer Lifecycle

All layers implement the same lifecycle, called in order each frame:

```
HandleInput()  →  Update(deltaTime)  →  Render()
```

### For Shapes

Shapes have no-op implementations for input and update:

```cpp
struct Rectangle {
    void HandleInput() {}      // No-op
    void Update(float) {}      // No-op
    void Render() const;       // Draw rectangle
};
```

### For Components

Components implement full lifecycle:

```cpp
struct Button {
    void HandleInput() {
        // Check mouse hover, clicks
        if (ContainsPoint(mousePos)) {
            if (mousePressed) m_state = Pressed;
            else m_state = Hover;
        }
    }

    void Update(float dt) {
        // Update animations, visual state
    }

    void Render() const {
        // Children render themselves via LayerManager
    }
};
```

## Focusable Concept

For layers that can receive keyboard focus:

```cpp
template <typename T>
concept Focusable = requires(T& c, engine::Key key, bool shift, bool ctrl, bool alt, char32_t codepoint) {
    { c.OnFocusGained() } -> std::same_as<void>;
    { c.OnFocusLost() } -> std::same_as<void>;
    { c.HandleKeyInput(key, shift, ctrl, alt) } -> std::same_as<void>;
    { c.HandleCharInput(codepoint) } -> std::same_as<void>;
    { c.CanReceiveFocus() } -> std::same_as<bool>;
};

static_assert(Focusable<Button>);
static_assert(Focusable<TextInput>);
```

## FocusManager Integration

### Current Implementation (Hybrid Approach)

Currently, FocusManager uses the `IFocusable` interface for runtime polymorphism, while the `Focusable` concept provides compile-time verification:

```cpp
// Current approach: IFocusable for runtime dispatch
struct Button : public IFocusable {
    void OnFocusGained() override;
    // ...
};

// Concept provides compile-time verification
static_assert(Focusable<Button>, "Button must satisfy Focusable concept");
```

This hybrid approach is pragmatic:
- IFocusable enables FocusManager's existing runtime dispatch
- Focusable concept ensures compile-time interface verification
- Migration to full variant storage can happen incrementally

### Future Optimization (Variant Storage)

For maximum performance (eliminating vtable overhead), FocusManager can be refactored to use variant storage:

```cpp
// Future approach: variant for zero-overhead dispatch
using FocusableLayer = std::variant<Button*, TextInput*>;

struct FocusEntry {
    FocusableLayer layer;
    int tabIndex;
};

// Type-safe dispatch via std::visit
void FocusManager::SetFocus(FocusableLayer& layer) {
    std::visit([](auto* l) { l->OnFocusGained(); }, layer);
}
```

This refactor would:
1. Replace `IFocusable*` with `std::variant<Button*, TextInput*>`
2. Use `std::visit` for method dispatch
3. Allow removing IFocusable inheritance from components
4. Eliminate virtual function overhead

## LayerManager API

### Adding Layers

```cpp
// Add root-level layer
LayerHandle button = layerManager.Add(Button{...});

// Add child layer
LayerHandle label = layerManager.AddChild(button, Text{...});
```

### Accessing Layers

```cpp
// Type-safe access (returns nullptr if wrong type or invalid handle)
if (auto* btn = layerManager.Get<Button>(handle)) {
    btn->SetDisabled(true);
}

// Visit pattern for unknown type
layerManager.Visit(handle, [](auto& layer) {
    layer.HandleInput();
});
```

### Removing Layers

```cpp
layerManager.Remove(handle);  // Also removes all children
// Increments generation, so old handles become invalid
```

## Implementation Files

| File | Purpose |
|------|---------|
| `/libs/ui/layer/layer.h` | Layer and Focusable concepts, LayerHandle typedef |
| `/libs/ui/layer/layer_manager.h` | LayerManager with variant storage |
| `/libs/ui/layer/layer_manager.cpp` | Implementation with handle validation |
| `/libs/ui/shapes/shapes.h` | Shape types with lifecycle methods |
| `/libs/ui/components/button/button.h` | Button satisfying Layer + Focusable |
| `/libs/ui/components/text_input/text_input.h` | TextInput satisfying Layer + Focusable |
| `/libs/ui/focus/focus_manager.h` | FocusManager with variant storage |

## Related Documentation

- [Resource Handles](../resource-handles.md) - Handle pattern we extend for layers
- [Batched Text Rendering](./batched-text-rendering.md) - How text layers render
- [SDF Text Rendering](./sdf-text-rendering.md) - Font rendering pipeline
