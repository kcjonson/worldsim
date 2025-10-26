# UI Framework Library Options - Comparative Analysis

Created: 2025-10-26
Status: Research & Analysis

## Overview

This document analyzes UI framework options for world-sim, comparing existing libraries against a custom implementation. The goal is to provide an objective comparison to inform the library choice decision.

**Context**: We need a UI framework for:
- Main menu (buttons, text inputs, layout)
- Game HUD (health bars, inventory, minimap)
- Forms and dialogs
- Debug UI and tools

**Key Requirements**:
- Modern C++20 API with designated initializers (`.position = value` style)
- Scrollable containers that work correctly (clipping, culling, performance)
- Crisp text rendering (SDF fonts for vector aesthetic)
- Integration with custom OpenGL renderer
- Testability (HTTP inspector, scene graph export)

## Quick Reference Comparison

| Library | Mode | License | Language | API Style | Complexity | Scrolling |
|---------|------|---------|----------|-----------|------------|-----------|
| **Dear ImGui** | Immediate | MIT | C++ | Function calls | Low | Built-in |
| **Nuklear** | Immediate | MIT/Public | C | Function calls | Medium | Built-in |
| **RmlUI** | Retained | MIT | C++ | HTML/CSS | High | Built-in |
| **NanoGUI** | Retained | BSD | C++ | Builder | Medium | Built-in |
| **MyGUI** | Retained | MIT | C++ | XML config | High | Built-in |
| **Custom** | Retained | N/A | C++ | Your design | High | Build it |

## Library Deep Dives

---

### Option 1: Dear ImGui

**Architecture**: Immediate mode GUI
- Regenerates UI every frame from code
- No persistent state (stateless)
- Widget calls in render loop

**Example Code**:
```cpp
// Dear ImGui native API (in render loop)
void RenderUI() {
    ImGui::Begin("Main Menu");

    if (ImGui::Button("New Game", ImVec2(200, 60))) {
        StartNewGame();
    }

    ImGui::BeginChild("ScrollRegion", ImVec2(400, 600), true);
    for (int i = 0; i < 100; i++) {
        ImGui::Text("Item %d", i);
    }
    ImGui::EndChild();

    ImGui::End();
}
```

**Your Facade API** (wrapping ImGui):
```cpp
// What you want - needs facade layer
auto menu = std::make_shared<UI::Container>(UI::Container::Args{
    .position = glm::vec2(100, 100),
    .size = glm::vec2(800, 600),
    .title = "Main Menu"
});

auto button = std::make_shared<UI::Button>(UI::Button::Args{
    .label = "New Game",
    .size = glm::vec2(200, 60),
    .onClick = []() { StartNewGame(); }
});

menu->addChild(button);

// In render loop, your facade converts to ImGui calls
menu->render(); // Internally calls ImGui::Begin, ImGui::Button, etc.
```

**Integration Complexity**: **Medium**
- ImGui is immediate mode, your API is declarative
- Need facade to convert `.Args{}` to ImGui function calls
- Must call UI code every frame (can't just build once)
- State management awkward (ImGui manages input state, you manage your objects)

**Scrolling**:
- `ImGui::BeginChild()` with scroll flags
- Handles clipping automatically (uses scissor test internally)
- Virtual scrolling not built-in (renders all children)
- Performance good for <1000 items

**Text Rendering**:
- Built-in font atlas system (bitmap fonts)
- FreeType integration available
- No SDF fonts by default (would need custom integration)

**Pros**:
- Battle-tested (used in thousands of games/tools)
- Rich widget library out of the box
- Excellent documentation and community
- Debug/tools UI is its sweet spot
- Active development

**Cons**:
- Immediate mode doesn't match your declarative API naturally
- Facade layer adds complexity and indirection
- State management splits between ImGui and your objects
- Styling limited (not CSS-like)
- Less suitable for game UI (more for debug/editor UI)

**Best For**: Debug tools, editor UI, rapid prototyping

---

### Option 2: Nuklear

**Architecture**: Immediate mode, single-header library
- Similar to ImGui but C-based
- More minimal, less opinionated

**Example Code**:
```cpp
// Nuklear native API
if (nk_begin(ctx, "Main Menu", nk_rect(100, 100, 800, 600),
             NK_WINDOW_BORDER|NK_WINDOW_MOVABLE)) {

    nk_layout_row_static(ctx, 60, 200, 1);
    if (nk_button_label(ctx, "New Game")) {
        StartNewGame();
    }

    // Scrolling region
    nk_layout_row_dynamic(ctx, 600, 1);
    if (nk_group_begin(ctx, "Scroll", NK_WINDOW_BORDER)) {
        for (int i = 0; i < 100; i++) {
            nk_layout_row_dynamic(ctx, 25, 1);
            nk_label(ctx, "Item %d", NK_TEXT_LEFT);
        }
        nk_group_end(ctx);
    }
}
nk_end(ctx);
```

**Your Facade API**:
```cpp
// Similar to ImGui facade approach
auto menu = std::make_shared<UI::Container>(/* ... */);
// Facade converts to Nuklear calls in render()
```

**Integration Complexity**: **Medium-High**
- C library (need C++ wrappers for RAII)
- Similar facade needs as ImGui
- Manual memory management for context
- Less polished than ImGui

**Scrolling**:
- `nk_group_begin()` with scroll flags
- Automatic clipping
- No virtualization (renders all)

**Text Rendering**:
- Bring your own font backend
- Flexible but requires more setup

**Pros**:
- Single header file (~30k lines)
- Public domain/MIT license
- Smaller footprint than ImGui
- Flexible backend integration

**Cons**:
- C API is verbose and manual
- Less documentation than ImGui
- Smaller community
- Same immediate mode issues as ImGui
- More work to set up

**Best For**: Embedded UIs, minimal dependencies, C projects

---

### Option 3: RmlUI

**Architecture**: Retained mode, HTML/CSS-like
- Closest to web development model
- Document Object Model (DOM)
- CSS for styling, HTML-like markup

**Example Code**:
```cpp
// RmlUI uses HTML + CSS markup
// In RML file (HTML-like):
<body>
    <div id="menu" class="main-menu">
        <button id="new-game">New Game</button>
        <div class="scroll-container">
            <!-- Content -->
        </div>
    </div>
</body>

// In RCSS file (CSS-like):
.main-menu {
    position: absolute;
    top: 100px;
    left: 100px;
    width: 800px;
    height: 600px;
}

.scroll-container {
    overflow: auto;
    height: 600px;
}

// In C++:
Rml::ElementDocument* doc = context->LoadDocument("menu.rml");
doc->Show();

// Add event listener
auto button = doc->GetElementById("new-game");
button->AddEventListener("click", &OnNewGameClick);
```

**Your Facade API**:
```cpp
// RmlUI doesn't match your .Args{} style well
// Would need heavy wrapping or accept different paradigm
auto menu = UI::LoadDocument("menu.rml"); // Uses markup files
// OR build programmatically:
auto menu = doc->CreateElement("div");
menu->SetAttribute("class", "main-menu");
```

**Integration Complexity**: **High**
- Large library with many concepts (DOM, CSS, layout engine)
- Expects markup files (RML/RCSS) rather than C++ API
- Can build DOM programmatically but verbose
- Custom rendering backend required (well-documented)

**Scrolling**:
- Full CSS `overflow: auto` support
- Automatic clipping and scrollbars
- Layout reflow on content changes
- Virtual scrolling not built-in

**Text Rendering**:
- FreeType integration built-in
- Font faces, sizes, styles via CSS
- No SDF fonts by default

**Pros**:
- Most web-like (if you like HTML/CSS)
- Full layout engine (flexbox, grid)
- Mature and well-maintained
- Good documentation
- Handles complex UIs well

**Cons**:
- Very different from your `.Args{}` API
- Requires learning RML/RCSS markup
- Heavy (large library)
- Overkill for simple UIs
- C++ API verbose if not using markup

**Best For**: Complex game UIs, web developers comfortable with HTML/CSS

---

### Option 4: NanoGUI

**Architecture**: Retained mode, builder pattern
- Modern C++11 GUI library
- Declarative API (closer to your style)
- OpenGL/GLES backend

**Example Code**:
```cpp
// NanoGUI native API
auto window = new Window(screen, "Main Menu");
window->setPosition(Vector2i(100, 100));
window->setLayout(new BoxLayout(Orientation::Vertical, Alignment::Middle, 10, 10));

auto button = new Button(window, "New Game");
button->setCallback([]() { StartNewGame(); });

// VScrollPanel for scrolling
auto scroll = new VScrollPanel(window);
scroll->setFixedSize(Vector2i(400, 600));

auto container = new Widget(scroll);
container->setLayout(new BoxLayout(Orientation::Vertical));
for (int i = 0; i < 100; i++) {
    new Label(container, "Item " + std::to_string(i));
}
```

**Your Facade API**:
```cpp
// NanoGUI's API is already somewhat similar, but you could wrap it:
auto button = std::make_shared<UI::Button>(UI::Button::Args{
    .label = "New Game",
    .size = glm::vec2(200, 60),
    .onClick = []() { StartNewGame(); }
});

// Internally wraps nanogui::Button
button->m_impl = new nanogui::Button(parent, "New Game");
button->m_impl->setCallback(/* ... */);
```

**Integration Complexity**: **Medium**
- Designed for OpenGL integration
- C++ API more natural than ImGui
- Still need facade for `.Args{}` style
- Memory management (raw pointers vs smart pointers)

**Scrolling**:
- `VScrollPanel` widget built-in
- Automatic clipping via scissor test
- Manual layout (doesn't auto-size to content well)

**Text Rendering**:
- NanoVG-based rendering (includes fonts)
- Smooth text rendering
- Not SDF (uses stencil-based rendering)

**Pros**:
- Modern C++ design
- Good for simple UIs
- Nice visual style by default
- OpenGL integration straightforward
- BSD license

**Cons**:
- Smaller community than ImGui
- Limited widget library
- Less documentation
- Not actively developed (maintenance mode)
- Layout system basic

**Best For**: Simple game UIs, modern C++ projects

---

### Option 5: MyGUI

**Architecture**: Retained mode, game-focused
- Designed specifically for games
- XML-based layouts
- Skinning system

**Example Code**:
```cpp
// MyGUI uses XML layouts
// In layout.xml:
<MyGUI type="Layout">
    <Widget type="Window" name="MainMenu">
        <Property key="Position" value="100 100"/>
        <Property key="Size" value="800 600"/>

        <Widget type="Button" name="NewGameBtn">
            <Property key="Caption" value="New Game"/>
        </Widget>

        <Widget type="ScrollView" name="ScrollRegion">
            <Property key="Size" value="400 600"/>
        </Widget>
    </Widget>
</MyGUI>

// In C++:
MyGUI::LayoutManager::getInstance().loadLayout("layout.xml");
auto button = MyGUI::Gui::getInstance().findWidget<MyGUI::Button>("NewGameBtn");
button->eventMouseButtonClick += newDelegate(this, &OnNewGameClick);
```

**Your Facade API**:
```cpp
// MyGUI doesn't match .Args{} well (expects XML or verbose C++)
// Heavy wrapping needed
auto menu = std::make_shared<UI::Window>(UI::Window::Args{
    .title = "Main Menu",
    .position = glm::vec2(100, 100),
    .size = glm::vec2(800, 600)
});

// Facade creates MyGUI::Window, sets properties
```

**Integration Complexity**: **High**
- Large framework with many subsystems
- Expects XML layouts (or verbose C++ API)
- Custom rendering platform required
- Steep learning curve

**Scrolling**:
- `ScrollView` widget with full support
- Handles clipping, scrollbars, kinetic scrolling
- Good performance

**Text Rendering**:
- FreeType integration
- Bitmap fonts also supported
- Customizable

**Pros**:
- Designed for games (skinning, animations)
- Feature-rich widget library
- Good scrolling support
- Active community

**Cons**:
- Very XML-centric (not C++-first)
- Large and complex
- API doesn't match your style
- Heavy dependencies
- Overkill for simple UIs

**Best For**: MMOs, complex game UIs with heavy skinning needs

---

### Option 6: Custom Implementation

**Architecture**: Retained mode scene graph (your design)
- Component tree (like React)
- Declarative `.Args{}` API
- Custom renderer integration

**Example Code** (your API):
```cpp
auto menu = std::make_shared<UI::Container>(UI::Container::Args{
    .position = glm::vec2(100, 100),
    .size = glm::vec2(800, 600),
    .style = UI::Style{
        .backgroundColor = glm::vec4(0.2, 0.2, 0.2, 1.0),
    }
});

auto button = std::make_shared<UI::Button>(UI::Button::Args{
    .label = "New Game",
    .position = glm::vec2(300, 200),
    .size = glm::vec2(200, 60),
    .style = UI::Style{
        .backgroundColor = glm::vec4(0.3, 0.5, 0.8, 1.0),
        .cornerRadius = 5.0f
    },
    .onClick = []() { StartNewGame(); },
    .zIndex = 10.0f
});

auto scrollView = std::make_shared<UI::ScrollContainer>(UI::ScrollContainer::Args{
    .position = glm::vec2(200, 300),
    .size = glm::vec2(400, 600),
    .contentSize = glm::vec2(400, 2000), // Content larger than viewport
    .enableScissor = true,
    .style = UI::Style{
        .overflow = UI::Overflow::ScrollY
    }
});

for (int i = 0; i < 100; i++) {
    auto label = std::make_shared<UI::Text>(UI::Text::Args{
        .text = "Item " + std::to_string(i),
        .position = glm::vec2(10, i * 25),
        .fontSize = 16.0f
    });
    scrollView->addChild(label);
}

menu->addChild(button);
menu->addChild(scrollView);

// Add to root scene
scene->addChild(menu);
```

**What You'd Build**:

**Core Systems** (~2-3 weeks):
```cpp
// Scene graph
class UIElement {
    glm::vec2 m_position;
    glm::vec2 m_size;
    float m_zIndex;
    std::vector<std::shared_ptr<UIElement>> m_children;

    virtual void render(Renderer& renderer);
    virtual void update(float dt);
    virtual bool handleEvent(const Event& evt);

    void addChild(std::shared_ptr<UIElement> child);
    glm::mat4 getWorldTransform() const;
};

// Event system
class EventSystem {
    void dispatchMouseEvent(MouseEvent evt);
    void dispatchKeyEvent(KeyEvent evt);
    UIElement* hitTest(glm::vec2 position);
    UIElement* m_focusedElement;
};

// Clipping stack (scissor test)
class ClipStack {
    void push(const Rect& clipRect);
    void pop();
    std::stack<Rect> m_stack;
};
```

**Components** (~2 weeks):
- Rectangle (with corner radius, borders)
- Text (SDF font rendering)
- Image (texture-mapped quad)
- Button (Rectangle + Text + hover/click states)
- TextInput (cursor, selection, keyboard input)
- ScrollContainer (clipping, scrollbar, mouse wheel)

**Scrolling Container** (~3-4 days):
```cpp
class ScrollContainer : public UIElement {
    glm::vec2 m_scrollOffset{0, 0};
    glm::vec2 m_contentSize;
    bool m_enableScissor = true;

    void render(Renderer& renderer) override {
        if (m_enableScissor) {
            // Push scissor rect
            Rect viewportRect = {m_position, m_size};
            renderer.pushScissor(viewportRect);
        }

        // Apply scroll offset to children
        glm::mat4 scrollTransform = glm::translate(
            glm::mat4(1.0f),
            glm::vec3(-m_scrollOffset.x, -m_scrollOffset.y, 0)
        );

        // Cull children outside viewport
        for (auto& child : m_children) {
            Rect childRect = {
                child->getWorldPosition(),
                child->getSize()
            };

            if (viewportRect.intersects(childRect)) {
                child->render(renderer);
            }
        }

        if (m_enableScissor) {
            renderer.popScissor();
        }

        // Render scrollbar if needed
        if (m_contentSize.y > m_size.y) {
            renderScrollbar(renderer);
        }
    }

    bool handleEvent(const Event& evt) override {
        if (evt.type == EventType::MouseWheel) {
            m_scrollOffset.y += evt.wheelDelta * 20.0f;
            m_scrollOffset.y = glm::clamp(
                m_scrollOffset.y,
                0.0f,
                m_contentSize.y - m_size.y
            );
            return true; // Consume event
        }
        // ... handle drag scrolling
    }
};
```

**Integration Complexity**: **High (implementation effort)**
- You build everything from scratch
- Full control over API and behavior
- Deep OpenGL knowledge required (scissor test, batching, text rendering)
- Ongoing maintenance burden

**Scrolling**:
- You design exactly what you need
- OpenGL scissor test for clipping (solves your masking question!)
- Culling for performance (don't render off-screen children)
- Complete control

**Text Rendering**:
- Integrate msdfgen for SDF fonts
- Perfect match for vector aesthetic
- You control font atlas generation
- Crisp text at any size/zoom

**Pros**:
- **Perfect API** - exactly your `.Args{}` style
- Aligns with "roll our own" philosophy
- Deep learning experience (C++, OpenGL, UI systems)
- No external dependencies (except font lib)
- Integrates perfectly with custom renderer
- Full control over features and performance

**Cons**:
- **Time investment** - 4-6 weeks for basic functionality
- Ongoing maintenance burden
- Missing features third-party libs have (rich text, animations, accessibility)
- Bugs will happen (need thorough testing)
- No community support (you are the community)

**Estimated Implementation Time**:
- Core scene graph: 3-4 days
- Event system: 2-3 days
- Basic primitives (Rectangle, Text): 2-3 days
- Button component: 1 day
- ScrollContainer: 3-4 days
- TextInput: 3-4 days
- SDF text rendering: 3-5 days
- **Total: 4-6 weeks**

**Best For**: Learning, full control, perfect API match, "roll our own" philosophy

---

## Decision Framework

Not a recommendation, but factors to weigh:

### 1. API Match
**How important is the `.Args{}` designated initializer API?**
- **High importance**: Custom > NanoGUI > (ImGui/Nuklear with heavy facade)
- **Low importance**: Any library works

### 2. Philosophy Alignment
**"Roll our own implementations" - how strict?**
- **Strict**: Custom only
- **Pragmatic**: Use library where it saves time, wrap API for consistency

### 3. Time Budget
**How much time can UI consume?**
- **Constrained** (ship game quickly): ImGui or NanoGUI (quick setup)
- **Flexible** (learning project): Custom (deep learning experience)

### 4. UI Complexity
**How complex will your UI be?**
- **Simple** (main menu, HUD): Custom is very feasible
- **Complex** (inventories, skill trees, chat): Library saves time

### 5. Learning Goals
**What do you want to learn?**
- **OpenGL/graphics**: Custom implementation teaches clipping, batching, text rendering
- **Game systems**: Use library, focus on gameplay

### 6. Debug vs Game UI
**Are these separate concerns?**
- Debug UI: ImGui (industry standard for tools)
- Game UI: Custom or NanoGUI (prettier, more control)
- **Hybrid approach possible**

## Recommendations by Scenario

**Scenario A: "I want to ship a game"**
→ Use **NanoGUI** or **ImGui**
- Quick to integrate
- Wrap API for `.Args{}` consistency
- Focus on game features

**Scenario B: "I want to learn C++/OpenGL deeply"**
→ Build **Custom**
- Best learning experience
- Perfect API from day one
- Aligns with project philosophy
- You understand every line

**Scenario C: "I want the best of both"**
→ **Hybrid**: ImGui for debug, Custom for game UI
- ImGui for developer tools (port 8081 inspector already needs UI!)
- Custom for player-facing UI (menus, HUD)
- Each tool for its strength

**Scenario D: "I love web development patterns"**
→ **RmlUI**
- Most web-like
- HTML/CSS comfort zone
- But: doesn't match your `.Args{}` preference

## Implementation Path Recommendation

Based on your background (React developer) and requirements:

**Phase 1: Use ImGui for ui-sandbox development tools** (Week 1)
- Get HTTP inspector UI running quickly
- Use ImGui for metrics charts, log viewer
- Learn OpenGL integration with battle-tested library

**Phase 2: Build custom game UI components** (Weeks 2-5)
- Start with Rectangle, Text primitives
- Build ScrollContainer (solve your clipping issue!)
- Add Button, TextInput
- Perfect your `.Args{}` API

**Phase 3: Evaluate and decide** (Week 6)
- By now you've used ImGui AND built custom components
- Informed decision: continue custom or wrap NanoGUI?
- You understand the tradeoffs from experience

This approach:
- ✅ Gets ui-sandbox running quickly (ImGui)
- ✅ Teaches OpenGL UI concepts (custom primitives)
- ✅ Defers the big decision until you have experience
- ✅ Not wasted work (ImGui stays for debug tools)

## Next Steps

1. **Make library decision** (or hybrid approach)
2. Create `architecture.md` - Scene graph and event system design
3. Create `scrolling-containers.md` - Solve clipping/culling with OpenGL scissor test
4. Create `text-rendering.md` - SDF font integration

## References

**Dear ImGui**
- GitHub: https://github.com/ocornut/imgui
- Docs: https://github.com/ocornut/imgui/wiki

**Nuklear**
- GitHub: https://github.com/Immediate-Mode-UI/Nuklear

**RmlUI**
- Website: https://mikke89.github.io/RmlUiDoc/
- GitHub: https://github.com/mikke89/RmlUi

**NanoGUI**
- GitHub: https://github.com/mitsuba-renderer/nanogui

**MyGUI**
- Website: http://mygui.info/
- GitHub: https://github.com/MyGUI/mygui

**SDF Font Rendering**
- msdfgen: https://github.com/Chlumsky/msdfgen
- Valve's original paper: https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf

## Revision History

- 2025-10-26: Initial library comparison document created
