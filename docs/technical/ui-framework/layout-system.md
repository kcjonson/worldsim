# UI Layout System

This document describes the layout system for automatic component positioning.

## Problem Statement

The current UI system requires manual position calculation:

```cpp
// Current: Manual positioning everywhere
labelText.position = Foundation::Vec2{
    position.x + size.x / 2.0F,
    position.y + size.y / 2.0F
};

// TabBar: Manual tab layout
void recomputeLayout() {
    float currentX = position.x + kTabPadding;
    for (auto& tab : tabs) {
        tab.bounds.x = currentX;
        currentX += tab.bounds.width + kTabSpacing;
    }
}
```

This approach doesn't scale:
- Every component manually calculates child positions
- Resizing requires touching many files
- No automatic flow/wrap behavior
- Scrollable content requires manual content height tracking

## Design Goals

1. **Declarative layout** - Specify relationships, not coordinates
2. **Composable** - Layout containers can nest
3. **Minimal API** - VStack/HStack covers 90% of cases
4. **Opt-in** - Components can still use manual positioning
5. **Performance** - Layout computed once per frame, cached

## Layout Primitives

### 1. VStack (Vertical Stack)

Arranges children vertically from top to bottom.

```cpp
// API
struct VStack : public Container {
    struct Args {
        Foundation::Vec2 position{0, 0};
        Foundation::Vec2 size{0, 0};      // 0 = auto-size to content
        float spacing = 8.0f;              // Gap between children
        HAlign hAlign = HAlign::Left;      // Child horizontal alignment
    };

    explicit VStack(const Args& args);
};

// Usage
auto stack = VStack(VStack::Args{
    .position = {100, 100},
    .size = {200, 0},  // Fixed width, auto height
    .spacing = 12.0f
});

stack.addChild(Text{...});
stack.addChild(Button{...});
stack.addChild(Text{...});
// Children automatically positioned vertically
```

### 2. HStack (Horizontal Stack)

Arranges children horizontally from left to right.

```cpp
// API
struct HStack : public Container {
    struct Args {
        Foundation::Vec2 position{0, 0};
        Foundation::Vec2 size{0, 0};
        float spacing = 8.0f;
        VAlign vAlign = VAlign::Center;    // Child vertical alignment
    };

    explicit HStack(const Args& args);
};

// Usage
auto toolbar = HStack(HStack::Args{
    .position = {0, 0},
    .size = {0, 40},  // Auto width, fixed height
    .spacing = 4.0f
});

toolbar.addChild(Button{.label = "File"});
toolbar.addChild(Button{.label = "Edit"});
toolbar.addChild(Button{.label = "View"});
```

### 3. Spacer

Flexible space that expands to fill available room.

```cpp
struct Spacer : public IComponent {
    float minSize = 0.0f;   // Minimum space
    float flex = 1.0f;      // Relative flex weight
};

// Usage: Push button to right edge
auto header = HStack(HStack::Args{.size = {400, 40}});
header.addChild(Text{.text = "Title"});
header.addChild(Spacer{});  // Expands to fill
header.addChild(Button{.label = "Close"});
```

### 4. Alignment Enums

```cpp
enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Center, Bottom };
```

## Layout Protocol

### ILayoutable Interface

Components opt into layout by implementing:

```cpp
struct ILayoutable {
    // Size this component wants (may be overridden by parent)
    virtual Foundation::Vec2 preferredSize() const = 0;

    // Minimum size (layout won't shrink below this)
    virtual Foundation::Vec2 minSize() const { return {0, 0}; }

    // Set position and size (called by parent during layout)
    virtual void setBounds(const Foundation::Rect& bounds) = 0;
};
```

### Layout Phase

Layout runs once per frame, before rendering:

```cpp
// In Container::layout(const Rect& bounds)
void VStack::layout(const Rect& bounds) {
    // 1. Query preferred sizes
    float totalHeight = 0;
    for (auto* child : children) {
        if (auto* layoutable = dynamic_cast<ILayoutable*>(child)) {
            totalHeight += layoutable->preferredSize().y + m_spacing;
        }
    }

    // 2. Compute positions
    float y = bounds.y;
    for (auto* child : children) {
        if (auto* layoutable = dynamic_cast<ILayoutable*>(child)) {
            auto pref = layoutable->preferredSize();
            float x = computeXForHAlign(bounds, pref.x, m_hAlign);
            layoutable->setBounds({x, y, pref.x, pref.y});
            y += pref.y + m_spacing;
        }
    }

    // 3. Update own size if auto-sizing
    if (m_size.y == 0) {
        m_computedSize.y = totalHeight;
    }
}
```

## Common Patterns

### Pattern 1: Panel with Header and Content

```cpp
auto panel = VStack(VStack::Args{
    .position = {50, 50},
    .size = {300, 400}
});

// Header row
auto header = HStack(HStack::Args{.size = {0, 32}});
header.addChild(Text{.text = "Colonist Info"});
header.addChild(Spacer{});
header.addChild(Button{.label = "X", .size = {24, 24}});
panel.addChild(std::move(header));

// Content
panel.addChild(Text{.text = "Name: Alice"});
panel.addChild(NeedBar{.label = "Hunger", .value = 0.7f});
panel.addChild(NeedBar{.label = "Energy", .value = 0.4f});
```

### Pattern 2: Button Row

```cpp
auto buttons = HStack(HStack::Args{
    .spacing = 8.0f,
    .vAlign = VAlign::Center
});
buttons.addChild(Button{.label = "Cancel"});
buttons.addChild(Spacer{});  // Push OK to right
buttons.addChild(Button{.label = "OK", .type = Button::Primary});
```

### Pattern 3: Scrollable List

```cpp
auto scrollContainer = ScrollContainer(ScrollContainer::Args{
    .size = {200, 300}
});

auto list = VStack(VStack::Args{.spacing = 4.0f});
for (const auto& item : items) {
    list.addChild(ListItem{.text = item.name});
}

scrollContainer.setContent(std::move(list));
// ScrollContainer auto-sizes to VStack's computed height
```

### Pattern 4: Two-Column Form

```cpp
auto form = VStack(VStack::Args{.spacing = 8.0f});

auto addRow = [&](const std::string& label, auto&& input) {
    auto row = HStack(HStack::Args{.spacing = 12.0f});
    row.addChild(Text{.text = label, .size = {100, 0}});  // Fixed label width
    row.addChild(std::forward<decltype(input)>(input));
    form.addChild(std::move(row));
};

addRow("Name:", TextInput{.placeholder = "Enter name..."});
addRow("Age:", TextInput{.placeholder = "0"});
addRow("Role:", Dropdown{.options = {"Worker", "Builder", "Hunter"}});
```

## Integration with Existing System

### Backward Compatibility

Components without ILayoutable continue to work with manual positioning:

```cpp
// Old way still works
auto rect = Rectangle(Rectangle::Args{
    .position = {100, 100},
    .size = {50, 50}
});

// Shapes don't implement ILayoutable, so VStack skips them during layout
// (or we could add a simple wrapper)
```

### Gradual Migration

1. New panels use VStack/HStack from the start
2. Existing panels migrated as they're touched
3. Mixed mode (manual + layout) supported

### Container Integration

VStack and HStack extend Container, inheriting:
- Clipping support (`setClip()`)
- Content offset for scrolling (`setContentOffset()`)
- Event dispatch (`dispatchEvent()`)
- Memory arena for children

## Size Computation

### Fixed Size
```cpp
VStack(VStack::Args{.size = {200, 400}});  // Exactly 200x400
```

### Auto Size (One Dimension)
```cpp
VStack(VStack::Args{.size = {200, 0}});  // Width 200, height = content
```

### Full Auto Size
```cpp
VStack(VStack::Args{.size = {0, 0}});  // Both dimensions fit content
```

### Flex Sizing (Future)
```cpp
// Not in initial implementation, but the pattern supports it:
struct FlexItem : public ILayoutable {
    float flex = 1.0f;  // Relative weight for remaining space
};
```

## Implementation Plan

### Phase 1: Core Layout
1. Create `ILayoutable` interface
2. Implement `VStack` with basic vertical stacking
3. Implement `HStack` with basic horizontal stacking
4. Add `Spacer` component

### Phase 2: Size Computation
1. Add `preferredSize()` to existing components (Button, Text, etc.)
2. Implement auto-sizing logic in VStack/HStack
3. Handle min/max size constraints

### Phase 3: ScrollContainer Integration
1. VStack computes content height automatically
2. ScrollContainer reads content size from child
3. Scrollbar thumb size based on viewport/content ratio

### Phase 4: Demo and Validation
1. Create LayoutScene in ui-sandbox
2. Demonstrate common patterns
3. Performance testing with deep nesting

## Performance Considerations

### Layout Caching
- Layout recomputed only when:
  - Children added/removed
  - Container size changes
  - Child explicitly marks layout dirty

```cpp
class VStack : public Container {
    bool m_layoutDirty = true;

    void addChild(...) {
        Container::addChild(...);
        m_layoutDirty = true;
    }

    void layout(const Rect& bounds) {
        if (!m_layoutDirty && bounds == m_lastBounds) {
            return;  // Cache hit
        }
        // ... compute layout ...
        m_layoutDirty = false;
        m_lastBounds = bounds;
    }
};
```

### Avoiding Deep Recursion
- Layout is O(n) where n = total components
- No circular dependencies (parent→child only)
- Typical game UI has < 200 components, layout takes < 1ms

## Related Documentation

- [architecture.md](./architecture.md) - Component hierarchy
- [clipping.md](./clipping.md) - Clipping and scrolling
- [data-binding.md](./data-binding.md) - ViewModel pattern
