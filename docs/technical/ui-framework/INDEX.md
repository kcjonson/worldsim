# UI Framework - Technical Documentation

Created: 2025-10-26
Status: Research & Design Phase

## Overview

This directory contains technical documentation for the UI framework in world-sim. The UI framework provides components for game menus, forms, HUD elements, and debug tools using a modern C++20 API with designated initializers.

**Key Design Goals:**
- Modern declarative API (`.position = value` style)
- Scrollable containers with correct clipping and performance
- Crisp text rendering (SDF fonts matching vector aesthetic)
- Testability via HTTP inspector (scene graph JSON export)
- Integration with custom OpenGL renderer

## Documentation

### Library Selection

**[ui-architecture-fundamentals.md](./ui-architecture-fundamentals.md)** - **START HERE**
- Immediate vs Retained mode explained for web developers
- What a scene graph actually is (React's virtual DOM equivalent)
- What production games actually use
- RmlUI deep dive (HTML/CSS for games)
- **Read this first to understand the landscape**

**[library-options.md](./library-options.md)** - Detailed library comparison
- Dear ImGui, Nuklear, RmlUI, NanoGUI, MyGUI, Custom
- Each with code examples, facade API, pros/cons
- Includes learning-project framing (less relevant for production)

**[integration-analysis.md](./integration-analysis.md)** - Technical integration details
- OpenGL rendering architecture (who draws what?)
- Transitive dependencies breakdown
- Rendering conflict analysis
- Backend implementation examples

**[library-isolation-strategy.md](./library-isolation-strategy.md)** - **Critical before adopting RmlUI**
- How to isolate RmlUI from your codebase (interface pattern)
- What CAN'T be isolated (.rml files, data binding)
- Hidden constraints and conflicts with project goals
- Migration risk assessment
- **Read this before committing to any library**

**[rendering-boundaries.md](./rendering-boundaries.md)** - **Where does RmlUI end and custom rendering begin?**
- Usage boundaries (RmlUI for screen-space panels, custom for world)
- The "two APIs for rectangles" problem and solution
- Unified primitive rendering layer
- World-space vs screen-space UI
- Complete render loop architecture
- **NOTE**: Contains original analysis. See colonysim-integration-architecture.md for actual implementation plan.

### Colonysim Integration (CURRENT PRIORITY)

**[colonysim-integration-architecture.md](./colonysim-integration-architecture.md)** - **MUST READ** - Complete integration strategy
- Four-layer rendering architecture (Components → Primitives → BatchRenderer → OpenGL)
- Colonysim pattern analysis (shared_ptr in vectors)
- Worldsim research pattern (value semantics)
- **Pragmatic hybrid decision** - Start with shared_ptr, optimize later if needed
- InputManager pattern (instance-based, not singleton)
- Complete implementation plan and file organization
- What we're porting vs what we're NOT doing
- Adaptation examples (VectorGraphics → Primitives API)

**Related Research**:
- [Component Storage Patterns](/docs/research/component-storage-patterns.md) - Deep dive on memory patterns
  - Pattern A (shared_ptr) vs Pattern B (value semantics) vs Pattern C (hybrid)
  - Performance analysis and benchmarks from industry
  - Why we chose Pattern A initially for UI components
  - Future optimization paths

### Core Architecture

**[primitive-rendering-api.md](./primitive-rendering-api.md)** - Unified foundation for all 2D drawing
- DrawRect, DrawText, DrawTexture API
- Used by RmlUI backend, game rendering, and world-space UI
- Batching strategy and performance targets
- Integration with existing renderer
- Clipping and transform stacks
- **Solves the "two different APIs" problem**

**[sdf-rendering.md](./sdf-rendering.md)** - **GPU-Based SDF Rendering for UI Primitives** (NEW)
- Signed Distance Field approach for rectangle borders and rounded corners
- 5x geometry reduction (4 vertices vs 20 per bordered rect)
- Fragment shader implementation with perfect anti-aliasing
- Border positioning modes (Inside/Center/Outside)
- Performance analysis: 3x faster than CPU tessellation
- Complete implementation guide with testing strategy
- **Critical for modern UI aesthetics and performance**

**[rmlui-integration-architecture.md](./rmlui-integration-architecture.md)** - Complete RmlUI integration design
- Isolation layer (IUISystem, IDocument, IElement interfaces)
- RmlUI adapter implementation (hidden from game code)
- Renderer backend (RmlUI → Primitives API)
- Usage patterns (XML, programmatic, hybrid)
- Integration with game loop
- Testing and migration strategy

### Implementation Guides

**[opengl-rmlui-implementation-guide.md](./opengl-rmlui-implementation-guide.md)** - **Production-ready OpenGL + RmlUI setup**
- Based on RmlUI 6.0 official documentation and GL3 reference backend
- Complete rendering architecture and coordinate system handling
- RmlUI backend implementation (RenderInterface, geometry compilation, textures)
- Primitive API OpenGL implementation with batching
- State management (backup/restore) - CRITICAL for integration
- Integration patterns (main loop, initialization, input injection)
- Common pitfalls and troubleshooting
- Performance targets and profiling strategy
- **Read this to implement the rendering layer**

**text-rendering.md** (to be created if needed)
- SDF font integration with RmlUI
- Custom font backend using msdfgen
- Font atlas generation and management

## Quick Reference

### Desired API Style

Based on previous project experience:

```cpp
// Primitive shapes
auto rect = std::make_shared<UI::Rectangle>(UI::Rectangle::Args{
    .position = glm::vec2(300.0f, 300.0f),
    .size = glm::vec2(100.0f, 100.0f),
    .style = UI::Style{
        .backgroundColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.3f),
        .borderColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
        .borderWidth = 2.0f,
        .cornerRadius = 20.0f
    },
    .zIndex = 11.0f
});

// UI components
auto textInput = std::make_shared<UI::TextInput>(UI::TextInput::Args{
    .label = "Username:",
    .placeholder = "Enter username...",
    .position = glm::vec2(500.0f, 80.0f),
    .size = glm::vec2(200.0f, 30.0f),
    .zIndex = 30.0f
});

// Scrollable containers
auto scrollView = std::make_shared<UI::ScrollContainer>(UI::ScrollContainer::Args{
    .position = glm::vec2(100, 100),
    .size = glm::vec2(400, 600),
    .contentSize = glm::vec2(400, 2000), // Larger than viewport
    .enableScissor = true,
    .style = UI::Style{
        .overflow = UI::Overflow::ScrollY
    }
});

for (int i = 0; i < 100; i++) {
    auto item = std::make_shared<UI::Text>(UI::Text::Args{
        .text = "Item " + std::to_string(i),
        .position = glm::vec2(10, i * 25)
    });
    scrollView->addChild(item);
}
```

## Implementation Approaches

### Option A: Custom Implementation

**Pros:**
- Perfect API match from day one
- Deep learning experience
- Aligns with "roll our own" philosophy
- Full control

**Cons:**
- 4-6 weeks implementation time
- Ongoing maintenance burden
- Need to handle edge cases

**Best for:** Learning C++/OpenGL, long-term project control

### Option B: Wrap Existing Library

**Pros:**
- Faster to get running
- Battle-tested scrolling/clipping
- Community support

**Cons:**
- Facade layer adds complexity
- API impedance mismatch
- Less learning experience

**Best for:** Shipping game quickly, focus on gameplay

### Option C: Hybrid Approach

**Immediate:** Use ImGui for debug tools (HTTP inspector, metrics)
**Long-term:** Build custom game UI incrementally

**Best for:** Pragmatic balance of speed and learning

## Common UI Patterns

### Main Menu
```cpp
auto menu = std::make_shared<UI::Container>(UI::Container::Args{
    .size = glm::vec2(800, 600),
    .layout = UI::Layout::Vertical
});

menu->addChild(createButton("New Game", &OnNewGame));
menu->addChild(createButton("Load Game", &OnLoadGame));
menu->addChild(createButton("Settings", &OnSettings));
menu->addChild(createButton("Quit", &OnQuit));
```

### Form with Validation
```cpp
auto form = std::make_shared<UI::Form>();

auto username = form->addTextInput("Username", {
    .required = true,
    .minLength = 3,
    .maxLength = 20
});

auto password = form->addTextInput("Password", {
    .type = UI::TextInput::Type::Password,
    .required = true,
    .minLength = 8
});

auto submit = form->addButton("Submit", [&]() {
    if (form->validate()) {
        SubmitLogin(username->value(), password->value());
    }
});
```

### Game HUD
```cpp
auto hud = std::make_shared<UI::Container>(UI::Container::Args{
    .layout = UI::Layout::Absolute  // Manual positioning for game HUD
});

// Health bar
auto healthBar = std::make_shared<UI::ProgressBar>(UI::ProgressBar::Args{
    .position = glm::vec2(10, 10),
    .size = glm::vec2(200, 20),
    .value = 0.75f,  // 75% health
    .fillColor = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f)
});

// Minimap
auto minimap = std::make_shared<UI::Minimap>(UI::Minimap::Args{
    .position = glm::vec2(screenWidth - 210, 10),
    .size = glm::vec2(200, 200)
});

hud->addChild(healthBar);
hud->addChild(minimap);
```

## Integration with Observability

UI framework integrates with HTTP inspector (port 8081 for ui-sandbox):

**Scene Graph Export** (JSON):
```json
{
  "type": "Container",
  "id": "main_menu",
  "position": {"x": 100, "y": 100},
  "size": {"width": 800, "height": 600},
  "children": [
    {
      "type": "Button",
      "id": "new_game_btn",
      "label": "New Game",
      "position": {"x": 300, "y": 200},
      "size": {"width": 200, "height": 60}
    }
  ]
}
```

**F3 Hover Inspection**:
- Visual layer stack (what's rendered, z-order)
- Component hierarchy (parent/child tree)
- Live property values

See [/docs/technical/observability/ui-inspection.md](../observability/ui-inspection.md) for complete details.

## Related Documentation

### Design Documents (Game Requirements)
- [UI Art Style](/docs/design/ui-art-style.md) - "High tech cowboy" aesthetic

### Technical Implementation
- [Observability System](/docs/technical/observability/INDEX.md) - UI inspection and testability
- [Renderer Architecture](/docs/technical/renderer-architecture.md) - OpenGL integration (when created)
- [Vector Graphics](/docs/technical/vector-graphics/INDEX.md) - SVG rendering system

### Project Organization
- [Monorepo Structure](/docs/technical/monorepo-structure.md) - `libs/ui/` in dependency hierarchy
- [C++ Coding Standards](/docs/technical/cpp-coding-standards.md) - Style guide

## Next Steps

**Architecture phase complete. Ready for implementation:**

1. **Implement Primitive Rendering API** (`libs/renderer/`)
   - BatchAccumulator for geometry batching
   - OpenGL shader setup (vertex + fragment shaders)
   - State management and batching triggers

2. **Implement RmlUI Backend** (`libs/ui/src/rmlui/`)
   - RmlUIBackend class implementing Rml::RenderInterface
   - Geometry compilation (VAO/VBO/IBO)
   - Texture loading and management
   - State backup/restore (critical!)

3. **Integration Testing**
   - Load simple RML document (colored rectangles)
   - Test scissor regions (scrolling containers)
   - Test transforms (CSS transforms)
   - Validate against RmlUI visual test suite

4. **Performance Profiling**
   - Measure render time per frame
   - Count draw calls and batching effectiveness
   - Optimize if needed

See [opengl-rmlui-implementation-guide.md](./opengl-rmlui-implementation-guide.md) for complete implementation guidance.

## Revision History

- 2025-10-26: Initial UI framework documentation structure created
- 2025-10-26: Library options analysis completed
- 2025-10-26: OpenGL + RmlUI implementation guide created (production-ready)
