# UI Event System

This document describes the planned event propagation and hit testing system for the UI framework, inspired by HTML DOM events but adapted for our game's needs.

## Current State

The UI framework currently lacks a unified event system. Input handling is done through per-component polling of `InputManager`, which causes several issues:

1. **No event consumption** - When a UI element handles a click, underlying elements may also receive it
2. **Manual bounds checking** - Each container must manually check if clicks are within child bounds
3. **No hierarchical propagation** - Events don't flow through the component tree
4. **No debugging tools** - Can't inspect what's under a click point

### Current QUICKFIX Workarounds

The following files contain temporary workarounds that should be removed when this system is implemented:

| File | What to Remove |
|------|----------------|
| `libs/ui/component/Component.h` | N/A (no changes yet) |
| `apps/world-sim/components/GameUI.cpp:104-121` | `isPointOverUI()` method |
| `apps/world-sim/components/GameOverlay.h:42-46` | `isPointOverUI()` declaration |
| `apps/world-sim/components/GameOverlay.cpp:114-122` | `isPointOverUI()` implementation |
| `apps/world-sim/components/ZoomControl.h:38-39` | `isPointOver()` declaration |
| `apps/world-sim/components/ZoomControl.cpp:122-129` | `isPointOver()` implementation |

These methods implement manual point-in-bounds checking that will be replaced by the proper event consumption system described below.

---

## Design Goals

1. **Event consumption** - Components can stop events from propagating to elements below
2. **Efficient hit testing** - O(visible components) traversal with early termination
3. **Debug introspection** - Full layer stack available via HTTP for external debug app
4. **Z-index aware** - Higher z-index elements receive events first

---

## Core Concepts

### InputEvent

An `InputEvent` wraps raw input data with propagation control:

```cpp
struct InputEvent {
    enum class Type { MouseDown, MouseUp, MouseMove, KeyDown, KeyUp, Scroll };

    Type type;
    Foundation::Vec2 position;     // Screen coordinates
    engine::MouseButton button;    // For mouse events
    int keyCode;                   // For key events
    float scrollDelta;             // For scroll events

    // Propagation control
    bool consumed{false};

    void consume() { consumed = true; }
    [[nodiscard]] bool isConsumed() const { return consumed; }
};
```

### HitTestResult

When debugging, we want to know ALL components under a point, not just the topmost:

```cpp
struct HitTestEntry {
    std::string componentId;       // e.g., "btn_zoom_in"
    std::string componentType;     // e.g., "Button"
    short zIndex;
    Foundation::Rect bounds;
    bool wouldConsume;             // Would this component consume a click?
};

struct HitTestResult {
    std::vector<HitTestEntry> layers;  // Sorted by z-index, highest first
    Foundation::Vec2 testPoint;
};
```

---

## Event Flow

### Dispatch Model (Simplified)

Unlike HTML's capture/bubble phases, we use a single top-down dispatch:

1. **Sort by z-index** - All visible components sorted highest-first
2. **Hit test** - Filter to components containing the event point
3. **Dispatch** - Iterate through sorted list, calling `handleEvent()`
4. **Short-circuit** - Stop when `event.isConsumed()` returns true

```cpp
// In Component or Scene
bool dispatchEvent(InputEvent& event) {
    // Get all components under the event point, sorted by z-index (highest first)
    auto hitComponents = hitTestPoint(event.position);

    for (auto* component : hitComponents) {
        component->handleEvent(event);
        if (event.isConsumed()) {
            return true;  // Event was handled
        }
    }
    return false;  // Event reached bottom without being consumed
}
```

### Component Interface

```cpp
struct IComponent {
    virtual ~IComponent() = default;
    virtual void render() = 0;

    // Event handling (return true to indicate consumption)
    virtual bool handleEvent(InputEvent& event) { return false; }

    // Hit testing
    virtual bool containsPoint(Foundation::Vec2 point) const { return false; }
    virtual void collectHitTestEntries(Foundation::Vec2 point,
                                       std::vector<HitTestEntry>& outEntries) const {}

    short zIndex{0};
    bool visible{true};
    std::string id;
};
```

---

## Debug Introspection via HTTP

The debug layer stack is exposed through the existing Developer Server HTTP API, **not** a debug hotkey. This keeps debugging in the external Developer Client app.

### Endpoint: `GET /api/ui/hit-test?x={x}&y={y}`

Returns the full layer stack under a screen coordinate:

```json
{
    "testPoint": {"x": 150, "y": 200},
    "layers": [
        {
            "id": "btn_zoom_in",
            "type": "Button",
            "zIndex": 100,
            "bounds": {"x": 140, "y": 190, "width": 28, "height": 28},
            "wouldConsume": true
        },
        {
            "id": "game_overlay",
            "type": "GameOverlay",
            "zIndex": 50,
            "bounds": {"x": 0, "y": 0, "width": 800, "height": 600},
            "wouldConsume": false
        },
        {
            "id": "game_scene",
            "type": "GameScene",
            "zIndex": 0,
            "bounds": {"x": 0, "y": 0, "width": 800, "height": 600},
            "wouldConsume": true
        }
    ]
}
```

### Event Stream: Click Events

On each click, the full hit test result is sent via the existing SSE event stream:

```json
{
    "event": "ui:click",
    "data": {
        "position": {"x": 150, "y": 200},
        "button": "left",
        "consumed": true,
        "consumedBy": "btn_zoom_in",
        "layerStack": [/* same as hit-test response */]
    }
}
```

This allows the Developer Client to show real-time click debugging without polling.

---

## Implementation Plan

### Phase 1: Event Infrastructure

1. Create `InputEvent` struct in `libs/ui/input/InputEvent.h`
2. Add `handleEvent(InputEvent&)` to `IComponent` interface
3. Create `HitTestResult` and related types
4. Add `containsPoint()` default implementation using bounds

### Phase 2: Component Updates

1. Update `Button` to use `handleEvent()` and call `consume()` on click
2. Update `TextInput` similarly
3. Add bounds tracking to components that need hit testing

### Phase 3: Dispatch System

1. Create `EventDispatcher` class (or add to existing manager)
2. Implement z-index sorted dispatch
3. Wire up to `InputManager` in main loop

### Phase 4: Debug Integration

1. Add `/api/ui/hit-test` endpoint to Developer Server
2. Add click events to SSE stream
3. Update Developer Client to display layer stack

### Phase 5: Cleanup

1. Remove all QUICKFIX methods listed above
2. Migrate manual `handleInput()` implementations to `handleEvent()`
3. Update documentation

---

## Comparison with HTML DOM

| Feature | HTML DOM | Our System |
|---------|----------|------------|
| Event phases | Capture → Target → Bubble | Single top-down pass |
| Stop propagation | `stopPropagation()` | `event.consume()` |
| Prevent default | `preventDefault()` | Not needed (no defaults) |
| Event delegation | Common pattern | Native via z-index sorting |
| Debug tools | Browser DevTools | External Developer Client via HTTP |

We simplify the model since:
- No capture phase needed (game UI is flat, not deeply nested)
- No default actions to prevent
- Z-index sorting gives us natural layering

---

## Performance Considerations

### Hit Testing Cost

For N visible components:
- **Naive**: O(N) bounds checks per event
- **Optimized**: Spatial partitioning (quadtree) for O(log N)

For MVP, O(N) is acceptable. Game UI typically has < 100 visible components at once.

### Memory

- `InputEvent`: ~40 bytes (stack allocated, passed by reference)
- `HitTestResult`: Heap allocated only for debug endpoint, not normal dispatch

---

## Related Documentation

- [UI Inspection](../observability/ui-inspection.md) - Developer Client UI debugging
- [Developer Server](../observability/developer-server.md) - HTTP/SSE API
