# Rendering Architecture Boundaries

Created: 2025-10-26
Status: Critical Architecture Decision

## The Core Problem

We have two rendering systems emerging:

**1. RmlUI** (for UI panes)
```cpp
auto rect = doc->CreateElement("div");
rect->SetAttribute("style", "width: 100px; height: 100px; background: red;");
```

**2. Custom Renderer** (for game world)
```cpp
auto rect = std::make_shared<Rendering::Rectangle>(Rendering::Rectangle::Args{
    .size = glm::vec2(100, 100),
    .color = glm::vec4(1, 0, 0, 1)
});
```

**This creates awkward questions:**
- Health bars above entities - which system?
- Selection rectangles around units - which system?
- Minimap - which system?
- Tooltips in world space - which system?
- Damage numbers floating up - which system?

**The user is right:** It's odd to have two completely different APIs for drawing a rectangle.

## The Usage Boundaries

### Clear Cases (No Ambiguity)

**RmlUI Territory:**
```
✅ Full-screen menu overlays
   - Main menu
   - Pause menu
   - Settings screen

✅ Modal dialogs
   - Confirmation dialogs
   - Load/Save file picker

✅ Full-screen game UI panels
   - Inventory screen (covers whole screen)
   - Skill tree (full overlay)
   - Crafting menu
```

**Custom Renderer Territory:**
```
✅ Game world tiles
   - Procedural ground covers
   - Terrain

✅ Static world entities
   - SVG decorations (flowers, rocks)
   - Buildings
   - Trees

✅ Animated entities
   - Creatures
   - Swaying grass
   - Water
```

### Ambiguous Cases (The Gray Area)

**In-Game HUD Elements:**
```
❓ Resource counters (top-right corner)
❓ Minimap (corner overlay)
❓ Action buttons / hotbar
❓ Clock / season indicator
```

**World-Space UI:**
```
❓ Health bars above entities
❓ Selection rectangles around units
❓ Floating damage numbers
❓ Name tags above colonists
❓ Tooltips when hovering over world objects
❓ Building placement preview (ghost)
```

**Where should these live?**

## Architecture Options

---

### Option A: RmlUI for Screen-Space Only

**Strict boundary:** RmlUI = Screen-space overlays, Custom = Everything in world

**World-space UI uses custom rendering:**
```cpp
// Health bar above entity (custom rendering)
void RenderHealthBar(Entity entity) {
    Vec2 screenPos = WorldToScreen(entity.position);

    m_renderer->DrawRect({
        .position = screenPos + Vec2(0, -20),
        .size = Vec2(50, 5),
        .color = Color::Red
    });
}
```

**HUD uses RmlUI:**
```cpp
// Resource counter (RmlUI)
<!-- hud.rml -->
<div class="resource-counter">
    <span>Wood: {wood}</span>
    <span>Stone: {stone}</span>
</div>
```

**Pros:**
- ✅ Clear boundary (screen-space vs world-space)
- ✅ Performance (custom renderer optimized for game)
- ✅ World-space UI integrated with game rendering

**Cons:**
- ❌ Two different APIs for rectangles/text
- ❌ Need to build world-space UI primitives yourself
- ❌ Tooltips are awkward (screen-space but tied to world objects)

**This is the most common pattern in games.**

---

### Option B: RmlUI for All UI (Including World-Space)

**Boundary:** RmlUI = All UI, Custom = Only tiles/entities/decorations

**World-space UI in RmlUI:**
```cpp
// Health bar as RmlUI element positioned in world
void UpdateHealthBar(Entity entity) {
    Vec2 screenPos = WorldToScreen(entity.position);

    auto healthBar = doc->GetElementById("health-" + entity.id);
    healthBar->SetAttribute("style",
        "position: absolute; "
        "left: " + std::to_string(screenPos.x) + "px; "
        "top: " + std::to_string(screenPos.y - 20) + "px;");

    // Update width based on health percentage
    auto fill = healthBar->GetElementById("fill");
    fill->SetAttribute("style",
        "width: " + std::to_string(entity.health * 50) + "px;");
}
```

**Pros:**
- ✅ One UI system for everything
- ✅ Consistent API for all UI
- ✅ Styling via CSS (health bars can be themed)

**Cons:**
- ❌ Performance nightmare (updating 100s of element positions every frame)
- ❌ RmlUI not designed for this use case
- ❌ Layout recalculation overhead
- ❌ Batching breaks with constant position updates

**Not recommended for performance reasons.**

---

### Option C: Unified Primitive Layer

**Create shared primitive API that both systems use:**

```cpp
// libs/renderer/include/renderer/primitives.h
namespace Renderer {

class Primitives {
public:
    // Low-level drawing primitives
    static void DrawRect(const Rect& bounds, const Color& color);
    static void DrawRectOutline(const Rect& bounds, const Color& color, float width);
    static void DrawText(const std::string& text, const Vec2& pos, const Font& font, const Color& color);
    static void DrawTexture(TextureHandle texture, const Rect& bounds);
    static void DrawLine(const Vec2& start, const Vec2& end, const Color& color, float width);

    // Batching
    static void BeginBatch();
    static void EndBatch();
};

} // namespace Renderer
```

**RmlUI backend uses primitives:**
```cpp
class RmlUIBackend : public Rml::RenderInterface {
    void RenderGeometry(Rml::Vertex* vertices, int num_vertices,
                       int* indices, int num_indices,
                       Rml::TextureHandle texture) override {

        Renderer::Primitives::BeginBatch();

        // Convert RmlUI geometry to primitive calls
        for (int i = 0; i < num_indices; i += 3) {
            // RmlUI gives us triangles, convert to rects/text where possible
            Renderer::Primitives::DrawRect(...);
        }

        Renderer::Primitives::EndBatch();
    }
};
```

**Game code uses same primitives:**
```cpp
void RenderHealthBar(Entity entity) {
    Vec2 screenPos = WorldToScreen(entity.position);

    // Same API as RmlUI backend uses!
    Renderer::Primitives::DrawRect({
        .position = screenPos + Vec2(0, -20),
        .size = Vec2(50, 5),
        .color = Color::Red
    });

    Renderer::Primitives::DrawRect({
        .position = screenPos + Vec2(0, -20),
        .size = Vec2(50 * entity.health, 5),
        .color = Color::Green
    });
}
```

**Your `.Args{}` wrapper uses primitives:**
```cpp
class Rectangle {
    Args m_args;

public:
    void Render() {
        Renderer::Primitives::DrawRect(
            {m_args.position, m_args.size},
            m_args.style.backgroundColor
        );
    }
};
```

**Pros:**
- ✅ **One way to draw primitives** (solves your concern!)
- ✅ RmlUI, custom rendering, game UI all use same underlying API
- ✅ Consistent batching strategy
- ✅ Easy to reason about performance

**Cons:**
- ⚠️ RmlUI backend might not map cleanly (it gives triangles, not rects)
- ⚠️ Still have RmlUI for screen-space, custom for world-space (two APIs at high level)

**This is a good middle ground.**

---

### Option D: Two Distinct Systems (Accept Duplication)

**Acknowledge the systems serve different purposes:**

**RmlUI = Complex screen-space UI**
- Full-screen panels
- Nested layouts
- Data binding
- CSS styling
- Forms, inputs, scrolling

**Custom Renderer = Game world + Simple HUD**
- Tiles, entities, effects
- World-space UI (health bars, names)
- Simple HUD elements (resource counters)
- Performance-critical rendering

**Pros:**
- ✅ Each system optimized for its use case
- ✅ Clear separation of concerns
- ✅ No forced compromises

**Cons:**
- ❌ Two different APIs
- ❌ Learning curve (know both systems)
- ❌ Some duplication (both can draw rectangles)

**This is honest about the trade-off.**

---

## Real-World Examples

### RimWorld (Custom UI)
- **All UI**: Custom retained mode scene graph
- **World-space UI**: Same system (health bars, names, etc.)
- **Render order**: World → World UI → Screen UI
- **One API for everything**

### Factorio (Custom UI)
- **All UI**: Custom immediate mode (regenerated each frame)
- **Performance**: Optimized to handle thousands of UI elements
- **One API for everything**

### Unity Games
- **Screen UI**: Unity UI Toolkit (like RmlUI)
- **World-space UI**: Unity World-Space Canvas OR custom rendering
- **Often mixed**: Use Unity for complex panels, custom for performance-critical

### Unreal Games
- **Screen UI**: UMG (Unreal Motion Graphics)
- **World-space UI**: UMG with 3D widget component OR custom
- **Often mixed**: Use UMG for everything possible

**Pattern:** AAA engines support both, but it's complex. Indies often pick one approach.

## Recommended Architecture for Your Game

### Hybrid Approach with Unified Primitives (Option C + Option A)

**Layer 1: Primitive Rendering API** (bottom)
```cpp
namespace Renderer {
    void DrawRect(Rect, Color);
    void DrawText(string, Vec2, Font, Color);
    void DrawTexture(Texture, Rect);
}
```

**Layer 2a: RmlUI** (screen-space UI)
```cpp
// Backend uses Layer 1
RmlUIBackend → Renderer::Primitives
```

**Layer 2b: Game Rendering** (world)
```cpp
// Uses Layer 1 directly
TileRenderer → Renderer::Primitives
EntityRenderer → Renderer::Primitives
```

**Layer 3: High-Level APIs**

**For screen-space UI (complex panels):**
```cpp
// RmlUI via markup or programmatic
auto panel = LoadRML("inventory.rml");
// OR
auto button = CreateButton({.label = "Click"});
```

**For world-space UI (simple, performance-critical):**
```cpp
// Direct primitive calls
Renderer::DrawRect({healthBarPos, healthBarSize}, Color::Red);
```

**For simple screen-space HUD:**
```cpp
// Could use either:
// - RmlUI for consistency
// - Primitives for performance
```

### Usage Guidelines

**Use RmlUI for:**
```
✅ Full-screen menus (main menu, settings)
✅ Complex panels (inventory with grid, skill trees)
✅ Dialogs and popups
✅ Forms with inputs and validation
✅ Anything with complex layout (flexbox, nested containers)
```

**Use Custom Rendering (Primitives) for:**
```
✅ All game world rendering (tiles, entities)
✅ World-space UI (health bars, floating text, selection boxes)
✅ Performance-critical HUD (resource counters updated every frame)
✅ Minimap (custom rendering of world data)
✅ Anything that needs tight integration with game state
```

**Gray area (choose based on complexity):**
```
❓ Tooltips:
   - If simple → Primitives
   - If rich (formatted text, images) → RmlUI

❓ HUD elements:
   - If static layout → RmlUI
   - If updated every frame → Primitives

❓ Notifications/toasts:
   - If animated and styled → RmlUI
   - If simple text → Primitives
```

## Implementation Strategy

### Phase 1: Primitive Rendering API

**Build the foundation:**
```cpp
// libs/renderer/include/renderer/primitives.h
namespace Renderer {

class Primitives {
public:
    // Initialize
    static void Init(Renderer* renderer);

    // Immediate mode API
    static void DrawRect(const Rect& bounds, const Color& color);
    static void DrawRectBorder(const Rect& bounds, const Color& color,
                               float width, float radius = 0);
    static void DrawText(const std::string& text, const Vec2& position,
                        const Font& font, const Color& color);
    static void DrawTexture(TextureHandle texture, const Rect& bounds,
                           const Rect& uvs = {0, 0, 1, 1});

    // Batching
    static void BeginFrame();
    static void EndFrame();

private:
    static Renderer* s_renderer;
    static BatchRenderer s_batch;
};

} // namespace Renderer
```

**Implementation uses your existing renderer:**
```cpp
void Primitives::DrawRect(const Rect& bounds, const Color& color) {
    // Add to batch
    s_batch.AddQuad({
        .position = bounds.position,
        .size = bounds.size,
        .color = color
    });
}

void Primitives::EndFrame() {
    // Flush batch to renderer
    s_batch.Flush();
}
```

### Phase 2: RmlUI Integration

**Backend uses primitives:**
```cpp
class RmlUIBackend : public Rml::RenderInterface {
    void RenderGeometry(...) override {
        // Convert RmlUI geometry to primitive calls
        Renderer::Primitives::BeginBatch();

        // Draw via primitives
        for (auto& tri : triangles) {
            Renderer::Primitives::DrawTexture(...);
        }

        Renderer::Primitives::EndBatch();
    }
};
```

### Phase 3: Game Rendering

**Tiles, entities use primitives:**
```cpp
void TileRenderer::RenderTile(Tile tile) {
    // Could use primitives OR vector graphics system
    // For simple colored tiles:
    Renderer::Primitives::DrawRect(tile.bounds, tile.color);

    // For complex vector graphics:
    m_vectorRenderer->RenderSVG(tile.decoration);
}
```

### Phase 4: World-Space UI

**Health bars, etc. use primitives:**
```cpp
void RenderHealthBar(Entity entity) {
    Vec2 screenPos = WorldToScreen(entity.position);
    Vec2 barPos = screenPos + Vec2(-25, -30);

    // Background
    Renderer::Primitives::DrawRect(
        {barPos, {50, 5}},
        Color(0.2, 0.2, 0.2, 0.8)
    );

    // Health fill
    Renderer::Primitives::DrawRect(
        {barPos, {50 * entity.healthPercent, 5}},
        entity.healthPercent > 0.3 ? Color::Green : Color::Red
    );

    // Border
    Renderer::Primitives::DrawRectBorder(
        {barPos, {50, 5}},
        Color::Black,
        1.0f
    );
}
```

## The Complete Render Loop

```cpp
void Game::RenderFrame() {
    m_renderer->Clear();

    // 1. Game world (tiles, entities)
    Renderer::Primitives::BeginFrame();
    m_tileRenderer->Render();        // Uses primitives or vector renderer
    m_entityRenderer->Render();      // Uses vector graphics system
    Renderer::Primitives::EndFrame();

    // 2. World-space UI (health bars, etc.)
    Renderer::Primitives::BeginFrame();
    for (auto& entity : m_visibleEntities) {
        RenderHealthBar(entity);     // Uses primitives directly
    }
    Renderer::Primitives::EndFrame();

    // 3. Screen-space UI (RmlUI)
    m_uiSystem->Update(dt);
    m_uiSystem->Render();            // RmlUI backend uses primitives

    m_renderer->Present();
}
```

## Answers to Your Question

**"Would we use RmlUI all the way down to tiles?"**
- ❌ **No** - Tiles are game world, use custom rendering

**"Are RmlUI just for UI panes?"**
- ✅ **Yes** - RmlUI for screen-space panels (menus, inventory)
- ❌ **Not for** - World rendering, world-space UI

**"Two different APIs for rectangles?"**
- ✅ **Solution:** Unified primitive layer
- RmlUI backend uses primitives
- Game uses same primitives
- **One way to draw**, multiple high-level APIs on top

## Recommendation

**Build it in this order:**

1. **Primitive rendering API** (1 week)
   - DrawRect, DrawText, DrawTexture
   - Batching system
   - Integration with existing renderer

2. **Game world rendering** (uses primitives or vector system)
   - Tiles
   - Entities
   - Effects

3. **World-space UI** (uses primitives)
   - Health bars
   - Floating text
   - Selection boxes

4. **RmlUI integration** (2 weeks)
   - Backend uses primitives
   - Screen-space panels only
   - Inventory, menus, dialogs

**Result:**
- ✅ One primitive API (solves your concern)
- ✅ RmlUI isolated to screen-space panels
- ✅ Game rendering optimized and integrated
- ✅ Clear usage boundaries

## Open Questions

1. **Where does the minimap live?**
   - Custom rendering (it's a view of the game world)
   - Not RmlUI (performance reasons)

2. **Rich tooltips (with images, formatting)?**
   - RmlUI if truly rich
   - Primitives if simple text

3. **Building placement preview (ghost)?**
   - Game rendering (it's in the world)
   - Not RmlUI

4. **Action button hotbar?**
   - Could be either (preference decision)
   - RmlUI if you want CSS styling
   - Primitives if you want tight control

Want me to create the primitive rendering API design as the next document?
