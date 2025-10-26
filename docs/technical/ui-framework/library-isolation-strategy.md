# UI Library Isolation & Risk Analysis

Created: 2025-10-26
Status: Critical Architecture Decision

## The Problem

Before adopting RmlUI (or any major library), we need to answer:
1. **How do we isolate it from our codebase?**
2. **Can we switch libraries later without rewriting everything?**
3. **What hidden constraints does it impose?**
4. **Does it conflict with our other architectural goals?**

This document provides honest analysis and mitigation strategies.

## Isolation Strategy: The Abstraction Layer

### The Wrong Way (Tight Coupling)

**Game code directly using RmlUI everywhere:**
```cpp
// BAD: Game logic directly depends on RmlUI
#include <RmlUi/Core.h>

void ShowInventory() {
    auto doc = context->LoadDocument("inventory.rml");  // ← RmlUI API
    auto grid = doc->GetElementById("item-grid");       // ← RmlUI API

    for (auto& item : player.inventory) {
        auto slot = doc->CreateElement("div");          // ← RmlUI API
        slot->SetInnerRML(item.name);                   // ← RmlUI API
        grid->AppendChild(slot);
    }
}
```

**Problems:**
- ❌ Game code imports RmlUI headers everywhere
- ❌ RmlUI types leak into game logic
- ❌ Switching libraries means rewriting all UI code
- ❌ Can't test UI without RmlUI
- ❌ Compile-time dependency on RmlUI

### The Right Way (Isolation Layer)

**Create a facade between game and RmlUI:**

```cpp
// libs/ui/include/ui/ui_system.h
// NO RmlUI headers! Pure interface.

namespace UI {

// Abstract interface - no RmlUI types!
class IDocument {
public:
    virtual ~IDocument() = default;
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual void SetDataModel(const json& data) = 0;
    virtual void BindEvent(const std::string& elementId,
                          const std::string& event,
                          std::function<void()> handler) = 0;
};

class IUISystem {
public:
    virtual ~IUISystem() = default;
    virtual std::unique_ptr<IDocument> LoadDocument(const std::string& path) = 0;
    virtual void Update(float dt) = 0;
    virtual void Render() = 0;
};

// Factory function - only interface exposed
std::unique_ptr<IUISystem> CreateUISystem(Renderer* renderer);

} // namespace UI
```

**Implementation (hidden in .cpp):**
```cpp
// libs/ui/src/rmlui_system.cpp
// RmlUI headers ONLY in implementation files

#include <RmlUi/Core.h>
#include "ui/ui_system.h"

namespace UI {

// Internal implementation - NOT exposed in headers
class RmlUIDocument : public IDocument {
    Rml::ElementDocument* m_doc;  // Private, not in header!

public:
    void Show() override {
        m_doc->Show();
    }

    void SetDataModel(const json& data) override {
        // Translate json to RmlUI data model
        auto dataModel = m_doc->CreateDataModel("game");
        // ... populate from json
    }

    void BindEvent(const std::string& elementId,
                   const std::string& event,
                   std::function<void()> handler) override {
        auto element = m_doc->GetElementById(elementId.c_str());
        // Wrap RmlUI event listener
        element->AddEventListener(event.c_str(),
            new FunctionEventListener(handler));
    }
};

class RmlUISystem : public IUISystem {
    Rml::Context* m_context;

public:
    std::unique_ptr<IDocument> LoadDocument(const std::string& path) override {
        auto doc = m_context->LoadDocument(path.c_str());
        return std::make_unique<RmlUIDocument>(doc);
    }

    // ... implement interface
};

// Factory implementation
std::unique_ptr<IUISystem> CreateUISystem(Renderer* renderer) {
    return std::make_unique<RmlUISystem>(renderer);
}

} // namespace UI
```

**Game code now uses interface:**
```cpp
// Game code - NO RmlUI dependency!
#include "ui/ui_system.h"  // Only our interface

void ShowInventory(UI::IUISystem* uiSystem) {
    auto doc = uiSystem->LoadDocument("inventory.rml");

    // Data model (pure JSON)
    json data = {
        {"items", GetInventoryItems()}
    };
    doc->SetDataModel(data);

    // Event binding (no RmlUI types)
    doc->BindEvent("close-btn", "click", []() {
        CloseInventory();
    });

    doc->Show();
}
```

**Benefits:**
- ✅ Game code has zero RmlUI dependencies
- ✅ Switching libraries = rewrite `rmlui_system.cpp` only
- ✅ Can test with mock UI system
- ✅ Compile-time isolation
- ✅ RmlUI types never leak

## What Can't Be Isolated?

### 1. Markup Files (.rml)

**The problem:**
```html
<!-- inventory.rml -->
<div class="inventory">
    <div data-for="item in items">
        <span>{item.name}</span>
    </div>
</div>
```

**This is RmlUI-specific markup!**

**If you switch libraries:**
- ❌ Have to rewrite all .rml files
- ❌ Data binding syntax is RmlUI-specific
- ❌ Event attributes might differ

**Mitigation strategies:**

**Option A: Keep markup minimal, logic in C++**
```html
<!-- Minimal markup - just structure -->
<div id="inventory-container"></div>
```

```cpp
// Logic in C++ (portable)
for (auto& item : items) {
    auto slot = CreateItemSlot(item);
    container->AddChild(slot);
}
```

**Option B: Generate markup from C++ (most portable)**
```cpp
// No .rml files at all!
std::string GenerateInventoryMarkup(const std::vector<Item>& items) {
    std::stringstream markup;
    markup << "<div class='inventory'>";
    for (auto& item : items) {
        markup << "<div class='item-slot'>";
        markup << "<span>" << item.name << "</span>";
        markup << "</div>";
    }
    markup << "</div>";
    return markup.str();
}

doc->SetInnerHTML(GenerateInventoryMarkup(items));
```

**Option C: Accept the coupling for complex UI**
- For truly complex UI with hundreds of elements, markup is worth it
- Migration cost is acceptable vs building everything in C++
- Most libraries support HTML-like markup anyway

**Recommendation:**
- Simple UI (menus): Generate from C++ (portable)
- Complex UI (inventory grids, skill trees): Use markup (accept coupling)

### 2. Styling (.rcss)

**The problem:**
```css
/* inventory.rcss */
.inventory {
    display: flex;
    flex-wrap: wrap;
}
```

**RmlUI's CSS is similar to real CSS but not identical.**

**If you switch libraries:**
- ⚠️ Might need to rewrite styles
- ⚠️ Property names might differ

**Mitigation:**
- Stick to standard CSS properties (flex, padding, margin)
- Avoid RmlUI-specific extensions
- Keep styles in separate files (easier to rewrite)

### 3. Data Binding Syntax

**The problem:**
```html
<div data-for="item in items">
  {item.name}
</div>
```

**This is RmlUI's data binding syntax.**

**If you switch to:**
- Another library → Different syntax
- Custom UI → No data binding, use C++ loops

**Mitigation:**
- Use data binding sparingly for complex UIs
- For simple UIs, update via C++ (portable)

## Hidden Constraints & Conflicts

### Constraint 1: Rendering Architecture

**RmlUI's renderer backend:**
```cpp
class RenderInterface {
    virtual void RenderGeometry(...) = 0;
    virtual CompiledGeometryHandle CompileGeometry(...) = 0;
    virtual void RenderCompiledGeometry(...) = 0;
};
```

**Integration with your renderer:**

**Potential conflicts:**
1. **Shader management** - RmlUI expects specific shaders
2. **Batching** - RmlUI compiles geometry, might break your batching
3. **Render passes** - Need careful ordering with vector graphics
4. **OpenGL state** - RmlUI changes state, need save/restore

**Mitigation:**
```cpp
class YourRmlUIBackend : public Rml::RenderInterface {
    Renderer* m_renderer;

    void RenderGeometry(...) override {
        // Save OpenGL state
        m_renderer->PushState();

        // Translate RmlUI geometry to your format
        auto batch = m_renderer->CreateBatch();
        batch->AddVertices(...);

        // Render using your batching system
        m_renderer->RenderBatch(batch);

        // Restore state
        m_renderer->PopState();
    }
};
```

**Does this conflict with vector graphics?**

**Render order:**
```cpp
void RenderFrame() {
    // 1. Tiles (vector graphics Tier 1)
    m_vectorRenderer->RenderTiles();

    // 2. Entities (vector graphics Tier 3)
    m_vectorRenderer->RenderEntities();

    // 3. UI (RmlUI)
    m_uiSystem->Render();  // ← Uses your backend
}
```

✅ **No conflict if:**
- RmlUI backend uses your renderer
- You control render pass ordering
- State is properly saved/restored

❌ **Potential conflict:**
- If RmlUI caches compiled geometry, might not integrate with your batching
- Performance: RmlUI recompiles geometry on change (might be slower than your system)

### Constraint 2: Event System

**RmlUI's events:**
```cpp
element->AddEventListener("click", eventListener);
```

**Your game's event system:**
```cpp
m_eventBus->Subscribe<ClickEvent>(handler);
```

**Integration challenge:**
- RmlUI has its own event bubbling/capturing
- Might differ from your game's event system

**Mitigation:**
```cpp
// Bridge: RmlUI events → Game events
class EventBridge : public Rml::EventListener {
    EventBus* m_gameBus;

    void ProcessEvent(Rml::Event& evt) override {
        // Translate RmlUI event to game event
        if (evt.GetType() == "click") {
            m_gameBus->Emit<UIClickEvent>({
                .elementId = evt.GetTargetElement()->GetId(),
                .position = {evt.GetParameter("mouse_x"), evt.GetParameter("mouse_y")}
            });
        }
    }
};
```

### Constraint 3: Update Frequency

**RmlUI expects to be updated every frame:**
```cpp
void GameLoop() {
    while (running) {
        context->Update();  // ← Must call every frame
        context->Render();
    }
}
```

**Implications:**
- ✅ Fine for game UI (already running at 60 FPS)
- ⚠️ Can't pause UI updates separately from game
- ⚠️ UI animations always run (can't freeze UI while keeping game running)

**Conflict with project goals?**
- Probably fine, games already update every frame

### Constraint 4: Memory Management

**RmlUI uses reference counting:**
```cpp
Rml::ElementPtr element = doc->CreateElement("div");
// ← Returns reference-counted smart pointer
```

**Your project uses `std::shared_ptr`:**
```cpp
auto element = std::make_shared<UIElement>(...);
```

**Integration challenge:**
- Different smart pointer types
- Can't store RmlUI elements in your containers easily

**Mitigation:**
```cpp
// Wrapper that adapts reference counting
class ElementWrapper : public IElement {
    Rml::ElementPtr m_rmlElement;  // RmlUI smart ptr

public:
    // Expose via std::shared_ptr
    static std::shared_ptr<IElement> Create(Rml::ElementPtr elem) {
        return std::make_shared<ElementWrapper>(elem);
    }
};
```

### Constraint 5: Threading

**RmlUI is single-threaded:**
- ❌ Can't render UI on separate thread
- ❌ Can't update UI from background thread

**Your observability system:**
- HTTP server runs on separate thread
- Might want to update debug UI from server thread

**Conflict?**
- ⚠️ Debug UI can't be RmlUI-based if updated from HTTP thread
- ✅ Game UI is fine (main thread only)

**Mitigation:**
- Use RmlUI for game UI (main thread)
- Use ImGui for debug UI (can be separate thread with care)
- OR: Queue UI updates from HTTP thread, apply on main thread

### Constraint 6: Font Rendering

**RmlUI requires FreeType:**
```json
// vcpkg.json
{
  "dependencies": [
    "rmlui",     // ← Brings in FreeType transitively
    "freetype"
  ]
}
```

**Conflict with SDF fonts?**
- You want msdfgen for crisp vector text
- RmlUI uses FreeType for rasterization

**Can you use both?**
- ✅ Yes, but need to integrate msdfgen with RmlUI's font interface
- ⚠️ More complex integration

**Mitigation:**
```cpp
// Custom font backend for RmlUI using msdfgen
class MSDFFontEngine : public Rml::FontEngineInterface {
    // Use msdfgen instead of FreeType
    // RmlUI doesn't care how you render fonts
};

Rml::SetFontEngineInterface(&mySdfFontEngine);
```

## Project Goals Compatibility Check

### Goal 1: "Roll Our Own" Philosophy

**Conflict?**
- ⚠️ RmlUI is a large external library
- ⚠️ Goes against "roll our own" for core systems

**Counter-argument:**
- ✅ UI framework is not a core game system (it's infrastructure)
- ✅ Similar to using OpenGL vs writing software renderer
- ✅ Focus "roll our own" on game-specific systems

**Verdict:**
- Acceptable if you view UI as "platform/format support" like OpenGL
- Not acceptable if you strictly interpret "roll our own"

### Goal 2: Custom ECS

**Conflict?**
- ✅ No conflict - RmlUI doesn't use ECS
- ✅ UI components are separate from game entities

### Goal 3: Vector Graphics System

**Conflict?**
- ⚠️ RmlUI has its own rendering system
- ⚠️ Need to bridge RmlUI → your renderer

**Integration complexity:**
```cpp
// RmlUI generates geometry, you render it
class VectorUIBackend : public Rml::RenderInterface {
    void RenderGeometry(vertices, indices, texture) {
        // Convert to your vector graphics format?
        // OR render as textured quads (simpler)
        m_renderer->DrawTexturedQuads(vertices, indices, texture);
    }
};
```

**Verdict:**
- ✅ Can integrate if RmlUI backend uses your renderer
- ⚠️ RmlUI's text rendering is raster-based, not SDF

### Goal 4: Observability (HTTP Inspector)

**Conflict?**
- ⚠️ Threading issue (see Constraint 5)
- ⚠️ Need to expose RmlUI's DOM tree via HTTP

**Integration:**
```cpp
// HTTP endpoint: GET /api/ui/tree
std::string GetUITree() {
    auto doc = context->GetDocument("current");
    return SerializeElementToJSON(doc);  // Custom serialization
}
```

**Verdict:**
- ✅ Doable but requires custom serialization
- ⚠️ RmlUI's DOM structure might not match your desired format

### Goal 5: Testability

**Conflict?**
- ⚠️ Hard to test RmlUI UI without actually rendering
- ⚠️ No headless mode for testing

**Mitigation:**
- Abstract UI behind interface (shown above)
- Create mock UI system for tests
- Test game logic separately from UI

## Migration Risk Assessment

**If you need to switch libraries later:**

### Low Risk (Easy to Migrate)
- ✅ **Game logic** - isolated via interface
- ✅ **Event handlers** - bridge pattern
- ✅ **Data models** - using JSON

### Medium Risk (Some Work)
- ⚠️ **Renderer backend** - rewrite backend for new library (~500 lines)
- ⚠️ **Styling** - convert .rcss to new format

### High Risk (Significant Rewrite)
- ❌ **Markup files** - rewrite all .rml files
- ❌ **Data binding** - different syntax in new library
- ❌ **Complex layouts** - if new library doesn't support flexbox

**Estimated migration cost:**
- Simple UI (20 screens): 1-2 weeks
- Complex UI (100+ screens): 4-8 weeks

**Mitigation:**
- Keep markup minimal
- Use data binding sparingly
- Isolate via interface layer

## The Alternative: Custom UI

**If RmlUI feels too risky, build custom:**

**Pros:**
- ✅ Total control
- ✅ Zero hidden constraints
- ✅ Perfect integration

**Cons:**
- ❌ 8-12 weeks before you can build UI
- ❌ Need to build layout engine yourself
- ❌ Missing features (animations, rich text, etc.)

**For production game:**
- ⚠️ 8-12 weeks is expensive
- ⚠️ Without layout engine, complex UI is painful

## Recommendation

### Use RmlUI with Isolation Layer

**Architecture:**
```
Game Code
    ↓ (depends on interface only)
UI Interface (IUISystem, IDocument)
    ↓ (implementation)
RmlUI Adapter (rmlui_system.cpp)
    ↓
RmlUI Library
    ↓
Your OpenGL Renderer (via backend)
```

**Accept these couplings:**
1. Markup files (.rml) - migration cost acceptable
2. Styling (.rcss) - similar to CSS, portable enough
3. FreeType dependency - needed for fonts anyway

**Mitigate these risks:**
1. **Isolate game logic** - use interface pattern
2. **Keep markup minimal** - generate complex UI from C++
3. **Bridge events** - don't leak RmlUI types
4. **Custom renderer backend** - control integration

**Timeline:**
- Interface layer: 3-5 days
- RmlUI integration: 1 week
- Renderer backend: 3-5 days
- **Total: 2-3 weeks to get working**

vs

- Custom UI: 8-12 weeks
- Then start building UI

**For production game with complex UI:**
- RmlUI is lower risk (proven, faster to market)
- Isolation layer gives flexibility
- Migration cost is acceptable insurance

## Next Steps

1. **Create UI interface** (`IUISystem`, `IDocument`)
2. **Prototype RmlUI integration** (1 week)
3. **Build one complex UI panel** (inventory or skill tree)
4. **Evaluate:**
   - Does flexbox handle your layouts?
   - Is performance acceptable?
   - Are constraints manageable?
5. **Decide:** Commit to RmlUI or build custom

**Don't commit blindly - prototype first!**

## Open Questions

1. **How complex is your most complex UI?**
   - Hundreds of elements → RmlUI makes sense
   - Tens of elements → Custom might be fine

2. **Do you need real-time UI updates?**
   - Game state changes → UI updates
   - RmlUI's data binding helps here

3. **Timeline pressure?**
   - <6 months to ship → Use RmlUI
   - >1 year → Could justify custom

4. **Team size?**
   - Solo dev → RmlUI saves time
   - Team with UI engineer → Could build custom

Answer these and the decision becomes clearer.
