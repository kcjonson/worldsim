# Colonysim → Worldsim UI Integration Architecture

Created: 2025-10-29
Status: **DECIDED** - Pragmatic Hybrid Approach

## Executive Summary

After comprehensive analysis of both codebases and worldsim's architecture research, we have identified the complete integration strategy for porting colonysim's UI components to worldsim. This document captures the research findings, architectural decisions, and implementation plan.

**Core Decision**: Port colonysim's Layer 4 components using a pragmatic hybrid approach:
- Start with colonysim's `shared_ptr` pattern (proven, functional)
- Adapt components to use worldsim's Primitives API (Layer 3)
- Optimize to research-recommended patterns later if profiling shows need

---

## The Four-Layer Rendering Architecture

### Complete Stack

Based on `/docs/research/modern_rendering_architecture.md` and worldsim's existing architecture:

```
┌─────────────────────────────────────────────────────────┐
│ LAYER 4: Persistent Components (MISSING in worldsim)   │
│                                                          │
│ - Layer hierarchy (parent-child scene graph)            │
│ - Shape classes (Rectangle, Circle, Text, etc.)         │
│ - UI Components (Button, TextInput, etc.)               │
│ - Style system (Base + Border composition)              │
│                                                          │
│ Storage: std::vector<shared_ptr<Layer>> (colonysim)     │
│ Research Ideal: std::vector<ConcreteType> (no ptr)      │
│                                                          │
│ Each component's Render() calls ↓                       │
└──────────────────┬──────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────┐
│ LAYER 3: Primitives API (EXISTS in worldsim)           │
│                                                          │
│ Immediate-mode API:                                     │
│   - DrawRect({ .bounds = ..., .style = ... })           │
│   - DrawText({ .text = ..., .font = ..., ... })         │
│   - DrawCircle({ .center = ..., .radius = ... })        │
│                                                          │
│ Called every frame from Layer 4 components              │
│ C++20 designated initializers                           │
│                                                          │
│ Documented: /docs/technical/ui-framework/              │
│             primitive-rendering-api.md                   │
│                                                          │
│ Accumulates in BatchRenderer ↓                          │
└──────────────────┬──────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────┐
│ LAYER 2: BatchRenderer (EXISTS in worldsim)            │
│                                                          │
│ "Command Buffer" pattern from research:                 │
│   - Accumulates vertices/indices in CPU buffers         │
│   - Sorts by state (shader, texture, blend mode)        │
│   - Flushes to GPU with minimal draw calls              │
│   - Statistics tracking (draw calls, vertices)          │
│                                                          │
│ Implementation: libs/renderer/primitives/batch_renderer │
│                                                          │
│ Submits to OpenGL ↓                                     │
└──────────────────┬──────────────────────────────────────┘
                   │
┌──────────────────▼──────────────────────────────────────┐
│ LAYER 1: OpenGL 3.3+                                    │
│                                                          │
│ Via GLEW                                                 │
└─────────────────────────────────────────────────────────┘
```

### What Colonysim Provides

**Layer 4 Components** - The missing piece worldsim needs:

| Component | Purpose | Storage Pattern |
|-----------|---------|-----------------|
| Layer | Parent-child hierarchy, z-ordering | `std::vector<shared_ptr<Layer>> children` |
| Shape | Base class for visual elements | `shared_ptr<Shape>` (inherits from Layer) |
| Rectangle/Circle/Line | Concrete shape classes | Stored as `shared_ptr<Layer>` polymorphically |
| Text | Text rendering shape | Uses FontRenderer (already ported) |
| Button | Interactive button component | Contains `shared_ptr<Rectangle>`, `shared_ptr<Text>` |
| TextInput | Text input component | Contains `shared_ptr<Rectangle>`, etc. |
| Style | Color, border, corner radius | Plain structs (no pointers) |
| CoordinateSystem | DPI handling, percentage layouts | Utility functions |

**What Colonysim Does Differently**:

| Aspect | Colonysim | Worldsim Exists | Worldsim Research Ideal |
|--------|-----------|-----------------|-------------------------|
| Graphics Loader | GLAD | GLEW ✅ | GLEW ✅ |
| Layer 4 | `shared_ptr` in vectors | ❌ Missing | `std::vector<T>` (value semantics) |
| Layer 3 API | VectorGraphics singleton | Primitives namespace ✅ | Primitives ✅ |
| Layer 2 | VectorGraphics batching | BatchRenderer ✅ | BatchRenderer ✅ |
| Input | Direct GLFW calls | Scene::HandleInput ✅ | InputManager instance |

---

## Memory Pattern Analysis

### Colonysim's Pattern (Object-Oriented)

**Storage**:
```cpp
// Layer.h
class Layer {
protected:
    std::vector<std::shared_ptr<Layer>> children;  // Polymorphic storage
};

// Scene stores layers as shared_ptr
std::shared_ptr<Layer> buttonLayer = std::make_shared<Layer>();
std::shared_ptr<Button> button = std::make_shared<Button>();
buttonLayer->addItem(button);  // Adds to children vector
```

**Pros**:
- ✅ Works (proven in production)
- ✅ Familiar OOP pattern
- ✅ Easy polymorphism via virtual functions
- ✅ Uniform container type

**Cons** (from worldsim research):
- ❌ Heap allocation overhead (every component needs `new`)
- ❌ Cache-unfriendly (pointer chasing, scattered memory)
- ❌ Ref-counting cost (atomic operations in shared_ptr)
- ❌ Indirection cost (extra dereference per access)

### Worldsim Research Pattern (Data-Oriented)

From `/docs/research/modern_rendering_architecture.md`:

**Storage**:
```cpp
// Recommended approach (lines 82-122)
struct RenderSystem {
    std::vector<RectangleShape> rectangles_;  // Value semantics
    std::vector<CircleShape> circles_;         // Contiguous memory
    std::vector<PathShape> paths_;

    // No shared_ptr! Direct storage in vectors
};
```

**Pros** (from research):
- ✅ Cache-friendly (contiguous memory, no pointer chasing)
- ✅ No heap allocation per object
- ✅ "50x improvements over pointer-based approaches" (line 125)
- ✅ Better performance at scale

**Cons**:
- ❌ Requires full rewrite of colonysim components
- ❌ More complex polymorphism (`std::variant` or type erasure)
- ❌ Unproven in this codebase

### Research Conclusion (Line 220)

> "start with simple persistent shape objects in contiguous arrays, build command buffers for rendering, measure your specific performance characteristics, then optimize the identified bottlenecks"

**Translation**: Start with what works, optimize later based on profiling data.

---

## Architectural Decision: Pragmatic Hybrid

### Decision

**Port colonysim's Layer 4 using shared_ptr pattern initially, optimize later if needed**.

### Rationale

**Why Start With shared_ptr**:
1. **Proven functional** - Colonysim works in production
2. **Faster to implement** - Less refactoring required
3. **Enables complete system quickly** - Get all components working
4. **Can measure actual performance** - Profile before optimizing
5. **Research endorses pragmatic approach** - "start simple, measure, optimize"

**Why Easy to Refactor Later**:
```cpp
// Current: Component calls Primitives
void Rectangle::Render() {
    Primitives::DrawRect({
        .bounds = GetBounds(),
        .style = m_style
    });
}

// If we optimize storage from shared_ptr to value semantics later,
// Render() logic doesn't change! Only storage changes.
```

**Render methods are already abstracted** - they only call Primitives API.

### What We're Porting

**From Colonysim** (in order):
1. **InputManager** - Instance-based pattern (already matches research)
2. **Style system** - Plain structs (no pointers)
3. **Layer hierarchy** - With `shared_ptr<Layer>` initially
4. **Shape classes** - Rectangle, Circle, Line, Text
5. **UI Components** - Button, TextInput
6. **CoordinateSystem** - Utility functions

**Adaptation Required**:
```cpp
// Colonysim's Rectangle::render()
void render(bool batched = false) override {
    VectorGraphics::getInstance().drawRectangle(/* ... */);
}

// Becomes worldsim's Rectangle::Render()
void Render() {
    Primitives::DrawRect({
        .bounds = {m_position.x, m_position.y, m_size.x, m_size.y},
        .style = {
            .fill = m_style.color,
            .borderColor = m_style.borderColor,
            .borderWidth = m_style.borderWidth,
            .cornerRadius = m_style.cornerRadius
        }
    });
}
```

**Key Change**: VectorGraphics → Primitives API

---

## Integration Points

### 1. Input Handling

**Colonysim Pattern** (instance-based, NOT singleton):
```cpp
// InputManager.h
class InputManager {
public:
    InputManager(GLFWwindow* window, Camera& camera, GameState& gameState);
    ~InputManager();

    void Update(float deltaTime);
    bool IsKeyPressed(int key) const;
    bool IsMouseButtonPressed(int button) const;
    glm::vec2 GetMousePosition() const;

private:
    static InputManager* s_instance;  // For GLFW callback routing only
    GLFWwindow* m_window;
    // ... state
};

// Constructor stores instance for callbacks
InputManager::InputManager(/* ... */) {
    s_instance = this;
    glfwSetKeyCallback(window, KeyCallback);
    // Static callbacks route to instance methods
}
```

**This is the correct pattern** - matches worldsim philosophy:
- Not a singleton (multiple instances possible)
- Dependency injection via constructor
- Static pointer only for GLFW callback routing (required by GLFW's C API)
- Clear lifecycle (constructor sets, destructor clears)

**Integration**:
```cpp
// libs/engine/input/input_manager.h
// Port colonysim's pattern with minimal changes
```

### 2. Primitives API Integration

**Current worldsim Primitives API**:
```cpp
namespace Primitives {
    void DrawRect(const RectArgs& args);
    void DrawText(const TextArgs& args);
    void DrawLine(const LineArgs& args);
    // ... etc
}
```

**Colonysim components adapted to use**:
```cpp
// libs/ui/shapes/rectangle.h
class Rectangle : public Shape {
public:
    void Render() override {
        Primitives::DrawRect({
            .bounds = GetBounds(),
            .style = {.fill = m_style.color, .border = ...}
        });
    }
};
```

### 3. Layer Hierarchy

**Colonysim's Layer**:
```cpp
class Layer {
protected:
    std::vector<std::shared_ptr<Layer>> children;
    glm::vec2 position;
    float zIndex;
    bool dirty;  // For sort optimization

public:
    void addItem(std::shared_ptr<Layer> item);
    void removeItem(std::shared_ptr<Layer> item);
    void sortChildren();  // By z-index (only when dirty)
    virtual void render(bool batched = false);
    virtual void update(float deltaTime);
    virtual void handleInput(/* ... */);
};
```

**Port to worldsim**:
```cpp
// libs/ui/layer/layer.h
// Keep pattern initially, optimize later if needed
class Layer {
protected:
    std::vector<std::shared_ptr<Layer>> m_children;  // Worldsim naming (m_ prefix)
    glm::vec2 m_position;
    float m_zIndex;
    bool m_dirty;

public:
    void AddChild(std::shared_ptr<Layer> child);     // PascalCase per worldsim
    void RemoveChild(std::shared_ptr<Layer> child);
    void SortChildren();
    virtual void Render();
    virtual void Update(float deltaTime);
    virtual void HandleInput(const InputState& input);  // Use InputManager state
};
```

### 4. Style System

**Colonysim's Style** (plain structs, no pointers):
```cpp
struct Base {
    glm::vec4 color{1.0f};
    float opacity{1.0f};
};

struct Border {
    glm::vec4 borderColor{0.0f};
    float borderWidth{0.0f};
    BorderPosition borderPosition{BorderPosition::Inside};
    float cornerRadius{0.0f};
};

struct RectangleStyleParams : Base, Border {
    // Composition - inherits from Base and Border
};
```

**Port to worldsim**:
```cpp
// libs/renderer/styles/
// Keep pattern - this is already good (value semantics)
```

---

## Implementation Plan

### Phase 1: Port Components (Start Here)

**Epic 1: InputManager** (1-2 days)
- Create `libs/engine/input/` library
- Port from colonysim with minimal changes
- Keep instance-based pattern
- Adapt GLFW callbacks to worldsim conventions

**Epic 2: Style System** (1 day)
- Create `libs/renderer/styles/` library
- Port style structs (Base, Border, concrete types)
- No rendering logic - just data structures

**Epic 3: Layer System** (2-3 days)
- Create `libs/ui/layer/` library
- Port Layer class with `shared_ptr` storage
- Z-index sorting with dirty flag optimization
- Transform hierarchy support
- Update naming to worldsim conventions (PascalCase methods, m_ prefix)

**Epic 4: Shape System** (3-4 days)
- Create `libs/ui/shapes/` library
- Port Shape base class
- **CRITICAL**: Rewrite `render()` → `Render()` to call Primitives API
- Port Rectangle, Circle, Line shapes
- Adapt Text shape to use worldsim's FontRenderer

**Epic 5: UI Components** (3-4 days)
- Create `libs/ui/components/` library
- Port Button (state machine, onClick callbacks)
- Port TextInput (cursor, focus, text editing)
- Adapt input handling to use InputManager (not direct GLFW)

**Epic 6: CoordinateSystem** (1-2 days)
- Create `libs/renderer/coordinate_system/` library
- Port utility functions (percentWidth, percentHeight, etc.)
- DPI handling and projection matrix creation

### Phase 2: Profile and Measure

**Goal**: Understand actual performance characteristics

**Questions to answer**:
- How many UI elements render per frame?
- Is shared_ptr overhead measurable?
- Where are the real bottlenecks?
- Do we hit 60 FPS with typical UI?

**Tools**:
- Profiler (CPU sampling)
- Frame time measurements
- Draw call counting (already in BatchRenderer)

### Phase 3: Optimize If Needed

**Only if profiling shows bottlenecks**:

**Potential optimizations**:
- Replace `vector<shared_ptr<Layer>>` with value semantics
- Implement type-specific containers (`vector<Rectangle>`, `vector<Circle>`)
- Add Structure of Arrays for hot paths
- Implement object pooling for frequently created/destroyed components

**Research guidance** (line 218):
> "production engines universally adopted hybrid architectures because purity fails in practice"

---

## What We're NOT Doing

### ❌ Not Porting VectorGraphics

**Colonysim has**:
```cpp
VectorGraphics::getInstance().drawRectangle(/* ... */);
```

**Worldsim already has better**:
```cpp
Primitives::DrawRect(/* ... */);  // BatchRenderer underneath
```

VectorGraphics is colonysim's Layer 2+3 combined. Worldsim has these layers separated and designed per research. **Use worldsim's Primitives API instead.**

### ❌ Not Creating Singletons

**Colonysim's VectorGraphics is a singleton**. Worldsim research discourages this. InputManager appears to be a singleton but is actually instance-based (static pointer only for GLFW callbacks).

**Worldsim pattern**: Pass dependencies via constructor (dependency injection).

### ❌ Not Refactoring to Value Semantics Yet

**Wait for profiling data**. Research says:
- shared_ptr pattern: Known to work
- Value semantics: Better performance theoretically
- **Pragmatic approach**: Start with known working, optimize based on measurements

### ❌ Not Using ECS

**Colonysim uses OOP hierarchy**. Worldsim research mentions ECS as advanced optimization for large-scale performance (Unity DOTS, Unreal Mass Entity).

**Decision**: Start with OOP (Layer hierarchy), migrate to ECS only if profiling shows need for extreme scale (10,000+ UI elements).

---

## Validation Criteria

### Phase 1 Complete When

- ✅ Can create Button with onClick handler
- ✅ Can render nested Layer hierarchy
- ✅ Input routing works through InputManager
- ✅ All rendering goes through Primitives API
- ✅ Batching works (minimal draw calls)
- ✅ Text rendering works (uses worldsim's FontRenderer)
- ✅ Styles apply correctly (borders, corners, colors)

### Performance Targets

**Baseline** (from primitive-rendering-api.md):
- 100 health bars: <0.1ms, 1-2 draw calls
- Resource HUD (10 counters): <0.05ms, 2-3 draw calls
- UI panel (100 elements): <0.3ms, 5-10 draw calls
- **Overall budget**: <1ms per frame for all primitive rendering

**With Layer 4 components**:
- Simple UI scene (10-20 components): 60 FPS
- Complex UI scene (100+ components): 60 FPS
- Nested layers (3-4 deep): No performance degradation

### Ready for Phase 2 When

- ✅ Basic UI demo scene renders correctly
- ✅ Performance baseline established
- ✅ Profiling data collected
- ✅ Bottlenecks identified (if any)

---

## File Organization

```
libs/
├── engine/
│   └── input/
│       ├── include/engine/input/
│       │   ├── input_manager.h
│       │   └── input_state.h
│       └── src/
│           └── input_manager.cpp
│
├── renderer/
│   ├── primitives/          [EXISTS - Layer 3]
│   │   ├── primitives.h     ✅
│   │   └── batch_renderer.* ✅
│   │
│   ├── styles/              [NEW - Port from colonysim]
│   │   ├── include/renderer/styles/
│   │   │   ├── base_style.h
│   │   │   ├── border_style.h
│   │   │   ├── rectangle_style.h
│   │   │   └── ...
│   │   └── src/
│   │       └── (minimal/no cpp files - mostly structs)
│   │
│   └── coordinate_system/   [NEW - Port from colonysim]
│       ├── include/renderer/coordinate_system/
│       │   └── coordinate_system.h
│       └── src/
│           └── coordinate_system.cpp
│
└── ui/
    ├── layer/               [NEW - Port from colonysim]
    │   ├── include/ui/layer/
    │   │   └── layer.h
    │   └── src/
    │       └── layer.cpp
    │
    ├── shapes/              [NEW - Adapt from colonysim]
    │   ├── include/ui/shapes/
    │   │   ├── shape.h
    │   │   ├── rectangle.h
    │   │   ├── circle.h
    │   │   ├── line.h
    │   │   └── text.h
    │   └── src/
    │       ├── rectangle.cpp  // Calls Primitives::DrawRect()
    │       ├── circle.cpp     // Calls Primitives::DrawCircle()
    │       └── ...
    │
    ├── components/          [NEW - Adapt from colonysim]
    │   ├── include/ui/components/
    │   │   ├── button.h
    │   │   └── text_input.h
    │   └── src/
    │       ├── button.cpp
    │       └── text_input.cpp
    │
    └── font/                [EXISTS - Already ported ✅]
        ├── include/ui/font/
        │   └── font_renderer.h
        └── src/
            └── font_renderer.cpp
```

---

## Key Differences: Colonysim vs Worldsim Conventions

**Adapt during port**:

| Aspect | Colonysim | Worldsim |
|--------|-----------|----------|
| Function names | `camelCase` | `PascalCase` |
| Member variables | `camelCase` | `m_prefix` |
| Method names | `render()`, `update()` | `Render()`, `Update()` |
| Parameter passing | References, mix | Const ref for large, value for small |
| Include guards | `#pragma once` ✅ | `#pragma once` ✅ |
| Designated initializers | Sometimes | Always (C++20 standard) |
| Graphics loader | GLAD | GLEW |

**Example adaptation**:
```cpp
// Colonysim
class Rectangle {
    glm::vec2 position;
    glm::vec2 size;
    void render() { /* ... */ }
};

// Worldsim
class Rectangle {
    glm::vec2 m_position;
    glm::vec2 m_size;
    void Render() { /* ... */ }
};
```

---

## Technical Constraints

### Graphics API

**Both use OpenGL 3.3+**, but different loaders:
- Colonysim: GLAD
- Worldsim: GLEW

**Impact**: Minimal - just initialization differences

**Solution**: FontRenderer port already proved GLAD→GLEW conversion works. Continue using GLEW.

### GLFW Input

**Colonysim**: Direct GLFW calls in components
```cpp
if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
    // Handle click
}
```

**Worldsim**: InputManager abstraction
```cpp
if (inputManager.IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
    // Handle click
}
```

**Solution**: Adapt components to use InputManager API.

---

## Risk Mitigation

### Risk: shared_ptr causes performance issues

**Mitigation**:
- Designed for easy refactoring (render() already calls Primitives)
- Can swap storage without changing render logic
- Profile before optimizing

### Risk: Too much work to port everything

**Mitigation**:
- Incremental approach (one library at a time)
- Each component testable independently
- Can stop at any point with partial port

### Risk: Architectural mismatch

**Mitigation**:
- Research validates both patterns work
- Colonysim is proven functional
- Pragmatic hybrid approach balances speed and quality

---

## References

### Worldsim Documentation

- `/docs/research/modern_rendering_architecture.md` - Industry research on rendering patterns
- `/docs/technical/ui-framework/primitive-rendering-api.md` - Layer 3 API design
- `/docs/technical/ui-framework/rendering-boundaries.md` - RmlUI vs custom rendering
- `/docs/technical/cpp-coding-standards.md` - Worldsim conventions

### Colonysim Source

- `/Volumes/Code/colonysim/src/Rendering/Layer.h` - Layer hierarchy
- `/Volumes/Code/colonysim/src/Rendering/Shapes/` - Shape implementations
- `/Volumes/Code/colonysim/src/Rendering/Components/` - UI components
- `/Volumes/Code/colonysim/src/Rendering/Styles/` - Style system
- `/Volumes/Code/colonysim/src/InputManager.h` - Input handling
- `/Volumes/Code/colonysim/src/CoordinateSystem.h` - Coordinate utilities

### Research Quotes

**On pragmatic approach** (line 220):
> "start with simple persistent shape objects in contiguous arrays, build command buffers for rendering, measure your specific performance characteristics, then optimize the identified bottlenecks"

**On hybrid architectures** (line 218):
> "production engines universally adopted hybrid architectures because purity fails in practice"

**On memory patterns** (line 125):
> "50x improvements over pointer-based approaches" [for value semantics]

**On retained objects** (line 3):
> "Modern production game engines overwhelmingly use retained-mode architectures with persistent object references"

---

## Revision History

- 2025-10-29: Initial architecture analysis and decision documentation
- 2025-10-29: Complete colonysim/worldsim compatibility analysis
- 2025-10-29: Pragmatic hybrid approach decided based on research
