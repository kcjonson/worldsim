# UI Event System

This document describes the event propagation and hit testing system for the UI framework.

## Architecture

Based on the [Game Programming Patterns - Game Loop](https://gameprogrammingpatterns.com/game-loop.html) canonical pattern:

```
while (running) {
    processInput();    // No delta time - discrete events
    update(elapsed);   // Delta time here - advance simulation
    render();
}
```

**Key insight:** Input handling does NOT need delta time. It captures discrete events ("what happened?"). Delta time belongs in `update()` where simulation advances ("how much time passed?").

### Event Flow

```
app::App::tick(), step 5: the dispatch engine::UiBridge installs
    ↓
Create InputEvents from the frame's input (MouseMove, MouseDown, MouseUp, Scroll)
    ↓
overlay->handleInput(event), then scene->handleInput(event)
    ↓
Scene dispatches to UI components via handleEvent()
    ↓
Component hierarchy handles propagation via dispatchEvent()
```

---

## Core Types

### InputEvent

Located in `libs/ui/input/InputEvent.h`:

```cpp
struct InputEvent {
    enum class Type { MouseDown, MouseUp, MouseMove, Scroll };

    Type type;
    Foundation::Vec2 position;     // Screen coordinates
    foundation::MouseButton button;    // For MouseDown/MouseUp
    float scrollDelta;             // For Scroll
    int modifiers;                 // GLFW modifier flags

    // Propagation control
    bool consumed{false};

    void consume() { consumed = true; }
    [[nodiscard]] bool isConsumed() const { return consumed; }

    // Factory methods
    static InputEvent mouseDown(Vec2 pos, MouseButton btn, int mods = 0);
    static InputEvent mouseUp(Vec2 pos, MouseButton btn, int mods = 0);
    static InputEvent mouseMove(Vec2 pos);
    static InputEvent scroll(Vec2 pos, float delta);
};
```

**Note:** Key events are handled separately by `FocusManager`, not this event system.

### IComponent Interface

Located in `libs/ui/component/Component.h`:

```cpp
struct IComponent {
    virtual ~IComponent() = default;
    virtual void render() = 0;

    // Event handling - return true if event was consumed
    virtual bool handleEvent(InputEvent& event) { return false; }

    // Hit testing
    virtual bool containsPoint(Foundation::Vec2 point) const { return false; }

    short zIndex{0};
    bool visible{true};
};
```

### UiScene Interface

Located in `libs/engine/app/UiScene.h`, over salal's `app::Scene`:

```cpp
class UiScene : public app::Scene {
    // ... app::Scene's lifecycle: onEnter(App&), update(const FrameContext&), render(float), ...

    // Input event handling - receives events from the bridge
    virtual bool handleInput(UI::InputEvent& event) { return false; }
    virtual std::vector<const UI::IComponent*> getUiRoots() const { return {}; }
};
```

---

## Dispatch Model

### The Bridge Creates Events

`engine::UiBridge` (`libs/engine/app/UiBridge.cpp`) is the App's dispatch, so it runs once a
frame, after the frame's input is read and before scene changes and update. Unless the App is
paused it builds the events from `app.input().frame()` and sends each through the overlays,
last added first, then the top scene, stopping at the first that consumes it:

- a `MouseMove` at the frame's pointer every frame, so hover holds under a still cursor;
- `MouseDown` for a Left or Right press since the last frame, then `MouseUp` for a release;
- a `Scroll` with the last wheel delta, sent every frame until something consumes it. GameScene
  zooms the camera with what's left (`UiBridge::consumeScroll`), so a scroll a list takes doesn't
  zoom too.

Modifiers come from the Shift, Control and Alt keys held. Keys and characters don't become
InputEvents: they go to the `FocusManager`, paused or not (see focus-management.md).

### Scene Dispatches to Components

Scenes override `handleInput()` to dispatch to their UI:

```cpp
bool MyScene::handleInput(UI::InputEvent& event) override {
    // Dispatch to components (reverse order for z-ordering)
    for (auto it = components.rbegin(); it != components.rend(); ++it) {
        if ((*it)->handleEvent(event)) {
            return true;
        }
    }
    return false;
}
```

### Container Dispatches to Children

The `Container` class (and `Component` base) provide `dispatchEvent()`:

```cpp
bool Container::handleEvent(InputEvent& event) override {
    return dispatchEvent(event);  // Dispatch to children in z-order
}

bool Component::dispatchEvent(InputEvent& event) {
    // Sort children by z-index if needed
    // Dispatch in reverse order (highest z-index first)
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (!(*it)->visible) continue;
        if ((*it)->handleEvent(event)) return true;
        if (event.isConsumed()) return true;
    }
    return false;
}
```

### Leaf Components Handle Events

Interactive components like Button implement `handleEvent()`:

```cpp
bool Button::handleEvent(InputEvent& event) {
    if (disabled || !visible) return false;

    switch (event.type) {
        case InputEvent::Type::MouseMove:
            hovered = containsPoint(event.position);
            break;

        case InputEvent::Type::MouseDown:
            if (containsPoint(event.position)) {
                pressed = true;
                event.consume();
                return true;
            }
            break;

        case InputEvent::Type::MouseUp:
            if (pressed && containsPoint(event.position)) {
                if (onClick) onClick();
                pressed = false;
                event.consume();
                return true;
            }
            pressed = false;
            break;
    }
    return false;
}
```

---

## Design Decisions

### Why No Delta Time in handleInput()

Input events are discrete - something happened or it didn't. No time component needed.

- `handleInput(InputEvent&)` - "mouse clicked at position X"
- `update(const app::FrameContext&)` - "advance world by frame.dt seconds"

For continuous game input (WASD camera movement), read the frame's snapshot in `update`:

```cpp
void GameScene::update(const app::FrameContext& frame) {
    if (engine::key(frame.input, foundation::Key::W).down) {
        camera.move(speed * static_cast<float>(frame.dt));  // Delta time belongs here
    }
}
```

### Why Scenes Have handleInput() Not handleEvent()

Consistency. Everything that processes input calls it `handleInput`:
- `UiScene::handleInput(InputEvent&)` - scene receives input
- `IComponent::handleEvent(InputEvent&)` - component handles event

The distinction: scenes *receive* input from the bridge, components *handle* events dispatched by scenes.

### Why Single Top-Down Pass (No Bubbling)

Unlike HTML DOM's capture/bubble phases, we use a single top-down dispatch:

1. Sort by z-index (highest first)
2. Hit test each component
3. Call `handleEvent()` until one consumes
4. Stop propagation

Simpler because:
- Game UI is relatively flat, not deeply nested
- No default browser actions to prevent
- Z-index sorting gives natural layering

---

## Debug Integration (Future)

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
        }
    ]
}
```

---

## Performance

For N visible components:
- **Current**: O(N) bounds checks per event
- **Future**: Spatial partitioning (quadtree) for O(log N)

O(N) is acceptable for now. Game UI typically has < 100 visible components.

---

## Related Documentation

- [Focus Management](./focus-management.md) - Keyboard focus and Tab navigation
- [Component Architecture](./component-architecture.md) - Component base classes
