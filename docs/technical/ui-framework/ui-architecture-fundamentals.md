# UI Architecture Fundamentals for Production Games

Created: 2025-10-26
Status: Decision Critical

## Context

This document explains UI architecture patterns for **production-grade complex game UI** - the kind needed for management/strategy games with inventory systems, skill trees, resource management interfaces, etc.

**Not covered:** Simple menu UI, debug tools, editor UI.

## The Big Picture: How Game UIs Actually Work

### The DOM/React Model You Know

In web development:
```jsx
// You describe the UI declaratively
function InventoryPanel({ items }) {
  return (
    <div className="inventory">
      {items.map(item => (
        <ItemSlot key={item.id} item={item} />
      ))}
    </div>
  );
}

// React:
// 1. Builds a virtual DOM (scene graph)
// 2. Diffs against previous render
// 3. Updates only what changed in real DOM
// 4. Browser renders pixels
```

**This is called "Retained Mode" with "Declarative API"**

### Three Fundamentally Different Architectures

## Architecture 1: Immediate Mode

**How it works:**
```cpp
// Called EVERY FRAME (60 times/second)
void RenderUI() {
    for (auto& item : inventory) {
        if (Button(item.name)) {
            UseItem(item);
        }
    }
}
// No persistent UI objects. Regenerated from scratch every frame.
```

**Mental Model:**
- Imagine React re-ran your entire component tree 60 times per second
- No virtual DOM, no diffing
- Pure functions: `gameState → UI calls → pixels`

**Real-World Examples:**
- Dear ImGui (debug tools, editors)
- Nuklear
- Most game engine editors (Unity, Unreal inspectors)

**When to use:**
- ✅ Debug/development tools
- ✅ Editor UI that changes constantly
- ✅ Simple overlays

**When NOT to use:**
- ❌ Complex game UI with thousands of elements
- ❌ Data binding and reactive updates
- ❌ Complex layouts (flexbox, grid)
- ❌ UI that needs to persist state

**Performance:**
- CPU: Regenerating is fast (millions of function calls/sec)
- GPU: Still batches geometry efficiently
- **But:** No caching, no dirty tracking for large UIs

**Why you DON'T want this:**
You said "extremely complex UI panes" - immediate mode doesn't scale well to thousands of dynamic elements.

---

## Architecture 2: Retained Mode Scene Graph

**How it works:**
```cpp
// Built ONCE (or when data changes)
auto inventory = std::make_shared<InventoryPanel>(InventoryPanel::Args{
    .position = glm::vec2(100, 100),
    .size = glm::vec2(400, 600)
});

for (auto& item : gameState.items) {
    auto slot = std::make_shared<ItemSlot>(ItemSlot::Args{
        .item = item,
        .onClick = [&]() { UseItem(item); }
    });
    inventory->addChild(slot);
}

// Every frame:
void Render() {
    inventory->render();  // Traverses tree, renders only visible
}
```

**What's a Scene Graph?**

Like React's virtual DOM - a tree of UI objects:
```
Window (root)
├── InventoryPanel
│   ├── ItemSlot (item 1)
│   ├── ItemSlot (item 2)
│   └── ...
└── SkillTreePanel
    ├── SkillNode (strength)
    └── SkillNode (dexterity)
```

Each node has:
- Position, size
- Children
- Render function
- Event handlers

**Mental Model:**
- **Exactly like React's virtual DOM**
- Build tree once
- Update only when data changes
- Traverse and render each frame

**Real-World Examples:**
- Unity UI Toolkit (formerly UIElements)
- Unreal UMG (Unreal Motion Graphics)
- Custom game UIs (RimWorld, Factorio)
- NanoGUI
- Your previous C++ project!

**When to use:**
- ✅ Complex game UI
- ✅ Thousands of elements
- ✅ Data-driven UI
- ✅ Persistent state

**Performance:**
- Tree traversal: ~0.05ms for 1000 elements
- Dirty tracking: Only update what changed
- Culling: Don't render off-screen elements
- **Scales to very large UIs**

**Why you WANT this:**
This is what complex games use. Matches your `.Args{}` API perfectly.

---

## Architecture 3: Markup-Based Retained Mode

**How it works:**
```cpp
// Define UI in markup (HTML/XML-like)
// inventory_panel.rml:
<template>
  <div class="inventory-panel">
    <div class="header">Inventory</div>
    <scrollview class="item-grid">
      <!-- Items populated from data binding -->
    </scrollview>
  </div>
</template>

// inventory_panel.rcss (CSS-like):
.inventory-panel {
    width: 400px;
    height: 600px;
    background-color: #2a2a2a;
}

.item-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    gap: 10px;
}

// In C++:
auto panel = context->LoadDocument("inventory_panel.rml");
panel->GetElementById("item-grid")->AppendChild(/* item slots */);
```

**Mental Model:**
- **Exactly like React with JSX/HTML**
- Markup describes structure
- CSS describes layout and style
- C++ provides data and logic

**Real-World Examples:**
- RmlUI (formerly libRocket)
- Coherent UI (uses HTML/CSS/JS with Chromium)
- Scaleform (Flash-based, deprecated)

**When to use:**
- ✅ Designers/artists need to build UI
- ✅ Complex layouts (flexbox, grid)
- ✅ Want CSS-like styling
- ✅ Familiar with HTML/CSS

**Performance:**
- Same as retained mode scene graph
- Layout engine adds overhead (~0.2ms for complex layouts)
- **Scales well to large UIs**

**Why you might WANT this:**
You're a React developer - this is most familiar!

---

## What Do Production Games Actually Use?

### AAA Games (Professional Engines)

**Unity:**
- **UI Toolkit** (retained mode, React-like)
- USS (CSS-like styling)
- UXML (XML layouts)
- **Used by:** Hearthstone, Subnautica, Rust

**Unreal Engine:**
- **UMG** (Unreal Motion Graphics) - retained mode
- Visual Blueprint editor
- C++ widgets for complex logic
- **Used by:** Fortnite, Gears of War, ARK

**CryEngine:**
- **Scaleform** (Flash, deprecated)
- Now: Custom retained mode systems

### Indie Management/Strategy Games

**RimWorld:**
- Custom retained mode UI
- Scene graph with layouting
- Data binding to game state
- **~2000 concurrent UI elements**

**Factorio:**
- Custom retained mode UI
- Efficient dirty tracking
- Complex layouts (grids, tables)

**Dwarf Fortress (Premium):**
- Custom retained mode
- Previously text-based, now graphical UI

**Oxygen Not Included:**
- Unity UI Toolkit
- Data-driven panels
- Complex nested layouts

### Pattern: Complex Games → Retained Mode

**Zero professional complex games use immediate mode for gameplay UI.**

Immediate mode is for:
- Debug tools
- Editor inspectors
- Development overlays

## Decision Framework for Your Game

### Your Requirements (Management Game):
- ✅ Complex resource management UI
- ✅ Dynamic data (updates when game state changes)
- ✅ Thousands of UI elements
- ✅ Nested layouts
- ✅ Professional quality

### Your Preferences:
- ✅ React/JSX familiarity
- ✅ Declarative `.Args{}` API
- ✅ HTML/CSS-like if possible

### Your Constraints:
- ✅ Production game (not learning project)
- ✅ Must integrate with custom OpenGL renderer
- ✅ Minimal dependencies preferred

## The Three Real Options

### Option A: RmlUI (HTML/CSS-like)

**What it is:**
- Retained mode scene graph
- HTML-like markup (`.rml` files)
- CSS-like styling (`.rcss` files)
- Full layout engine (flexbox)

**Example:**
```html
<!-- main_menu.rml -->
<rml>
<head>
    <link type="text/rcss" href="menu.rcss"/>
</head>
<body>
    <div id="menu-container">
        <button class="menu-btn" onclick="new_game">New Game</button>
        <button class="menu-btn" onclick="load_game">Load Game</button>
    </div>
</body>
</rml>
```

```css
/* menu.rcss */
#menu-container {
    display: flex;
    flex-direction: column;
    gap: 20px;
    padding: 50px;
}

.menu-btn {
    width: 200px;
    height: 60px;
    background-color: #4a4a4a;
    border: 2px solid #888;
    border-radius: 5px;
}

.menu-btn:hover {
    background-color: #6a6a6a;
}
```

**Pros:**
- ✅ **Most familiar to you** (HTML/CSS like React)
- ✅ Full flexbox layout engine
- ✅ Declarative markup
- ✅ Data binding support
- ✅ Scales to thousands of elements
- ✅ Professional quality (used in shipped games)
- ✅ Active development

**Cons:**
- ❌ FreeType dependency (fonts)
- ❌ Learning curve for RML/RCSS (slight differences from HTML/CSS)
- ❌ Requires custom OpenGL backend (~500 lines)
- ❌ Larger library

**Integration:**
```cpp
// Initialize
Rml::SetRenderInterface(&myOpenGLRenderer);  // Your backend
auto context = Rml::CreateContext("main", Rml::Vector2i(1920, 1080));

// Load document
auto doc = context->LoadDocument("main_menu.rml");
doc->Show();

// Bind events
auto button = doc->GetElementById("new-game-btn");
button->AddEventListener("click", &OnNewGame);

// Update (60 FPS)
context->Update();
context->Render();
```

**Used by:**
- Multiple shipped indie games
- Game studios for in-game UI
- Not as famous as Unity/Unreal but production-ready

**Verdict for you:**
- ✅ Best match for React background
- ✅ Handles complex UI
- ⚠️ FreeType dependency (but you need font rendering anyway)

---

### Option B: NanoGUI (Modern C++ Retained)

**What it is:**
- Retained mode scene graph
- C++ builder API (like your `.Args{}`)
- Automatic layout (limited flexbox-like)

**Example:**
```cpp
auto window = new Window(screen, "Main Menu");
window->setLayout(new BoxLayout(Orientation::Vertical, Alignment::Middle, 20, 20));

auto btnNewGame = new Button(window, "New Game");
btnNewGame->setCallback([]() { StartNewGame(); });

auto btnLoadGame = new Button(window, "Load Game");
btnLoadGame->setCallback([]() { LoadGame(); });
```

**Pros:**
- ✅ Modern C++11 API
- ✅ Retained mode scene graph
- ✅ Somewhat similar to your `.Args{}`
- ✅ OpenGL integration built-in

**Cons:**
- ❌ **NanoVG dependency** - own renderer
- ❌ **Render conflicts possible** - NanoVG manages OpenGL state
- ❌ Limited widget library
- ❌ No markup/CSS (pure C++)
- ❌ Limited layout options (basic box layout only)
- ❌ **Maintenance mode** (not actively developed)
- ❌ Not designed for thousands of elements

**NanoVG Conflict Details:**
```cpp
// NanoVG rendering:
nvgBeginFrame(vg, width, height, pixelRatio);
    // ← Modifies stencil buffer, scissor state, shaders
    // ← May conflict with your vector graphics rendering
nvgEndFrame(vg);
```

**Verdict for you:**
- ⚠️ Render conflicts are a real risk
- ❌ Not designed for complex game UI
- ❌ Limited layout capabilities
- ❌ Not actively maintained

**Recommendation:** Don't use NanoGUI for your use case.

---

### Option C: Custom Retained Mode UI

**What it is:**
- You build retained mode scene graph
- Your perfect `.Args{}` API
- Your layout system (or manual positioning)

**Example:**
```cpp
auto menu = std::make_shared<UI::Container>(UI::Container::Args{
    .position = glm::vec2(100, 100),
    .size = glm::vec2(400, 600),
    .layout = UI::Layout::Vertical{
        .gap = 20,
        .padding = 50
    }
});

auto btnNewGame = std::make_shared<UI::Button>(UI::Button::Args{
    .label = "New Game",
    .size = glm::vec2(200, 60),
    .style = UI::Style{
        .backgroundColor = glm::vec4(0.3, 0.3, 0.3, 1.0),
        .hoverColor = glm::vec4(0.4, 0.4, 0.4, 1.0)
    },
    .onClick = []() { StartNewGame(); }
});

menu->addChild(btnNewGame);
```

**What you'd build:**
1. **Scene graph** (~2 weeks)
   - UIElement base class
   - Parent/child hierarchy
   - Transform propagation
   - Z-index sorting

2. **Event system** (~1 week)
   - Mouse events (click, hover, drag)
   - Keyboard events (focus, tab navigation)
   - Event bubbling/capturing

3. **Layout system** (~2-3 weeks) - OPTIONAL
   - Flexbox-like algorithm
   - OR just manual positioning

4. **Components** (~2-3 weeks)
   - Rectangle, Text, Image
   - Button, TextInput, Checkbox
   - ScrollContainer
   - Complex: Grid, Table, Tree

5. **Rendering** (~1 week)
   - Batching with your renderer
   - Scissor test for clipping
   - Text rendering (SDF fonts)

**Total: 8-12 weeks** for full-featured UI framework

**Pros:**
- ✅ Perfect `.Args{}` API
- ✅ Zero rendering conflicts
- ✅ Exactly what you need, nothing more
- ✅ Deep understanding of your codebase
- ✅ No dependencies (except font lib)

**Cons:**
- ❌ **8-12 weeks development time**
- ❌ Ongoing maintenance burden
- ❌ No layout engine (unless you build flexbox)
- ❌ Missing features (rich text, animations, etc.)
- ❌ You're the only support

**Verdict for you:**
- ⚠️ 8-12 weeks is a LOT for a production game
- ❌ Without layout engine, complex UI is hard
- ❌ You said "not trying to learn" - this is maximum learning

---

## The Honest Production Game Recommendation

### For Complex Management Game UI:

**Use RmlUI** (HTML/CSS-like)

**Why:**
1. **You're a React developer** - HTML/CSS is your language
2. **Complex UI is critical** - RmlUI designed for this
3. **Flexbox layout** - handles complex nested layouts
4. **Production ready** - used in shipped games
5. **Data binding** - UI updates when game state changes
6. **Designer-friendly** - artists can work on markup/CSS

**Accept:**
- FreeType dependency (but you need fonts anyway)
- Learning RML/RCSS (minor differences from HTML/CSS)
- Writing OpenGL backend (~500 lines)

**Timeline:**
- Integration: 1-2 weeks
- Learning: 1 week
- Building your UI: ongoing

**vs Custom (8-12 weeks before you can even start building UI)**

### Alternative if You Hate Dependencies:

**Build custom BUT:**
- Use **manual positioning** (no layout engine)
- Keep it simple (just components, no advanced features)
- **4-6 weeks** instead of 8-12

This is only viable if you're okay with manually positioning everything:
```cpp
auto button = UI::Button({.position = glm::vec2(300, 400), ...});
// No flexbox, no grid, just x/y coords
```

## My Recommendation

**For a production management game:**

**Phase 1: Prototype with RmlUI** (2 weeks)
- Integrate RmlUI
- Build one complex UI panel
- Test with real game data
- Evaluate if it works for your needs

**Phase 2: Decide**
- If RmlUI works: Use it
- If RmlUI doesn't work: Build custom (now you know why)

**Don't:**
- ❌ Use ImGui (not for complex game UI)
- ❌ Use NanoGUI (render conflicts, limited)
- ❌ Build custom first (validate assumptions with prototype)

## Questions to Help You Decide

1. **How complex is "complex"?**
   - 10s of elements: Custom might be fine
   - 100s of elements: Need layout engine
   - 1000s of elements: Definitely need RmlUI or Unity/Unreal

2. **Do you need flexbox/grid layouts?**
   - Yes: RmlUI (has flexbox)
   - No: Custom with manual positioning

3. **Timeline to ship game?**
   - <6 months: Use RmlUI
   - >1 year: Could justify custom

4. **Is FreeType dependency a dealbreaker?**
   - No: Use RmlUI
   - Yes: Custom (but you need fonts anyway!)

What's your timeline and how complex is the UI?
