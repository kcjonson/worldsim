## Development Log

### 2025-12-01 - Lua Scripting for Procedural Asset Generation

**Summary:**
Implemented Phase 2 of the Asset System: Lua scripting for procedural asset generation. Created a deciduous tree generator demonstrating 3/4 top-down view (Rimworld style) with extensive visual variation across 40 unique trees.

**What Was Accomplished:**
- Integrated sol2 (Lua 5.4.8) into vcpkg dependencies
- Created `LuaEngine` class for script execution with seeded randomness
- Created `LuaGenerator` implementing `IAssetGenerator` interface
- Exposed Path API to Lua (`Path:new()`, `path:addVertex()`, `path:setColor()`, `path:close()`, `asset:addPath()`)
- Created deciduous.lua tree generator with:
  - Canopy with variable layers (1-3 back layers, 1-3 front clusters)
  - Tapered trunk with dark edge highlights
  - Branches from various positions on trunk (0-70% down)
  - Shadow layer for depth
  - Highlight blob for lighting
  - Multiplicative variation (60-140%) for distinct trees
  - `canopyStretch` factor for tall vs wide trees
- Created TreeScene demo rendering 40 trees in 8x5 grid

**Files Created:**
- `libs/engine/assets/lua/LuaEngine.h/.cpp` - Lua script executor
- `libs/engine/assets/lua/LuaGenerator.h/.cpp` - IAssetGenerator implementation
- `assets/generators/trees/deciduous.lua` - Procedural tree generator
- `assets/definitions/trees/tree.xml` - Asset definition with scriptPath
- `apps/ui-sandbox/scenes/TreeScene.cpp` - Demo scene

**Files Modified:**
- `vcpkg.json` - Added sol2 dependency
- `libs/engine/CMakeLists.txt` - Added Lua library linking
- `libs/engine/assets/AssetRegistry.cpp` - scriptPath support in XML parser
- `libs/engine/assets/GeneratorRegistry.cpp` - LuaGenerator registration

**Technical Decisions:**
1. **sol2 over LuaJIT**: sol2 provides cleaner C++ bindings with type safety; Lua 5.4 is sufficient for our needs
2. **Seed-based randomness**: C++ seeds Lua's `math.randomseed()` ensuring deterministic generation from same seed
3. **Path-based API**: Lua scripts build `VectorPath` objects via `Path` usertype, mirroring C++ `IAssetGenerator` output
4. **Multiplicative variation**: Using `* (0.6 + random * 0.8)` instead of additive offsets creates more visually distinct trees
5. **Draw order layering**: Back canopy → branches → front canopy ensures branches are visible but partially covered for depth

**Lessons Learned:**
- Coordinate system confusion (-Y is "up" in 3/4 view) caused branch disconnection - fixed by understanding canopy vs trunk positioning
- Branches covered by canopy layers - fixed by interleaving draw order
- Additive variation (±20%) was too subtle for visual distinction - multiplicative (60-140%) works better

**PR:** https://github.com/kcjonson/worldsim/pull/41

**Next Steps:**
- Phase 3: Variant caching (binary cache for pre-generated variants)
- Phase 4: Full tree demo (Weber & Penn branching, mixed flora scene)

---

### 2025-11-30 - Asset System Architecture Design

**Summary:**
Designed comprehensive asset system architecture supporting both simple (designer-authored SVG) and procedural (Lua-generated) assets with moddability as a core requirement. This addresses the broader question of how game assets should be structured on disk.

**Design Decisions:**

1. **Simple vs Procedural Assets**
   - **Simple**: Hand-drawn SVG files (flowers, mushrooms, rocks). Load once, tessellate, render via GPU instancing with per-instance variation (color, scale, rotation).
   - **Procedural**: Lua script + parameters that generate vector graphics algorithmically (trees, bushes). Pre-generate N variants at load time, cache to disk.

2. **Data-Driven Definitions (inspired by RimWorld)**
   - XML asset definitions separate data from logic
   - Definitions support inheritance (`ParentDef`) for DRY
   - Later-loaded definitions override earlier ones (mod support)
   - Example: `<defName>Tree_Oak</defName>` with `<generator><script>deciduous.lua</script></generator>`

3. **Lua Scripting for Procedural Generation**
   - Lua chosen for: embeddable (~200KB), sandboxable, fast (LuaJIT), industry standard
   - Scripts define `generate(params, rng)` function returning `VectorAsset`
   - API exposes: `VectorAsset`, `VectorPath`, `Color`, `Vec2`, `Math`, seeded RNG
   - Sandbox restricts file I/O, OS access, network for mod safety

4. **Pre-Generation Strategy**
   - Procedural assets generate N variants at load time (e.g., 200 unique oaks)
   - Variants cached to binary files for fast reload
   - Variant selection by position seed ensures determinism
   - Zero generation cost during gameplay

**Research Findings:**
- RimWorld's Def system proves highly moddable and extensible
- Weber & Penn algorithm (SpeedTree) preferred for tree branching over L-systems
- Similar patterns applicable to: terrain features, buildings, weather effects, creatures, items

**Files Created:**
- `docs/technical/asset-system/README.md` - Architecture overview
- `docs/technical/asset-system/asset-definitions.md` - XML schema, inheritance, modding
- `docs/technical/asset-system/lua-scripting-api.md` - Lua API reference with examples

**Files Modified:**
- `docs/technical/INDEX.md` - Added Asset System section
- `docs/status.md` - Added Asset System Architecture epic

**Relationship to Animation Performance:**
This work complements the animation performance epic. The Asset System handles **what** assets exist and how they're generated, while the tiered renderer handles **how** they're rendered efficiently. Pre-generation at load time means procedural trees don't need runtime tessellation - they select from cached variants.

**Next Steps:**
Phase 1 implementation: Asset Registry, XML parser, simple asset loader, renderer integration.

---

### 2025-11-30 - Animated Grass NO-GO: CPU Tessellation Performance Analysis

**Summary:**
10,000 animated grass blades with per-frame Bezier retessellation achieved only **12 FPS** (target: 60 FPS). This is a critical NO-GO for naive CPU tessellation at scale. Created comprehensive optimization plan with tiered animation system.

**Benchmark Results:**
| Metric | Result | Target | Status |
|--------|--------|--------|--------|
| FPS | 12 | 60 | **4x under** |
| Frame time | ~83ms | 16.67ms | **5x over** |
| Tessellation | ~65ms | <10ms | **6x over** |
| Blades | 10,000 | 10,000 | OK |
| Triangles | 24,832 | - | OK |

**Bottleneck Analysis:**
- **Bezier flattening (De Casteljau)**: ~50ms (77% of frame)
- **Ear clipping tessellation**: ~10ms (15% of frame)
- **Buffer building/rendering**: ~5ms (8% of frame)

**GO/NO-GO Decision:** NO-GO for naive CPU tessellation.

**Research Conducted:**
Explored three optimization approaches:
1. **GPU Instancing + Vertex Shader Animation** - Pre-tessellate once, animate in vertex shader. Industry standard (Zelda BotW, Genshin Impact). Works for simple parametric deformation.
2. **CPU Optimization Stack** - Arena allocators (5-10%), temporal coherence (2-4x), SIMD Bezier (2-3x). Combined can achieve 45-60 FPS.
3. **Tiered System** - Combine both: GPU instancing for simple flora, optimized CPU for complex flora.

**Key Insight:** User clarified this isn't just about grass - the system must support complex vector flora (trees, bushes, procedural assets) with true Bezier curve deformations. This means CPU tessellation must remain viable for complex assets even while GPU instancing handles simple repeated objects.

**Chosen Architecture: Tiered Animation System**
- **Tier 1 (GPU Instancing)**: Simple flora with parametric deformation (grass, small plants). Target: 100,000+ instances @ 60 FPS.
- **Tier 2 (Optimized CPU)**: Complex flora with true Bezier curves (trees, bushes, procedural). Target: 1,000-2,000 assets @ 60 FPS.
- **Tier 3 (Hybrid)**: Runtime tier selection based on asset complexity and distance.

**Files Created:**
- `docs/technical/vector-graphics/animation-performance.md` - Technical spec for optimization epic

**Files Modified:**
- `docs/status.md` - Added Animation Performance Optimization epic, marked Grass Validation complete

**Next Steps:**
Phase 1 (CPU Optimization) first to validate Bezier deformation can hit 60 FPS:
1. Arena allocator integration for Bezier/Tessellator
2. Temporal coherence system (skip unchanged blades)
3. SIMD Bezier flattening (ARM NEON)

---

### 2025-11-30 - Uber Shader Epic Complete

**Summary:**
Marked the Uber Shader - Unified Rendering Pipeline epic as complete. This epic merged shape and text rendering into a single shader, eliminating shader switches and enabling correct z-ordering.

**What Was Accomplished:**
- Verified uber.vert and uber.frag shaders exist with unified vertex format
- Verified BatchRenderer has addTextQuad() method and unified UberVertex struct
- Verified Text::Render() calls batchRenderer->addTextQuad() directly
- Confirmed TextBatchRenderer and msdf_text shaders were deleted
- All performance goals achieved: zero shader switches, single draw call, correct z-ordering

**Key Files:**
- `libs/renderer/shaders/uber.vert` - Unified vertex shader (6 attributes)
- `libs/renderer/shaders/uber.frag` - Fragment shader with renderMode branching
- `libs/renderer/primitives/BatchRenderer.h` - Unified batch renderer with addTextQuad()
- `libs/ui/shapes/Shapes.cpp` - Text::Render() using BatchRenderer directly

---

### 2025-11-30 - Archived: Vector Graphics Validation - Stars

**Summary:**
Archived completed epic from Recently Completed section. This epic validated the vector graphics rendering pipeline with star shapes.

**What Was Accomplished:**
- Single Hardcoded Star: Basic tessellation, rendering, visual quality verification
- 10,000 Static Stars: Instance generation, batching system, 60 FPS verified
- 10,000 Animated Stars: Rotation animation, 60 FPS sustained
- SVG Loading for Stars: SVG file loading, path parsing, identical performance verified

**Result:** Stars validated! 10,000 animated stars @ 60 FPS achieved

**Spec/Documentation:** `/docs/technical/vector-graphics/validation-plan.md`

---

### 2025-11-30 - Archived: Core Engine Patterns Implementation

**Summary:**
Archived completed epic from Recently Completed section. This epic established foundational engine patterns.

**What Was Accomplished:**
- String Hashing System: FNV-1a hash, HASH() macro, collision detection, common constants
- Structured Logging System: Logger class, 4 log levels, macros, ANSI colors, debug server integration
- Memory Arena Allocators: Arena class, FrameArena, ScopedArena RAII, 14× faster than malloc
- Resource Handle System: ResourceHandle type, ResourceManager template, generation validation
- Application Class: Unified game loop, HandleInput() phase, delta time, exception handling

**Spec/Documentation:** `/docs/technical/` (multiple docs)

---

### 2025-11-29 - Shader-Based Rect Clipping (Phase 1 Complete)

**Summary:**
Implemented Phase 1 of the clipping system using shader-based rect clipping. This approach tests per-fragment whether it falls within the clip bounds and discards it if outside, avoiding costly GPU state changes from glScissor.

**What Was Accomplished:**
- Created clip_types.h with ClipShape (ClipRect, ClipRoundedRect, ClipCircle, ClipPath), ClipMode, ClipSettings
- Added clipBounds (vec4) to UberVertex struct in batch_renderer.h
- Updated VAO setup with new vertex attribute for clipBounds
- Added clip test to uber.frag shader (discard if outside bounds)
- Implemented PushClip/PopClip stack in primitives.cpp
- Fixed DPI scaling issue on Retina displays (logical→physical pixel conversion)
- Created clip_scene.cpp demo with text clipping demonstration
- Modified vector_perf_scene.cpp with 'C' key toggle for clipping performance testing

**Files Created:**
- `libs/renderer/graphics/clip_types.h` - Clipping type definitions
- `apps/ui-sandbox/scenes/clip_scene.cpp` - Clipping demo scene

**Files Modified:**
- `libs/renderer/primitives/batch_renderer.h` - Added clipBounds to UberVertex
- `libs/renderer/primitives/batch_renderer.cpp` - Updated VAO, SetClipBounds/ClearClipBounds
- `libs/renderer/primitives/primitives.cpp` - PushClip/PopClip implementation, DrawText stub
- `libs/renderer/primitives/primitives.h` - PushClip/PopClip declarations
- `libs/renderer/shaders/uber.vert` - Pass-through clipBounds attribute
- `libs/renderer/shaders/uber.frag` - Fragment discard based on clip test
- `apps/ui-sandbox/scenes/vector_perf_scene.cpp` - Added 'C' key toggle

**Key Technical Details:**

**1. Shader-Based Clipping (Zero GPU State Changes)**
```glsl
// uber.frag - Per-fragment clip test
if (clipBounds.z > 0.0 && clipBounds.w > 0.0) {
    if (gl_FragCoord.x < clipBounds.x || gl_FragCoord.x > clipBounds.x + clipBounds.z ||
        gl_FragCoord.y < clipBounds.y || gl_FragCoord.y > clipBounds.y + clipBounds.w) {
        discard;
    }
}
```

**2. DPI Scaling Fix (Retina Displays)**
The shader uses gl_FragCoord which operates in physical pixels, but the API uses logical pixels. Fixed by scaling clip bounds when setting:
```cpp
void BatchRenderer::SetClipBounds(float x, float y, float w, float h) {
    float dpiScale = static_cast<float>(m_framebufferHeight) / static_cast<float>(m_windowHeight);
    m_currentClipBounds = glm::vec4(x * dpiScale, y * dpiScale, w * dpiScale, h * dpiScale);
}
```

**3. Nested Clipping via Intersection**
PushClip intersects the new clip with the current clip to support nested clips:
```cpp
// Intersect with current clip for nested clipping
if (!m_clipStack.empty()) {
    // Calculate intersection of current and new clip
    float minX = std::max(currentClip.x, newClip.x);
    // ... intersection math
}
```

**4. Primitives::DrawText Dependency Issue**
Discovered that `Primitives::DrawText` cannot be implemented in the renderer library because `FontRenderer` lives in the ui library. This would create a circular dependency (renderer→ui→renderer).

**Options identified:**
1. Move FontRenderer to renderer library (cleanest, significant refactor)
2. Add PRIVATE include path (include-only, no link dependency)
3. Create abstract IGlyphGenerator interface in renderer, implement in ui

**Current solution:** Use `UI::Text` component for text rendering. DrawText remains a documented stub.

**Lessons Learned:**
- Shader-based clipping avoids GPU state machine overhead (glScissor requires flush)
- DPI handling is critical - gl_FragCoord always uses physical pixels
- Library dependency architecture matters: renderer→ui is the correct direction, not the reverse

**Next Steps:**
- Phase 2: Add clip and contentOffset properties to Container for scrolling
- Future: Implement ClipRoundedRect, ClipCircle in shader

---

### 2025-11-26 - UI Component Architecture Documentation

**Summary:**
Created comprehensive architecture documentation for the UI framework's unified layer model, virtual interfaces, and handle-based references. This establishes the foundational design patterns for the UI system.

**What Was Accomplished:**
- Created `/docs/technical/ui-framework/architecture.md` documenting the unified layer model
- Defined `ILayer` interface (HandleInput/Update/Render lifecycle) in `/libs/ui/layer/layer.h`
- Defined `IFocusable` interface for keyboard focus management
- Created `LayerHandle` type (index + generation) for safe layer references
- Added lifecycle methods (HandleInput/Update) to all shape types
- Added interface implementations to Button and TextInput

**Files Created:**
- `libs/ui/layer/layer.h` - ILayer and IFocusable interfaces, LayerHandle type

**Files Modified:**
- `libs/ui/shapes/shapes.h` - Added HandleInput()/Update() to all shapes
- `libs/ui/components/button/button.h` - Added ILayer and IFocusable interface implementations
- `libs/ui/components/text_input/text_input.h` - Added ILayer and IFocusable interface implementations
- `docs/technical/ui-framework/INDEX.md` - Added architecture.md to Core Architecture section
- `docs/technical/INDEX.md` - Updated architecture.md entry (no longer planned)

**Key Design Decisions:**

1. **Unified Layer Model**: Everything is a Layer (shapes + components in one hierarchy). No separate managers for primitives vs widgets.

2. **Virtual Interfaces**: Explicit interface inheritance (IComponent, ILayer, IFocusable) for clear type relationships and runtime polymorphism via vtables.

3. **Handle-Based References**: Uses LayerHandle pattern (16-bit index + 16-bit generation) for safe layer references. Prevents dangling pointers when layers are removed.

4. **FocusManager with IFocusable**: Uses IFocusable interface for runtime dispatch to focusable components.

**Lessons Learned:**
- Virtual interfaces provide clarity at the cost of minimal vtable overhead
- Interface inheritance makes class relationships immediately visible in declarations
- Generation tracking in handles prevents stale reference issues

**Next Steps:**
- Future optimization: Add generation tracking to LayerManager for stale handle detection

---

### 2025-11-26 - Completed TextInput Component & Focus Management (PR #30)

**Summary:**
Completed the TextInput component with full keyboard focus management, text selection, and clipboard support. This completes Phase 1 of UI Components in the Colonysim UI Integration epic.

**What Was Accomplished:**
- Implemented TextInput component with cursor navigation, text editing, and UTF-8 support
- Created FocusManager for global Tab navigation between focusable components
- Added text selection (Shift+Arrow, Shift+Click, double-click word select, mouse drag)
- Integrated clipboard operations (Ctrl+C/X/V/A) via ClipboardManager abstraction
- Added key repeat support for held keys
- Refactored to use FocusManager singleton pattern (components auto-register)

**Files Created:**
- `libs/ui/components/text_input/text_input.h` - TextInput component header
- `libs/ui/components/text_input/text_input.cpp` - TextInput implementation
- `libs/ui/focus/focus_manager.h` - Focus management system header
- `libs/ui/focus/focus_manager.cpp` - FocusManager implementation
- `libs/engine/clipboard/clipboard_manager.h` - Clipboard abstraction header
- `libs/engine/clipboard/clipboard_manager.cpp` - ClipboardManager implementation
- `apps/ui-sandbox/scenes/text_input_scene.cpp` - TextInput demo scene

**Files Modified:**
- `libs/ui/components/button/button.h` - Added IFocusable interface, removed focusManager from Args
- `libs/ui/components/button/button.cpp` - Implemented focus handling, uses FocusManager singleton
- `libs/engine/input/input_manager.h` - Added key repeat tracking
- `libs/engine/input/input_manager.cpp` - Implemented key repeat with delay/interval
- `libs/engine/application/application.cpp` - Owns FocusManager and ClipboardManager instances

**Technical Details:**

**1. FocusManager Singleton Pattern**
Components register with the global FocusManager singleton automatically:
```cpp
// In constructor
FocusManager::Get().RegisterFocusable(this, m_tabIndex);

// In destructor
FocusManager::Get().UnregisterFocusable(this);
```
TabIndex of -1 means auto-assign incrementing values. Equal tabIndex values use stable_sort to preserve registration order.

**2. Key Repeat System**
Added to InputManager with configurable delay (500ms) and interval (50ms):
```cpp
void InputManager::Update() {
    // Check for key repeat on held keys
    for (auto& [key, info] : m_keyStates) {
        if (info.pressed && info.holdDuration > m_keyRepeatDelay) {
            // Fire repeat events at m_keyRepeatInterval
        }
    }
}
```

**3. Ghost Selection Fix**
Fixed issue where mouse-down state wasn't cleared on focus lost, causing phantom selections:
```cpp
void TextInput::OnFocusLost() {
    m_focused = false;
    m_mouseDown = false; // Prevents ghost selections
    ClearSelection();
}
```

**PR History:**
- PR #30: TextInput component with focus management, selection, and clipboard support (merged)

**Testing Performed:**
- Tab navigation cycles through all focusable components correctly
- Text selection via keyboard (Shift+Arrow) and mouse (drag, shift-click)
- Clipboard operations (copy, cut, paste, select all)
- Key repeat fires correctly for held keys
- 77 unit tests passing

---

### 2025-11-25 - Completed SDF Text Rendering Epic (Phase 5 + Polish)

**Summary:**
Completed the SDF Text Rendering & Batched Command Queue epic by finishing Phase 5 (Integration & Testing) and applying several rounds of polish and bug fixes. Text now renders correctly with proper z-ordering, alignment, and batching. All UI components in ui-sandbox now use the unified Text/Button component system.

**What Was Accomplished:**
- Fixed z-ordering bug: Text now correctly batches with shapes and renders on top of buttons
- Implemented comprehensive text alignment system (horizontal + vertical, point and bounding box modes)
- Added LRU glyph quad cache for performance optimization (PR #28)
- Fixed coordinate system unification between text and shapes
- Converted all UI components to use Text/Button (navigation menu, buttons, etc.)
- Fixed FontRenderer lifecycle issues (per-scene initialization bug)
- Added text alignment demonstration scene with visual testing grids

**Files Created:**
- `libs/ui/font/font_cache.h` - LRU cache for glyph quads
- `apps/ui-sandbox/scenes/text_shapes_scene.cpp` - Text alignment demonstration

**Files Modified:**
- `libs/ui/shapes/shapes.h` - Added alignment properties (hAlign, vAlign, alignMode)
- `libs/ui/shapes/shapes.cpp` - Implemented alignment calculations for text
- `libs/ui/font/font_renderer.cpp` - MeasureText now supports SDF fonts, fixed scaling
- `libs/ui/font/font_renderer.h` - Added GetGlyphQuad() with caching
- `libs/ui/layer/layer_manager.cpp` - Removed premature text flush (fixed z-ordering)
- `apps/ui-sandbox/main.cpp` - Global FontRenderer initialization, navigation menu uses Button/Text
- `apps/ui-sandbox/scenes/button_scene.cpp` - Uses global FontRenderer
- `apps/ui-sandbox/scenes/layer_scene.cpp` - Removed per-scene FontRenderer (fixed bug)
- `libs/engine/application/application.cpp` - Integrated text batch renderer lifecycle

**Technical Details:**

**1. Z-Ordering Fix**
The critical bug where text rendered behind buttons was caused by premature flushing in LayerManager::RenderLayers(). Removed the early text flush and relied on the overlay renderer's proper batching order.

Before:
```cpp
// BUG: This caused text to render before shapes
textBatchRenderer->Flush();
primitives->FlushBatches();
```

After:
```cpp
// Text batches with shapes, overlay renderer handles flush order
primitives->FlushBatches();  // Includes text via TextBatchRenderer
```

**2. Text Alignment System**
Implemented dual-mode alignment system:

**Point Mode** (alignMode = Point):
- Text aligns relative to an origin point
- hAlign: Left/Center/Right positions text horizontally around point
- vAlign: Top/Middle/Bottom/Baseline positions text vertically around point
- Use case: Labels, tooltips, floating text

**Bounding Box Mode** (alignMode = BoundingBox):
- Text aligns within a defined rectangular area
- hAlign: Left/Center/Right within box width
- vAlign: Top/Middle/Bottom within box height
- Use case: Buttons, panels, centered titles

**3. LRU Glyph Quad Cache (PR #28)**
Added performance optimization to avoid regenerating glyph quads on every frame:
```cpp
struct GlyphQuadCacheEntry {
    uint32_t character;
    float fontSize;
    float x, y;
    std::vector<PrimitiveVertex> vertices;
    std::vector<uint32_t> indices;
};
```
Cache hit rate: ~99% for typical UI rendering (buttons re-render same text)

**4. Coordinate System Unification**
Fixed font size calculation to use BASE_FONT_SIZE (16px) consistently:
```cpp
// Before: Used glyph size (32px) - WRONG
float fontSize = static_cast<float>(m_sdfGlyphSize);

// After: Use base font size (16px) - CORRECT
const float BASE_FONT_SIZE = 16.0f;
float fontSize = BASE_FONT_SIZE;
```

**5. FontRenderer Lifecycle Fix**
Fixed bug where layer_scene was creating its own FontRenderer instance, overwriting the global instance and causing navigation menu text to disappear. FontRenderer and TextBatchRenderer are now initialized once in main.cpp and shared across all scenes.

**PR History:**
- PR #28: Text rendering cache optimization
- PR #29: Text alignment improvements and coordinate system fixes

**Testing Performed:**
- Button scene: Text renders correctly on top of buttons
- Text shapes scene: Alignment grids verify all 12 alignment combinations (3 hAlign × 4 vAlign)
- Navigation menu: Uses Button components, text persists across scene changes
- Multiple scale testing: Text crisp at all sizes (tested 8px to 144px)
- Performance: Single draw call batching verified via debug output

**Lessons Learned:**
1. **Global vs per-scene resources**: Resources like FontRenderer should be initialized once and shared, not created per-scene
2. **Coordinate system consistency**: Using the same base units (logical pixels) for text and shapes prevents scaling bugs
3. **Caching strategy**: LRU cache with proper invalidation is effective for UI text rendering
4. **Alignment modes**: Dual-mode alignment (point vs bounding box) covers all UI text use cases

**Next Steps:**
The SDF Text Rendering epic is now complete. Remaining UI work:
- Keyboard Focus Manager (global Tab navigation)
- TextInput component (cursor, text editing)
- Additional UI components as needed

---

### 2025-11-20 - Fixed SDF Text Rendering Horizontal Squishing

**Summary:**
Fixed critical bug in SDF text rendering where glyphs were horizontally compressed, making narrow letters like 'I' and 'i' barely visible. The issue was that we were sampling the full 32×32 pixel atlas cell for each glyph instead of only sampling the actual glyph content within that cell. Implemented atlasBounds support following the official msdf-atlas-gen approach.

**Root Cause:**
The atlas generator allocated 32×32 pixel cells for each glyph but glyphs varied in actual size (e.g., 'I' is ~3 pixels wide, 'W' is ~20 pixels wide). The renderer was using the full cell UV coordinates, causing the shader to stretch narrow glyphs across the entire cell width, distorting their proportions.

**Files Modified:**
- `tools/generate_sdf_atlas/main.cpp` - Added atlasBounds calculation and export
- `libs/ui/font/font_renderer.h` - Added atlasBounds fields to SDFGlyph struct
- `libs/ui/font/font_renderer.cpp` - Read atlasBounds from JSON and use for UV coordinates
- `build/apps/ui-sandbox/fonts/Roboto-SDF.json` - Regenerated with atlasBounds data

**Technical Details:**

**1. Atlas Coordinate Types**
Following msdf-atlas-gen conventions, there are three coordinate systems:
- **atlas**: Full allocated cell (e.g., 32×32 pixels at x=0, y=0)
- **atlasBounds**: Actual glyph pixels within cell (e.g., 3×22 pixels at x=14, y=5)
- **plane**: Glyph positioning in EM units relative to baseline

**2. Generator Changes (`tools/generate_sdf_atlas/main.cpp`)**
Added atlasBounds calculation during generation:
```cpp
// Calculate actual glyph bounds within the atlas cell (in pixels)
// Apply transformation to plane bounds to get pixel coordinates
glyph.atlasBoundsLeft = (glyph.planeLeft + translateX) * uniformScale;
glyph.atlasBoundsBottom = (glyph.planeBottom + translateY) * uniformScale;
glyph.atlasBoundsRight = (glyph.planeRight + translateX) * uniformScale;
glyph.atlasBoundsTop = (glyph.planeTop + translateY) * uniformScale;
```

JSON export includes both cell and content bounds:
```json
"I": {
  "atlas": {"x": 0.5, "y": 0, "width": 0.0625, "height": 0.0625},
  "atlasBounds": {"left": 0.528, "bottom": 0.009, "right": 0.534, "top": 0.053},
  "plane": {"left": 0.089, "bottom": 0, "right": 0.184, "top": 0.711}
}
```

**3. Loader Changes (`libs/ui/font/font_renderer.cpp`)**
Added backward-compatible atlasBounds loading:
```cpp
if (glyphJson.contains("atlasBounds") && !glyphJson["atlasBounds"].is_null()) {
    glyph.atlasBoundsMin.x = glyphJson["atlasBounds"]["left"].get<float>();
    // ... read other bounds
} else {
    // Fallback: use full atlas cell if atlasBounds not available
    glyph.atlasBoundsMin = glyph.atlasUVMin;
    glyph.atlasBoundsMax = glyph.atlasUVMax;
}
```

**4. Renderer Changes**
Changed UV coordinates to use atlasBounds:
```cpp
quad.uvMin = glyph.atlasBoundsMin;  // Was: glyph.atlasUVMin
quad.uvMax = glyph.atlasBoundsMax;  // Was: glyph.atlasUVMax
```

**Key Learnings:**
1. **Uniform scaling is essential** - All glyphs must use the same pixels-per-EM scale for correct coordinate mapping
2. **Cell vs content distinction** - Atlas generators allocate fixed cells but must track actual glyph content bounds
3. **Research first** - Reading official msdf-atlas-gen documentation and source revealed the correct approach immediately
4. **Coordinate system complexity** - Three separate coordinate systems (atlas cells, atlas bounds, plane bounds) each serve a specific purpose

**References:**
- https://github.com/Chlumsky/msdf-atlas-gen/issues/2 (atlasBounds vs atlas)
- https://github.com/Chlumsky/msdf-atlas-gen/discussions/17 (plane bounds)
- https://github.com/Chlumsky/msdf-atlas-gen/discussions/47 (uniform scaling)

### 2025-11-18 - Shader Loading System Unified

**Summary:**
Consolidated two separate shader loading systems into a single unified RAII-based approach. Moved all shaders to centralized location (`libs/renderer/shaders/`) and updated both FontRenderer and BatchRenderer to use the same `Renderer::Shader` class. Eliminated ~120 lines of duplicate shader compilation code and inline shader strings from BatchRenderer.

**Files Created:**
- `libs/renderer/shaders/primitive.vert` - Primitive vertex shader (extracted from inline code)
- `libs/renderer/shaders/primitive.frag` - Primitive fragment shader (extracted from inline code)

**Files Moved:**
- `/shaders/text.vert` → `libs/renderer/shaders/text.vert` - Text vertex shader
- `/shaders/text.frag` → `libs/renderer/shaders/text.frag` - Text fragment shader

**Files Modified:**
- `libs/renderer/CMakeLists.txt` - Added POST_BUILD command to copy shaders to build directory
- `apps/ui-sandbox/CMakeLists.txt` - Added POST_BUILD command to copy shaders to executable directory
- `libs/renderer/primitives/batch_renderer.h` - Changed from `GLuint m_shader` to `Shader m_shader`, removed CompileShader() declaration
- `libs/renderer/primitives/batch_renderer.cpp` - Removed ~70 lines of inline shader source code, removed ~50 lines in CompileShader() method, updated Init/Shutdown/Flush to use Shader class API

**Key Implementation Details:**

**1. RAII Pattern Benefits**
The existing `Renderer::Shader` class uses RAII (Resource Acquisition Is Initialization) for automatic resource cleanup:
```cpp
// Before (BatchRenderer): Manual cleanup required
GLuint m_shader;
void Init() {
    m_shader = CompileShader();  // ~50 lines of manual GL calls
}
void Shutdown() {
    glDeleteProgram(m_shader);  // Easy to forget!
}

// After: Automatic cleanup via RAII
Shader m_shader;
void Init() {
    m_shader.LoadFromFile("primitive.vert", "primitive.frag");
}
void Shutdown() {
    // Shader destructor automatically calls glDeleteProgram
}
```

**2. Centralized Shader Storage**
All shaders now live in one location with automated copying:
```cmake
# libs/renderer/CMakeLists.txt - copies to build/shaders/
add_custom_command(TARGET renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    ${CMAKE_BINARY_DIR}/shaders
)

# apps/ui-sandbox/CMakeLists.txt - copies to executable directory
add_custom_command(TARGET ui-sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:ui-sandbox>/shaders
)
```

**3. Shader Loading API**
Both FontRenderer and BatchRenderer now use identical loading pattern:
```cpp
// FontRenderer (existing, unchanged)
m_shader.LoadFromFile("text.vert", "text.frag");

// BatchRenderer (newly updated)
m_shader.LoadFromFile("primitive.vert", "primitive.frag");
```

**Architecture Decisions:**

**Why RAII Over Manual Cleanup:**
- Exception safety: Automatic cleanup even when exceptions occur
- Resource leak prevention: Impossible to forget cleanup
- Clear ownership semantics: Shader class owns the GPU resource
- Move semantics support: Compatible with future resource manager pattern
- No performance overhead: Zero-cost abstraction

**Why Centralized Shader Location:**
- Single source of truth for all GLSL code
- Easier to find and modify shaders
- Consistent build process across all executables
- Simpler relative path resolution (always `shaders/filename`)

**Future Compatibility:**
This RAII pattern is compatible with centralized resource management for splash screen loading:
```cpp
class ResourceManager {
    std::unordered_map<std::string, Shader> m_shaders;

    void LoadDuringSplashScreen() {
        Shader primitiveShader;
        primitiveShader.LoadFromFile("primitive.vert", "primitive.frag");
        m_shaders["primitive"] = std::move(primitiveShader);  // Transfer ownership
        UpdateProgress(33);
    }
};
```

**Testing:**
- Verified primitive rendering (shapes scene) works correctly
- Verified text rendering (font_test scene) works correctly
- Both systems load shaders from centralized location successfully

**Lessons Learned:**
- Shader loading requires proper CMake commands to copy files to executable directory, not just build directory
- RAII eliminates entire classes of bugs (resource leaks, double-frees)
- Consolidating duplicate code early prevents divergence and maintenance burden

**Next Steps:**
None required - shader loading is now unified across the project.

---

### 2025-11-18 - InputManager Implementation Complete

**Summary:**
Ported InputManager from colonysim to worldsim with architectural adaptations. Created application-owned singleton for managing all input state (mouse, keyboard, scroll, drag detection). Removed Camera and GameState dependencies, making InputManager a pure state-tracking system. Integrated with Application lifecycle (Update called before Scene::HandleInput). Created InputTestScene for testing and demonstration.

**Files Created:**
- `libs/engine/input/input_manager.h` - InputManager interface with full input query API
- `libs/engine/input/input_manager.cpp` - Implementation with GLFW callback registration and state transitions
- `apps/ui-sandbox/scenes/input_test_scene.cpp` - Test scene displaying real-time input state

**Files Modified:**
- `libs/engine/application/application.h` - Added InputManager member, forward declaration, explicit destructor
- `libs/engine/application/application.cpp` - Create InputManager in constructor, call Update() in main loop
- `libs/engine/CMakeLists.txt` - Added input/input_manager.cpp to engine library sources
- `apps/ui-sandbox/CMakeLists.txt` - Added scenes/input_test_scene.cpp to ui-sandbox sources

**Key Implementation Details:**

**1. Application-Owned Singleton Pattern**
Matches SceneManager architecture for consistent patterns:
```cpp
class InputManager {
public:
    static InputManager& Get();
    static void SetInstance(InputManager* instance);

    explicit InputManager(GLFWwindow* window);
    // ...
private:
    static InputManager* s_instance;
};

// Application owns the instance
Application::Application(GLFWwindow* window) {
    m_inputManager = std::make_unique<InputManager>(m_window);
    InputManager::SetInstance(m_inputManager.get());
}
```

**2. GLFW Callback Integration**
Static callbacks route to instance methods via singleton:
```cpp
// Static callbacks registered with GLFW
static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (s_instance) {
        s_instance->HandleKeyEvent(key, scancode, action, mods);
    }
}

// Constructor registers all callbacks
InputManager::InputManager(GLFWwindow* window) : m_window(window) {
    glfwSetKeyCallback(m_window, KeyCallback);
    glfwSetMouseButtonCallback(m_window, MouseButtonCallback);
    glfwSetCursorPosCallback(m_window, CursorPosCallback);
    glfwSetScrollCallback(m_window, ScrollCallback);
    glfwSetCursorEnterCallback(m_window, CursorEnterCallback);
}
```

**3. Button State Transitions**
Distinguishes between Down (held), Pressed (first frame), and Released (last frame):
```cpp
enum class ButtonState { Up, Pressed, Down, Released };

void InputManager::Update(float deltaTime) {
    // Calculate mouse delta
    m_mouseDelta = m_mousePosition - m_lastMousePosition;
    m_lastMousePosition = m_mousePosition;

    // Update button state transitions
    // Pressed → Down, Released → Up
    for (auto& [button, state] : m_mouseButtonStates) {
        if (state == ButtonState::Pressed) state = ButtonState::Down;
        else if (state == ButtonState::Released) state = ButtonState::Up;
    }

    // Reset per-frame state
    m_scrollDelta = 0.0f;
}
```

**4. Drag Detection**
Automatic drag tracking based on mouse button state:
```cpp
// In MouseButtonCallback
if (action == GLFW_PRESS && button == GLFW_MOUSE_BUTTON_LEFT) {
    m_isDragging = true;
    m_dragStartPos = m_mousePosition;
}
else if (action == GLFW_RELEASE && button == GLFW_MOUSE_BUTTON_LEFT) {
    m_isDragging = false;
}

// Query API
bool IsDragging() const { return m_isDragging; }
glm::vec2 GetDragStartPosition() const { return m_dragStartPos; }
glm::vec2 GetDragDelta() const { return m_mousePosition - m_dragStartPos; }
```

**5. InputTestScene - Real-time State Display**
Comprehensive test scene showing all input state with color coding:
- Mouse position and delta (white)
- Mouse button states (green when pressed)
- Drag state (yellow when dragging, shows start and delta)
- Scroll delta (yellow when scrolling)
- Cursor in window state (green=in, yellow=out)
- Keyboard states for 11 common keys (green=pressed, yellow=released, cyan=down)
- Text scale: 1.5F (24px) for readability on high-DPI displays

**Testing:**
- ✅ Built successfully, all libraries compiling
- ✅ Launched InputTestScene on port 8083
- ✅ Verified mouse position tracking
- ✅ Verified mouse button down/pressed/released states
- ✅ Verified drag detection and delta calculation
- ✅ Verified scroll wheel input
- ✅ Verified keyboard states (down/pressed/released)
- ✅ Verified cursor enter/leave detection
- ✅ Text readable at 1.5F scale (FontRenderer base: 1.0F = 16px)

**Technical Decisions:**

**Application-Owned Singleton vs Pure Singleton:**
- Chose application-owned singleton pattern to match SceneManager
- Application creates and owns InputManager via std::unique_ptr
- Static Get() provides global access for convenience
- Rationale: Explicit ownership, predictable lifetime, matches existing patterns

**No Camera Dependency:**
- User feedback: "Why would the input manager care about the camera at all?"
- InputManager only tracks raw input state (pixel coordinates from GLFW)
- CoordinateSystem helper available for any conversions needed by scenes
- Rationale: Separation of concerns, InputManager is pure input tracking

**No GameState Dependency:**
- Removed GameState dependency from colonysim version
- Debug info will be sent via HTTP dev server (deferred to future task)
- Rationale: InputManager doesn't need game state, cleaner architecture

**Integration Point - Application::Update():**
- InputManager::Update() called in Application main loop
- Called AFTER glfwPollEvents() (callbacks have fired)
- Called BEFORE Scene::HandleInput() (scenes see current frame state)
- Order: glfwPollEvents() → InputManager::Update() → Scene::HandleInput() → Scene::Update()
- Rationale: Ensures scenes always see consistent, up-to-date input state

**Incomplete Type and std::unique_ptr:**
- Application has forward declaration of InputManager
- std::unique_ptr destructor requires complete type
- Solution: Explicit destructor in .cpp file where InputManager is complete
- Rationale: Standard C++ pattern for pimpl idiom

**Lessons Learned:**

1. **Architectural Consistency:** Matching existing patterns (SceneManager singleton) makes the codebase more predictable and easier to understand

2. **User Feedback is Critical:** User immediately identified unnecessary Camera dependency - simpler architecture resulted from questioning assumptions

3. **std::unique_ptr with Forward Declarations:** Must define destructor in .cpp file where type is complete, even if destructor is empty

4. **Scene Registration:** Static initialization in anonymous namespace is elegant but requires adding .cpp file to CMakeLists.txt - easy to forget

5. **High-DPI Text Scaling:** 1.0F scale (16px base) is too small on retina displays - 1.5F-2.0F provides better readability

**Next Steps:**
- Add HTTP debug endpoint `/api/input/state` for remote input state inspection
- Write unit tests for InputManager (button state transitions, delta calculations)
- Continue with next component: Style System

---

### 2025-11-18 - Shape System Complete - Circle and Text Rendering

**Summary:**
Completed the Shape System by implementing proper Circle and Text rendering. Extended the Primitives API with DrawCircle() using tessellation, enhanced the style system with CircleStyle and TextStyle (with alignment options), and integrated FontRenderer for text rendering. All four shape types (Rectangle, Circle, Line, Text) now render correctly with full style support.

**Files Modified:**
- `libs/foundation/graphics/primitive_styles.h` - Added CircleStyle, TextStyle, HorizontalAlign, VerticalAlign enums
- `libs/renderer/primitives/primitives.h` - Added DrawCircle(), SetFontRenderer(), GetFontRenderer()
- `libs/renderer/primitives/primitives.cpp` - Implemented DrawCircle() with 64-segment tessellation
- `libs/ui/shapes/shapes.h` - Updated Circle and Text to use proper style structs
- `libs/ui/shapes/shapes.cpp` - Implemented Circle::Render() and Text::Render() with full rendering logic
- `apps/ui-sandbox/scenes/layer_scene.cpp` - Updated demo to showcase circles with borders and text with alignment

**Key Implementation Details:**

**1. Circle Rendering - Tessellation Approach**
Implemented DrawCircle() using triangle fan tessellation:
```cpp
// 64 segments for smooth circles
constexpr int segments = 64;
std::vector<Foundation::Vec2> vertices;  // Center + perimeter
std::vector<uint16_t> indices;           // Triangle fan indices

// Generate vertices in a circle
for (int i = 0; i < segments; ++i) {
    float angle = i * angleStep;
    vertices.emplace_back(
        center.x + radius * std::cos(angle),
        center.y + radius * std::sin(angle)
    );
}

// Draw via DrawTriangles()
```

**2. Text Rendering - FontRenderer Integration**
Text shapes get FontRenderer from Primitives API and handle alignment:
```cpp
void Text::Render() const {
    ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
    float scale = style.fontSize / 16.0F;  // Base 16px

    // Measure text for alignment calculations
    glm::vec2 textSize = fontRenderer->MeasureText(text, scale);

    // Calculate aligned position based on HorizontalAlign/VerticalAlign
    // ... alignment logic ...

    fontRenderer->RenderText(text, alignedPos, scale, color);
}
```

**3. Style System Enhancements**
Added complete style support matching colonysim:
```cpp
struct CircleStyle {
    Color fill = Color::White();
    std::optional<BorderStyle> border = std::nullopt;
};

struct TextStyle {
    Color color = Color::White();
    float fontSize = 16.0F;
    HorizontalAlign hAlign = HorizontalAlign::Left;  // Left/Center/Right
    VerticalAlign vAlign = VerticalAlign::Top;        // Top/Middle/Bottom
};
```

**4. Avoided Circular Dependency**
Initially tried to add DrawText() to Primitives API, but this would require `renderer` library to depend on `ui` library (for FontRenderer), creating a circular dependency since `ui` already depends on `renderer`. Solution: Primitives API provides Set/GetFontRenderer(), and Text::Render() gets the FontRenderer and calls it directly.

**Testing:**
- Built successfully with all libraries compiling
- Launched layer_scene and captured screenshot
- Verified all shapes rendering:
  - ✅ Rectangles with fills and borders
  - ✅ Circles with fills and borders (smooth tessellation)
  - ✅ Lines with custom widths
  - ✅ Text with font scaling and alignment
  - ✅ Nested layers and z-ordering
  - ✅ Sidebar with 5 buttons and centered text labels

**Technical Decisions:**

**Circle Tessellation vs Shader:**
- Chose CPU tessellation over shader-based circles
- Rationale: Consistent with vector graphics approach, reuses existing triangle rendering
- 64 segments provides smooth circles without noticeable faceting
- Trade-off: More vertices but simpler pipeline

**FontRenderer Architecture:**
- Scenes must create FontRenderer and set it via Primitives::SetFontRenderer()
- Text shapes retrieve it via GetFontRenderer()
- Design allows multiple scenes to use different fonts if needed
- Font must be in working directory (fonts/Roboto-Regular.ttf)

**Lessons Learned:**
1. **Watch for circular dependencies** - Adding features to low-level libraries (renderer) that depend on high-level libraries (ui) breaks dependency hierarchy
2. **Tessellation is flexible** - Circle tessellation with 64 segments is fast and smooth enough for UI
3. **Style enums improve API** - HorizontalAlign/VerticalAlign enums are clearer than numeric constants
4. **FontRenderer needs projection** - Must set orthographic projection matrix matching viewport

**Next Steps:**
- InputManager port from colonysim
- Style System improvements (if needed)
- UI Components (Button, TextInput)

---

### 2025-11-18 - Layer System API Improvements

**Summary:**
Refactored LayerManager API to address ergonomic issues discovered during usage. Added Container type for pure hierarchy nodes, implemented auto-zIndex based on insertion order, simplified creation to single-call pattern, and fixed zIndex=0.0F support with stable sort behavior.

**Files Modified:**
- `libs/ui/shapes/shapes.h` - Added Container struct, zIndex/visible fields to all shapes
- `libs/ui/layer/layer_manager.h` - New Create() and AddChild() overloads, auto-zIndex counter
- `libs/ui/layer/layer_manager.cpp` - Auto-assignment logic, stable_sort implementation
- `libs/ui/layer/layer_manager.test.cpp` - New tests for auto-zIndex and stable sort
- `apps/ui-sandbox/scenes/layer_scene.cpp` - Demo both auto and explicit zIndex modes

**Key Changes:**

**1. Added Container Type**
Pure hierarchy node with no visual representation:
```cpp
struct Container {
    const char* id = nullptr;
    float zIndex{-1.0F};  // -1.0F = auto-assign
    bool visible{true};
    void Render() const {} // No-op
};
```

**2. Simplified API - From 3 Steps to 1**
Before (verbose):
```cpp
Circle circle{.center = {400.0F, 250.0F}, .radius = 80.0F};
uint32_t id = m_layerManager.CreateCircle(circle);
m_layerManager.SetZIndex(id, 3.0F);
m_layerManager.AddChild(parent, id);
```

After (clean):
```cpp
Circle circle{.center = {400.0F, 250.0F}, .radius = 80.0F};
m_layerManager.AddChild(parent, circle);  // Auto zIndex!
```

**3. Auto-ZIndex Based on Insertion Order**
- Default `zIndex = -1.0F` triggers auto-assignment
- Sequential values assigned: 1.0, 2.0, 3.0, etc.
- Explicit values (>= 0.0F) used as-is
- Sentinel changed from 0.0F to -1.0F to allow explicit zero

**4. Stable Sort for Equal ZIndex**
- Changed from `std::sort` to `std::stable_sort`
- Preserves insertion order for equal zIndex values
- CSS-like behavior: same zIndex renders in insertion order

**Implementation Details:**

**Auto-Assignment Logic:**
```cpp
float m_nextAutoZIndex{1.0F};  // Counter in LayerManager

template <typename T>
uint32_t CreateLayer(const T& shapeData) {
    float assignedZIndex = shapeData.zIndex;
    if (assignedZIndex < 0.0F) {  // -1.0F = auto
        assignedZIndex = m_nextAutoZIndex;
        m_nextAutoZIndex += 1.0F;
    }
    // ... create node with assignedZIndex
}
```

**New API Methods:**
- `Create(const Container&)`, `Create(const Shape&)` - Standalone creation
- `AddChild(parent, const Container&)`, `AddChild(parent, const Shape&)` - Create and attach in one call
- Removed: `CreateRectangle()`, `CreateCircle()`, etc. (replaced by `Create()`)

**Testing:**
- 37 tests passing (up from 34)
- New test: `AutoAssignZIndexOnCreate` - Verifies sequential assignment
- New test: `ExplicitZeroZIndexAllowed` - Verifies 0.0F works explicitly
- New test: `StableSortPreservesInsertionOrder` - Verifies CSS-like behavior
- Visual verification: layer_scene shows both auto and explicit override modes

**Memory Impact:**
- +9 bytes per shape (zIndex float + visible bool + padding)
- Negligible compared to shape geometry data
- Trade-off: slightly more memory for much better API ergonomics

**Lessons Learned:**
1. **Sentinel values matter** - Using 0.0F as both default and sentinel was a design smell. -1.0F is better.
2. **CSS patterns work** - Insertion order + stable sort is intuitive for developers
3. **Ergonomics > purity** - Duplicating zIndex/visible in both shape and LayerNode is worth it for API clarity
4. **Progressive disclosure** - Auto-assignment for common case, explicit override for power users

**Next Steps:**
- Shape System improvements (Circle, Text rendering)
- InputManager port from colonysim
- UI Components (Button, TextInput)

---

### 2025-10-30 - GitHub Actions CI/CD Integration

**Summary:**
Implemented complete GitHub Actions CI/CD pipeline with three separate workflow files for build validation, code quality checks, and test execution. All checks now passing on every PR and push to main.

**Files Created:**
- `.github/workflows/build.yml` - Release build validation on Ubuntu
- `.github/workflows/code-quality.yml` - Code formatting and static analysis
- `.github/workflows/tests.yml` - Unit test execution via CTest

**Implementation Details:**

**Three Separate Workflows:**
1. **Build Workflow** (`build.yml`):
   - Validates Release builds on Ubuntu
   - Uses vcpkg for dependency management
   - Uploads build artifacts
   - Runs in ~3 minutes

2. **Code Quality Workflow** (`code-quality.yml`):
   - **clang-format check**: Validates code formatting (--dry-run --Werror)
   - **clang-tidy check**: Static analysis requiring compile_commands.json
   - Non-modifying (read-only validation)
   - Runs in ~6-7 minutes

3. **Tests Workflow** (`tests.yml`):
   - Builds with BUILD_TESTING=ON in Debug mode
   - Runs CTest with `--output-on-failure`
   - Excludes benchmarks: `-E ".*\.bench"`
   - Uploads test results as artifacts
   - Runs in ~5 minutes

**Environment Matching - Local macOS to CI Ubuntu:**

Critical requirement: CI must mirror local development environment as closely as possible.

**Build System:**
- Local: Uses default CMake generator (Unix Makefiles)
- CI: Initially used Ninja (pre-installed on GitHub Actions runners)
- **Fix**: Removed `-G Ninja` from all workflows to match local environment
- **Note**: vcpkg internally uses Ninja to build dependencies in CI (because it's available), but this doesn't affect main project build

**System Dependencies:**
Discovered through iterative build failures that GLFW3 requires complete X11 stack on Linux:
- `libxmu-dev` - X miscellaneous utilities
- `libxi-dev` - X11 Input extension library
- `libgl-dev` - OpenGL libraries
- `libxrandr-dev` - X11 RandR extension (for multi-monitor support)
- `libxinerama-dev` - X11 Xinerama extension (for multi-monitor support)
- `libxcursor-dev` - X11 cursor management
- `libx11-dev` - X11 core libraries
- `make` - Build system (for Unix Makefiles)
- `cmake` - Build configuration
- `clang` - C/C++ compiler (matches local)
- `clang-format` - Code formatting (code-quality only)
- `clang-tidy` - Static analysis (code-quality only)

**Technical Decisions:**

**Separate Workflows vs Single Workflow:**
- User requirement: "separate tasks in github config that are individually runnable and reportable"
- Solution: Three workflow files instead of one monolithic file
- Benefit: Each check shows separately in PR status UI
- Benefit: Can re-run individual checks without re-running everything

**Build Testing Flag:**
- Build workflow: `BUILD_TESTING=OFF` (faster, validates compilation only)
- Tests workflow: `BUILD_TESTING=ON` (required for test compilation)
- Code quality workflow: `BUILD_TESTING=ON` (clang-tidy needs full compilation database)

**Benchmark Exclusion:**
- CTest pattern: `-E ".*\.bench"` excludes all `*.bench.cpp` tests
- Rationale: Benchmarks are slow and not needed for CI validation
- Benchmarks still run locally for performance work

**Challenges Encountered:**

**1. Code Formatting Errors:**
- Initial CI run showed "tons of code formatting errors"
- Solution: Created separate branch `fix/code-formatting`
- Used `xcrun clang-format -i` (not brew) to format all 50 source files
- 4,522 insertions, 4,558 deletions (net -36 lines)
- Merged via PR #15 before continuing with CI work

**2. Ninja Environment Mismatch:**
- CI initially used Ninja (available on GitHub runners)
- Local environment doesn't have Ninja installed
- User feedback: "CI needs to mirror our local environment as best as possible"
- Solution: Removed Ninja from workflows

**3. Missing X11 Dependencies:**
- GLEW required OpenGL system libraries
- GLFW3 required complete X11 stack
- Discovered through iterative build failures:
  - First: libxmu-dev, libxi-dev, libgl-dev
  - Second: libxrandr-dev (RandR headers not found)
  - Third: libxinerama-dev (Xinerama headers not found)
- Solution: Added complete X11 dependency set

**Lessons Learned:**

**Environment Parity:**
- CI environments (Ubuntu) require explicit system dependencies that may be implicit on macOS
- vcpkg warnings in build logs are critical clues for missing dependencies
- Build system choice (Ninja vs Make) matters for environment consistency

**Workflow Organization:**
- Separate workflows provide better visibility in GitHub UI
- Each workflow can have different optimization (Release vs Debug, BUILD_TESTING on/off)
- Parallel execution of workflows faster than sequential jobs

**Testing Infrastructure:**
- CTest exclusion patterns (`-E`) useful for filtering benchmark tests
- Test artifacts helpful for debugging CI failures
- `--output-on-failure` prevents noisy output when tests pass

**Next Steps:**
- Continue with remaining Unit Testing Infrastructure tasks (Engine/Renderer tests, Documentation)
- All CI checks now automated and passing ✅
- Foundation ready for test-driven development workflow

**Related PRs:**
- PR #14: GitHub Actions CI/CD Integration (merged)
- PR #15: Code Formatting Fixes (merged)

---

### 2025-10-29 - Colonysim→Worldsim Rendering Integration Architecture

**Critical Architectural Research Complete:**

After comprehensive analysis of both colonysim and worldsim codebases plus worldsim's rendering architecture research, established the complete strategy for integrating colonysim's UI components into worldsim.

**Research Findings:**

**Four-Layer Rendering Stack Identified:**
1. **Layer 4**: Persistent Components (Layer hierarchy, Shapes, UI Components) - MISSING in worldsim
2. **Layer 3**: Primitives API (immediate-mode API) - EXISTS in worldsim ✅
3. **Layer 2**: BatchRenderer (command buffer with state sorting) - EXISTS in worldsim ✅
4. **Layer 1**: OpenGL - EXISTS in worldsim ✅

**Memory Pattern Analysis:**
- **Colonysim uses**: `std::vector<shared_ptr<Layer>>` (Pattern A - object-oriented)
- **Worldsim research recommends**: `std::vector<ConcreteType>` (Pattern B - data-oriented, value semantics)
- **Research data**: Pattern B gives "50x improvements over pointer-based approaches" at scale (10,000+ objects)
- **UI workload**: Typically <1000 components, Pattern A performs adequately (colonysim hits 60 FPS)

**Architectural Decision: Pragmatic Hybrid**

**Choice**: Port colonysim's Layer 4 components using shared_ptr pattern initially, optimize later if profiling shows need.

**Rationale**:
- Colonysim's Pattern A is proven functional in production
- Faster to implement (less refactoring required)
- Enables complete system quickly to measure actual performance
- Easy to refactor later - render methods already call Primitives API, so storage changes don't affect rendering logic
- Research explicitly recommends: "start simple, measure, optimize identified bottlenecks" (modern_rendering_architecture.md line 220)

**Key Integration Decisions:**

**1. Rendering Backend:**
- ❌ NOT porting colonysim's VectorGraphics singleton
- ✅ Adapt colonysim components to call worldsim's Primitives API (Layer 3)
- **Example adaptation**:
  ```cpp
  // Colonysim: VectorGraphics::getInstance().drawRectangle(...)
  // Worldsim:   Primitives::DrawRect({.bounds = ..., .style = ...})
  ```

**2. InputManager Pattern:**
- Instance-based (NOT singleton) - matches worldsim philosophy
- Static pointer only for GLFW callback routing (required by GLFW's C API)
- Dependency injection via constructor
- **Colonysim already implements this pattern correctly** ✅

**3. Component Port List:**
- InputManager - Instance-based input abstraction
- Style system - Plain structs (Base, Border composition)
- Layer hierarchy - Parent-child with z-ordering, dirty flag optimization
- Shape classes - Rectangle, Circle, Line, Text (adapted to Primitives API)
- UI Components - Button, TextInput (adapted input handling + Primitives)
- CoordinateSystem - Utility functions (DPI handling, percentage layouts)

**Documentation Created:**

**New Technical Docs:**
- `/docs/technical/ui-framework/colonysim-integration-architecture.md` - **Complete integration strategy**
  - Four-layer stack explained in detail
  - Memory pattern comparison (shared_ptr vs value semantics vs hybrid)
  - Pragmatic hybrid decision and rationale
  - Implementation plan with file organization
  - Component adaptation examples
  - What we're porting vs NOT doing
  - Risk mitigation strategies

**New Research Docs:**
- `/docs/research/component-storage-patterns.md` - **Deep dive on memory patterns**
  - Pattern A (shared_ptr) - Colonysim's approach
  - Pattern B (value semantics) - Worldsim research ideal
  - Pattern C (hybrid) - What we're implementing
  - Performance benchmarks from industry (Unity DOTS, cache analysis)
  - When to use each pattern
  - std::variant polymorphism example
  - Future optimization paths

**Documentation Updated:**
- `/docs/technical/ui-framework/INDEX.md` - Added colonysim integration section, updated status
- `/docs/technical/ui-framework/rendering-boundaries.md` - Added appendix marking original Options A/B/C/D as historical context, superseded by colonysim integration
- `/docs/status.md` - Marked "Rendering Integration Decision" task as complete

**Impact on Project:**

**Immediate Next Steps** (now well-defined):
1. Port InputManager (`libs/engine/input/`)
2. Port Style system (`libs/renderer/styles/`)
3. Port Layer hierarchy (`libs/ui/layer/`)
4. Port Shape classes (`libs/ui/shapes/`) - **Critical adaptation point**: rewrite render() to use Primitives
5. Port UI components (`libs/ui/components/`)
6. Port CoordinateSystem (`libs/renderer/coordinate_system/`)

**RmlUI Deferred**: Colonysim components provide Layer 4, making RmlUI lower priority. Will add RmlUI later for complex panels (inventory, skill trees) if needed.

**Validation Criteria**:
- Button with onClick handler works
- Nested Layer hierarchy renders correctly
- Input routing through InputManager works
- All rendering goes through Primitives API (batching works)
- Performance: <1ms per frame for primitive rendering, 60 FPS sustained

**Research Validation**:

The research in modern_rendering_architecture.md proved highly valuable:
- Validated the four-layer architecture (persistent objects → command buffer → state sorting → GPU)
- Provided performance data on memory patterns
- Endorsed pragmatic approach ("hybrid architectures because purity fails")
- Confirmed colonysim's approach is industry-standard for UI workloads

**Lesson Learned**: Having a working reference implementation (colonysim) made architectural decisions much clearer. Instead of building from scratch, we can port proven components and adapt them to worldsim's patterns.

**Files Referenced:**
- Colonysim codebase at `/Volumes/Code/colonysim/src/Rendering/`
- Worldsim research at `/docs/research/modern_rendering_architecture.md`
- Worldsim API design at `/docs/technical/ui-framework/primitive-rendering-api.md`

### 2025-10-29 - Application Class - Unified Game Loop Architecture

**Production-Quality Game Loop Complete:**

Implemented a unified Application class for managing the main game loop across all applications (ui-sandbox, world-sim). Based on ColonySim's ScreenManager pattern, adapted for WorldSim's Scene-based architecture.

**Implementation:**

**Application Class** (`libs/engine/application/application.{h,cpp}`):
- Owns main game loop with consistent delta time calculation
- Provides pause/resume functionality
- Exception handling and error recovery
- GLFW window integration with framebuffer resize callbacks
- Manages viewport updates for rendering system
- Integrates with DebugServer for metrics and control

**IScene Interface Updates**:
- Added `HandleInput(float dt)` method for input processing phase
- Separates concerns: HandleInput → Update → Render
- All scenes now implement HandleInput() in addition to Update() and Render()

**SceneManager Enhancement**:
- Forwards HandleInput() calls to current scene
- Maintains existing Update() and Render() forwarding
- Provides unified scene lifecycle management

**Refactored Applications**:
- **ui-sandbox**: Removed 141 lines of boilerplate game loop code
- **world-sim**: Implemented using Application pattern (133+ lines of clean integration)
- Both apps now share identical game loop (DRY principle)

**Key Benefits:**

**Shared Infrastructure**: Both applications use exact same game loop implementation - no duplication.

**Testability**: Scenes can be tested independently without GLFW dependency (mock Application).

**Foundation for UI**: Ready for ColonySim UI integration - matches the architecture patterns used in production.

**Application-Level Features**: Pause, metrics, exception handling, viewport management all handled at application level, not per-scene.

**Separation of Concerns**: Clear lifecycle phases (HandleInput → Update → Render) make scene logic easier to reason about.

**Files Created:**
- `libs/engine/application/application.h` - Application class interface (117 lines)
- `libs/engine/application/application.cpp` - Game loop implementation (172 lines)

**Files Modified:**
- `apps/ui-sandbox/main.cpp` - Refactored to use Application (-141 lines of boilerplate)
- `apps/world-sim/main.cpp` - Implemented using Application pattern (+133 lines clean code)
- `libs/engine/scene/scene.h` - Added HandleInput() to IScene interface
- `libs/engine/scene/scene_manager.{h,cpp}` - Added HandleInput() forwarding
- All scene files in `apps/ui-sandbox/scenes/` - Implemented HandleInput() method

**Testing:**
- Verified ui-sandbox runs with all scenes (shapes, font_test, vector_perf, etc.)
- Confirmed metrics stream correctly to debug server
- Tested pause/resume functionality via control endpoints
- Validated exception handling with deliberate errors

**Next Steps:**
This Application class provides the foundation for integrating ColonySim's UI systems, which expect similar lifecycle management patterns.

### 2025-10-29 - Status.md Restructure: Epic/Story/Task Format

**Documentation System Overhaul:**

Transformed status.md from a mixed-format document (containing checklists, architectural decisions, and long-form content) into a pure Epic/Story/Task checklist system.

**New Format:**
- **Epic > Story > Task > Sub-task** hierarchy (max 3 levels of nesting)
- Only last 4 completed epics + in-progress + planned epics
- Completed epics move to development-log.md with context
- Template provided at top of document for consistency
- Epic is complete only when ALL sub-tasks are [x]

**Structure:**
1. **Recently Completed Epics** - Last 4 completed (with full task lists)
2. **In Progress Epics** - Currently active work
3. **Planned Epics** - Future work with dependencies
4. **Blockers & Issues** - Current problems
5. **Notes** - Brief status updates (not detailed rationale)

**Content Migration:**
- Architectural decisions → development-log.md (short) or technical docs (long)
- Performance targets → Keep with epics (success criteria)
- Recent Decisions list → development-log.md (preserved below)

**Recent Architectural Decisions (Preserved from status.md):**

**Documentation & Organization (2025-10-12):**
- Tech docs instead of ADRs, no numbering (topic-based organization)
- Created workflows.md for common tasks (separate from CLAUDE.md)
- CLAUDE.md streamlined to ~124 lines (navigation guide only)

**C++ Standards & Tools (2025-10-12):**
- Naming: PascalCase classes/functions, camelCase variables, m_ prefix members, k prefix constants
- Header guards: `#pragma once` (not traditional guards)
- File organization: Headers (.h) and implementation (.cpp) side-by-side
- Linting: clang-format (manual) + clang-tidy (automatic)
- User's .clang-format: tabs, 140 column limit

**Architecture Decisions:**

**Vector-Based Assets (2025-10-12):**
- All game assets use SVG format with dynamic rasterization
- Roll our own core systems (not external libraries)

**Client/Server Architecture (2025-10-24):**
- Two-process design from day one (world-sim + world-sim-server)
- Server spawns on-demand (only when playing, not during main menu)
- HTTP + WebSocket protocol: HTTP for control, WebSocket for 60 Hz gameplay
- HTTP Debug Server: Separate debugging system (port 8080) using SSE

**Procedural Rendering (2025-10-26):**
- Tiles are code-generated (not SVG-based)
- Biome Influence Percentage System: Tiles have multiple biome influences creating natural ecotones
- SVG Asset Categorization: (1) Decorations/Entities, (2) Texture Patterns, (3) Animated Vegetation
- Ground Covers vs Biomes: Ground covers are physical surfaces, biomes determine appearance
- 1:1 Pixel Mapping for UI: Primitives use framebuffer dimensions for pixel-perfect rendering

**Singleton Architecture Decision (2025-10-29):**
- **Keep singletons for rendering** (industry best practice for game engines)
- Performance: Zero indirection, cache-friendly, no parameter overhead
- Industry standard: Unreal, Unity, id Tech all use singletons for core systems
- Colonysim's singleton architecture is correct for game engine performance

**Rendering Integration Strategy (2025-10-29):**
- **Decision deferred** until after compatibility analysis
- **Option A (Recommended)**: Adopt colonysim's VectorGraphics as implementation behind worldsim's Primitives API
  - Get mature batching + text + scissor support
  - Keep worldsim's clean API
  - Minimal refactoring
- **Option B**: Keep worldsim's BatchRenderer, port colonysim code to use Primitives API
  - More work, but keeps worldsim architecture pure

**Logging Macro Naming (2025-10-26):**
- Use unprefixed global macros (`LOG_ERROR` not `WSIM_LOG_ERROR`) for brevity
- Trade-off: Potential library conflicts vs developer experience
- Acceptable risk: Game project (not library), we control dependencies

**Engine Patterns Implemented (2025-10-27):**
- Resource handles (32-bit IDs with generation) - ✅ IMPLEMENTED
- Memory arenas (linear allocators) - ✅ IMPLEMENTED (14× faster than malloc)
- String hashing (FNV-1a, compile-time) - ✅ IMPLEMENTED
- Structured logging (categories + levels) - ✅ IMPLEMENTED
- Application class (unified game loop) - ✅ IMPLEMENTED (2025-10-29)
- Immediate mode debug rendering - Planned for later

**Files Modified:**
- `/docs/status.md` - Complete rewrite to Epic/Story/Task format (~560 lines → ~400 lines)
- `/docs/development-log.md` - Added this entry with preserved architectural decisions

**Workflow Documentation Created:**
Will be added to CLAUDE.md to ensure process is followed in future sessions

### 2025-10-29 - PR 2: Font Rendering System ✅ COMPLETE

**Implementation Complete:**

Successfully ported colonysim's FreeType-based font rendering system to worldsim.

**Files Created:**
- `libs/renderer/shader/shader.h` + `.cpp` - Shader utility class (loads from files)
- `libs/ui/font/font_renderer.h` + `.cpp` - Font rendering system (~350 lines)
- `shaders/text.vert` + `text.frag` - Text rendering shaders
- `apps/ui-sandbox/scenes/font_test_scene.cpp` - Demo scene
- `fonts/Roboto-Regular.ttf` - Font file

**Files Modified:**
- `vcpkg.json` - Added FreeType dependency
- `libs/renderer/CMakeLists.txt` - Added shader source
- `libs/ui/CMakeLists.txt` - Added font_renderer + FreeType linkage
- `apps/ui-sandbox/CMakeLists.txt` - Added font_test_scene
- `libs/renderer/primitives/batch_renderer.h/.cpp` - Added GetViewport() method
- `libs/renderer/primitives/primitives.h/.cpp` - Exposed GetViewport() API
- `CLAUDE.md` - Added critical workflow for testing visual changes

**Technical Details:**
- Ported from colonysim with C++20 designated initializers
- Changed glad → GLEW (worldsim standard)
- Added GetViewport() API to query actual window dimensions
- Fixed projection matrix to use runtime viewport (not hardcoded 800x600)
- Font shaders compile to texture atlas for ASCII 0-128
- Text measurement + max glyph height utilities included

**Demo Scene:**
- Renders "Hello World!" at various scales (0.8x, 1.0x, 1.2x, 1.5x, 2.0x)
- Multiple colored text samples (red, green, blue, orange, white)
- Correctly handles actual viewport dimensions (5120x2880 on test machine)

**Lessons Learned:**
- CRITICAL: Must kill old instance → rebuild → launch new instance to test changes
- Port conflicts show helpful error message with curl command to exit
- Projection matrix must match actual framebuffer dimensions from GetViewport()
- Added explicit workflow to CLAUDE.md to prevent future mistakes

**Next:**
PR 3: Style System

### 2025-10-29 - Colonysim UI Integration - Comprehensive Analysis & Planning

**Strategic Planning Session:**

Conducted comprehensive analysis of `/Volumes/Code/colonysim` codebase to identify valuable UI systems for integration into worldsim. Created detailed 9-PR implementation plan with clear dependencies and deliverables.

**Colonysim Codebase Analysis:**

Analyzed 284 source files across colonysim project to identify production-quality C++20 UI systems:

**Systems Identified (by quality and value):**

1. **Font Rendering System** (9/10 quality, ~350 lines)
   - FreeType-based glyph caching (ASCII 0-128)
   - Pre-rendered textures per character
   - Text measurement and layout utilities
   - Dedicated vertex/fragment shaders
   - **Highly portable** - only depends on FreeType and Shader class

2. **Layer/Container Hierarchy** (9.5/10 quality, ~350 lines)
   - Parent-child scene graph with z-index ordering
   - Dirty flag optimization (only sorts when needed)
   - WorldSpace vs ScreenSpace projection types
   - Update/input propagation through tree
   - **Comprehensive documentation** (LayeringSystem.md)

3. **Style System** (9/10 quality, ~200 lines)
   - Composition-based (Base + Border classes)
   - Modern C++ with designated initializers
   - Concrete styles: Rectangle, Circle, Line, Text, Polygon
   - **Clean data structures**, highly portable

4. **Shape System** (8.5/10 quality, ~400 lines)
   - All shapes extend Layer (uniform treatment)
   - Rectangle, Circle, Line, Polygon, Text implementations
   - Dirty flag pattern for optimization
   - **Key insight**: Everything is a Layer!

5. **UI Components** (8.5/10 quality, ~1,400 lines)
   - **Button**: State machine (normal/hover/pressed), callbacks, type-based styling
   - **TextInput**: Focus management, cursor + blinking, scrolling, placeholder
   - **Feature-complete** but needs input abstraction (direct GLFW calls)

6. **VectorGraphics Batching** (9/10 quality, ~300 lines)
   - Batched rendering singleton
   - Queues geometry + text separately
   - Scissor support for clipping
   - **Mature implementation**, proven in production

7. **CoordinateSystem** (9/10 quality, ~100 lines)
   - Multi-DPI support with pixel ratio
   - Screen/world space conversions
   - Percentage-based layout helpers

**Worldsim Current State Analysis:**

Compared against worldsim's rendering architecture:

**Worldsim Strengths:**
- Clean Primitives API with batching
- Memory arenas (14x faster than malloc)
- Resource handle system with generation tracking
- Debug server with real-time metrics streaming
- Simple but effective scene management

**Worldsim Gaps:**
- No text rendering
- No texture system
- No UI hierarchy (scenes only)
- Incomplete state management (scissor/transform tracked but not applied)
- No Bezier curve support (blocks Phase 4+ vector graphics validation)

**Architectural Decisions:**

**1. Singleton Architecture Decision:**
- **Keep singletons for rendering** (industry best practice)
- Performance: Zero indirection, cache-friendly, no parameter overhead
- Industry standard: Unreal, Unity, id Tech all use singletons for core systems
- Colonysim's architecture is correct for game engine performance

**2. Rendering Integration Strategy:**
- **Decision deferred** to PR 1 compatibility analysis
- **Option A (Recommended)**: Adopt colonysim's VectorGraphics as implementation behind worldsim's Primitives API
  - Get mature batching + text + scissor support
  - Keep worldsim's clean API (scenes already use it)
  - Minimal refactoring
- **Option B**: Keep worldsim's BatchRenderer, port colonysim code to use Primitives API
  - More work, but keeps worldsim architecture pure

**9-PR Implementation Plan:**

**PR 1: Compatibility Analysis** (1-2 hours)
- Compare coordinate systems
- Test minimal integration
- Make rendering strategy decision
- Document findings

**PR 2: Font Rendering System** (3-4 hours)
- Port FontRenderer + shaders
- Add FreeType dependency
- Demo: "Hello World" rendering

**PR 3: Style System** (2-3 hours)
- Port Base, Border, concrete style classes
- Demo: Various styled shapes

**PR 4: Rendering Integration** (4-6 hours)
- Based on PR 1 decision
- Either adopt VectorGraphics or adapt to Primitives
- Ensure all existing scenes still work

**PR 5: Layer System** (3-4 hours)
- Port Layer hierarchy with z-ordering
- Port CoordinateSystem utilities
- Demo: Nested layers

**PR 6: Shape System** (4-5 hours)
- Port all shape types (Rectangle, Circle, Line, Polygon, Text)
- Demo: All shapes rendering

**PR 7: Button Component** (3-4 hours)
- Port Button with state machine
- Demo: Interactive buttons

**PR 8: TextInput Component** (4-5 hours)
- Port TextInput with focus management
- Demo: Text input fields

**PR 9: Polish & Integration** (3-4 hours)
- Memory arena integration
- Performance profiling
- Input abstraction
- Documentation updates

**Total Estimated Work:** 24-33 hours across 9 PRs

**Key Insights:**

**Production-Ready Code**: Colonysim's UI systems are 8.5-9.5/10 quality with modern C++20 patterns, proper optimizations, and comprehensive features.

**Architectural Compatibility**: Both projects use singletons, GLFW, OpenGL 3.3+, similar patterns. High compatibility expected.

**Hybrid Approach Best**: Use colonysim's VectorGraphics as implementation layer behind worldsim's Primitives API. Best of both worlds.

**Minimal Refactoring**: Keep worldsim's scene system and API, swap implementation underneath. Existing scenes continue working.

**Performance Focus**: All architectural decisions prioritize performance over flexibility (singletons, batching, dirty flags, memory arenas).

**Files Updated:**
- `/docs/status.md` - Added colonysim integration plan with 9 PRs, architectural decisions, task breakdown

**Next Session:**
Start PR 1 (Compatibility Analysis) - verify coordinate systems, test minimal integration, make rendering strategy decision.

### 2025-10-29 - Vector Graphics Validation Plan - Grass Blade Phases

**Documentation Update - Progressive Complexity Validation:**

Updated the vector graphics validation plan to reflect completed star phases and add new grass blade phases that introduce Bezier curve tessellation.

**Validation Progression:**

**Phase 0-3: Stars (COMPLETE) ✅**
- Validated basic polygon tessellation with straight line segments
- Proven 60 FPS with 10,000 static and animated stars
- Batching system works effectively (<100 draw calls)
- Foundation for vector graphics rendering established

**Phase 4-7: Grass Blades (IN PROGRESS) 🔄**
- **Phase 4**: Single grass blade with Bezier curves (introduces curve flattening)
- **Phase 5**: 10,000 static grass blades (batching with curved shapes)
- **Phase 6**: 10,000 animated grass blades (swaying/bending - CRITICAL validation)
- **Phase 7**: SVG loading with curve support

**Why Grass Blades Next:**
- **More complex than stars**: Requires Bezier curve tessellation, not just straight edges
- **Actual game assets**: Grass tufts are ground decorations in the design docs
- **Animation critical**: Grass swaying, bending, trampling are key gameplay features
- **Representative shapes**: Organic curved vegetation (not geometric stars)
- **Performance validation**: Curves create 2-3x more triangles than simple polygons

**Key Design Decisions:**

**Progressive validation approach**: Simple shapes first (stars) prove basic tessellation, then curved shapes (grass) prove the complete system can handle production assets.

**Performance budget adjustments**: Grass blades have slightly higher budgets (3ms tessellation vs 2ms, 8ms GPU vs 6ms) due to increased triangle count from curves.

**Critical validation milestone**: Phase 6 (10k animated grass blades) is the final proof that organic vector graphics with real-time curve-based animation is viable at production scale.

**Documentation Updated:**
- `/docs/technical/vector-graphics/validation-plan.md` - Added phases 4-7 for grass blades
- `/docs/status.md` - Updated active tasks and next steps

**Timeline:**
- Stars: ~1 week ✅ COMPLETE
- Grass: ~1 week ⏳ IN PROGRESS (starting Phase 4)
- Overall: ~2 weeks to prove full vector graphics viability

### 2025-10-29 - Sandbox Control Endpoints Implementation

**Production Developer Tools Enhancement:**

Implemented HTTP control endpoints for the ui-sandbox, enabling remote control via simple GET requests with query parameters. Completes the sandbox development workflow with graceful lifecycle management and port conflict detection.

**Implementation:**

1. **ControlAction Enum** - Single atomic enum prevents conflicting control directives
   - Actions: `None`, `Exit`, `SceneChange`, `Pause`, `Resume`, `ReloadScene`
   - Thread-safe atomic operations (HTTP thread writes, main thread reads)

2. **Control Endpoint** - `GET /api/control?action={action}`
   - `?action=exit` - Gracefully exit sandbox
   - `?action=scene&scene=name` - Switch scenes with validation
   - `?action=pause` - Pause scene updates/rendering
   - `?action=resume` - Resume scene updates/rendering
   - `?action=reload` - Reload current scene (OnExit + OnEnter)
   - Returns JSON responses with status/error

3. **Main Loop Integration**
   - Control action checking after `glfwPollEvents()`
   - All actions execute on main thread (GLFW/OpenGL safety)
   - Scene switching with validation and logging
   - Pause state tracking (skips update/render when paused)

4. **Port Conflict Detection**
   - Disabled SO_REUSEADDR on server socket
   - Prevents multiple instances on same port
   - Clear error message with curl command to kill existing instance
   - Instant detection (no HTTP connection delay)

5. **Scene Manager Enhancement**
   - Added `GetCurrentSceneName()` method for reload functionality

**Design Decisions:**

**Single atomic enum over multiple flags:** Prevents conflicting directives. Simpler API with clear semantics.

**GET with query params over POST+JSON:** Simpler for future use. Browser-testable. No JSON parsing.

**Disabled SO_REUSEADDR:** Prevents port conflicts without separate socket check. Fast and reliable.

**Main thread execution:** All GLFW/OpenGL operations on main thread. HTTP thread only sets flags.

**Files Modified:**
- `libs/foundation/debug/debug_server.{h,cpp}` - Control endpoint, SO_REUSEADDR disable
- `libs/engine/scene/scene_manager.{h,cpp}` - Added GetCurrentSceneName()
- `apps/ui-sandbox/main.cpp` - Control action processing in main loop
- `CLAUDE.md` - Added Sandbox Control section

**Testing:**
- ✅ Pause/Resume - verified scene updates stop/start
- ✅ Scene switching - tested shapes → arena transition
- ✅ Scene reload - verified OnExit/OnEnter called
- ✅ Exit - clean shutdown with exit code 0
- ✅ Port conflict - second instance fails with helpful error

### 2025-10-27 - Resource Handle System Implementation

**Foundational Engine Pattern Complete:**

Implemented a production-ready resource handle system for safe asset management. Handles provide safe references to resources (textures, meshes, SVG assets) with automatic stale reference detection via generation counters.

**Implementation:**

Created header-only handle system in `libs/renderer/resources/` with two components:

1. **ResourceHandle** (`resource_handle.h`) - Core 32-bit handle type
   - 16-bit index (lower bits) + 16-bit generation (upper bits)
   - Packing/unpacking methods: `GetIndex()`, `GetGeneration()`
   - Factory methods: `Make()`, `Invalid()`
   - Comparison operators for equality checks
   - Type-safe aliases: `TextureHandle`, `MeshHandle`, `SVGAssetHandle`

2. **ResourceManager<T>** (`resource_manager.h`) - Generic template manager
   - Free list for index recycling (O(1) allocation/deallocation)
   - Generation tracking prevents stale handle access
   - `Allocate()` - Get new handle, reuses freed slots
   - `Free()` - Increment generation, add to free list
   - `Get()` - Retrieve resource with validation
   - Capacity: 65,536 resources max per type (16-bit index)

**Key Features:**
- Header-only (no build system changes needed)
- Generation validation (detects stale/dangling references)
- O(1) allocation, free, and lookup
- Compact (4 bytes vs 8-byte pointer)
- Serializable (save/load as 32-bit value)
- Type-safe via templates

**Testing:**

Created comprehensive test suite in `apps/ui-sandbox/demos/handle_demo.cpp`:

**Test Results:**
- ✅ Basic Allocation: Allocate 3 handles, set/get data correctly
- ✅ Free List Reuse: Freed indices 1,2,3 → reallocated as 3,2 (LIFO, gen incremented)
- ✅ Stale Handle Detection: Old handle returns null after free/realloc
- ✅ Handle Validation: Invalid handles, out-of-range indices handled safely
- ✅ Comparison Operators: Equality/inequality work correctly

**Use Cases:**
- Texture management with hot-reloading
- SVG asset caching
- Mesh resource pooling
- Any resource with potential lifetime issues

**Design Decisions:**

**Generation prevents stale access:** When resource freed, generation increments. Old handles with old generation return null on Get().

**Free list LIFO order:** Reuses most recently freed slot first. Improves cache locality for resources with similar lifetimes.

**Not thread-safe:** Each thread should use separate manager instance or add external synchronization. Documented as constraint.

**65,536 resource limit:** 16-bit index allows up to 65,536 resources per type. Can increase index bits if needed, trading generation bits.

**Files Created:**
- `libs/renderer/resources/resource_handle.h` - Handle type (75 lines, header-only)
- `libs/renderer/resources/resource_manager.h` - Manager template (115 lines, header-only)
- `apps/ui-sandbox/demos/handle_demo.cpp` - Test suite (270 lines)

**Files Modified:**
- `apps/ui-sandbox/CMakeLists.txt` - Use handle_demo for testing

**Integration:**
- All tests run automatically on ui-sandbox startup
- Console output shows detailed validation results
- No build system changes needed (header-only)

**Benefits:**
- Prevents dangling pointer bugs
- Enables hot-reloading (reload asset, handle stays valid)
- Half the memory of raw pointers (4 vs 8 bytes)
- Serialization-friendly for save files

**Next Steps:**
Vector graphics system can now use handles for SVG assets and rasterized texture caching.

### 2025-10-27 - Memory Arena Allocators Implementation

**Foundational Engine Pattern Complete:**

Implemented a production-ready memory arena allocator system for fast temporary allocations. Arenas provide 10-100× faster allocation/deallocation compared to standard malloc/new by using simple pointer-bump allocation and bulk deallocation.

**Implementation:**

Created header-only arena system in `libs/foundation/memory/arena.h` with three classes:

1. **Arena** - Core linear allocator
   - Allocates large buffer once via `malloc()`
   - Bump-pointer allocation with alignment support
   - Type-safe templated methods: `Allocate<T>()`, `AllocateArray<T>(count)`
   - Instant reset via pointer reset (no per-object cleanup)
   - Capacity tracking: `GetUsed()`, `GetSize()`, `GetRemaining()`

2. **FrameArena** - Per-frame wrapper
   - Designed for per-frame temporary data
   - `ResetFrame()` method for end-of-frame cleanup
   - Same allocation interface as Arena

3. **ScopedArena** - RAII wrapper
   - Saves checkpoint on construction
   - Automatically resets arena on destruction
   - For scoped temporary allocations

**Key Features:**
- Header-only (no build system changes needed)
- Non-copyable (deleted copy constructor/assignment)
- Alignment-aware (respects `alignof(T)` for all types)
- Debug-friendly (`assert()` on out-of-memory)
- ~170 lines total

**Performance Testing:**

Created comprehensive test suite in `apps/ui-sandbox/demos/arena_demo.cpp`:

**Performance Test Results:**
- Arena: 70 microseconds for 10,000 Vec2 allocations
- Standard: 652μs allocation + 342μs deallocation = 994μs total
- **Speedup: 14.2× faster than standard allocation**
- Reset: Instant (< 1 microsecond for any size)

**Validation Tests:**
- ✅ Alignment Test: All alignments correct (1, 4, 8, 16-byte)
- ✅ Capacity Test: Correctly tracked 800 bytes for 100 uint64_t allocations
- ✅ Reset Test: Instant reset to 0 bytes used
- ✅ Scoped Test: RAII automatic reset on scope exit (80 bytes → 0 bytes)

**Use Cases:**
- Per-frame temporary data (UI layout, debug rendering)
- Chunk generation scratch space (noise buffers, tile processing)
- Vector graphics tessellation (upcoming)
- Algorithm temporary buffers
- String building and formatting

**Design Decisions:**

**Does NOT call destructors:** Arenas are for POD types or manual cleanup. Documented as a constraint.

**Not thread-safe:** Each thread should use its own arena. Documented as a constraint.

**Assert on out-of-memory:** Fail-fast in debug builds instead of graceful degradation. Forces proper arena sizing during development.

**Files Created:**
- `libs/foundation/memory/arena.h` - Complete implementation (170 lines, header-only)
- `apps/ui-sandbox/demos/arena_demo.cpp` - Comprehensive test suite (208 lines)

**Files Modified:**
- `apps/ui-sandbox/CMakeLists.txt` - Switched from shapes_demo to arena_demo for testing

**Integration:**
- All tests run automatically on ui-sandbox startup
- Console output shows performance comparison and test results
- No build system changes needed (header-only)

**Next Engine Pattern:**
Resource handles (32-bit IDs with generation counter) for safe asset management.

### 2025-10-27 - Logging System Bug Fix - Developer Client Integration

**Critical Bug Fixed:**

Discovered and fixed two bugs preventing DEBUG logs from appearing in the developer client browser UI.

**Bug #1: Pre-Filtering Issue**

**Problem:** Logger was filtering logs by level BEFORE sending to debug server. This meant DEBUG logs (and any filtered logs) never reached the developer client, even though the client has its own filtering UI.

**Root Cause:** In `Logger::Log()` (`libs/foundation/utils/log.cpp`), the level filter check happened before calling `debugServer->UpdateLog()`:
```cpp
// OLD CODE (WRONG):
if (level < GetLevel(category)) {
    return;  // Too verbose, skip - NEVER REACHES DEBUG SERVER!
}
// ... format message ...
if (s_debugServer) {
    s_debugServer->UpdateLog(...);  // DEBUG logs never get here
}
```

**Fix:** Reordered code to send ALL logs to debug server before applying console filtering:
```cpp
// NEW CODE (CORRECT):
// Format message first
char message[256];
vsnprintf(message, sizeof(message), format, args);

// Send to debug server (ALL logs, regardless of console filter)
if (s_debugServer) {
    s_debugServer->UpdateLog(...);
}

// THEN apply level filter for console output
if (level < GetLevel(category)) {
    return;  // Skip console, but already sent to debug server
}
```

**Result:** Developer client receives ALL logs, users can filter in browser UI. Console still respects level filters (less noise).

**Bug #2: Race Condition on Startup**

**Problem:** Debug server was initialized AFTER most startup logs fired, so early logs never reached the ring buffer (`s_debugServer` was nullptr).

**Fix:** Moved debug server initialization to very beginning of `main()`:
```cpp
int main() {
    // Parse args FIRST (no logging yet)
    // ...

    Logger::Initialize();

    // Start debug server IMMEDIATELY (before any logs)
    Foundation::DebugServer debugServer;
    foundation::Logger::SetDebugServer(&debugServer);
    debugServer.Start(8081);

    // NOW all logs go to ring buffer
    LOG_INFO(UI, "UI Sandbox - Component Testing & Demo Environment");
    // ...
}
```

**Result:** All startup logs (including early DEBUG logs) are captured in ring buffer and available when client connects.

**Bug #3: Same-Timestamp Log Dropping**

**Problem:** When multiple logs fired within the same millisecond (common during startup), only the first log was sent to clients. Subsequent logs with the same timestamp were dropped.

**Root Cause:** Debug server used `>` (greater than) for timestamp comparison:
```cpp
// OLD CODE (WRONG):
if (entry.timestamp > lastSentTimestamp) {
    // Send log
    lastSentTimestamp = entry.timestamp;
}
// Logs with same timestamp as lastSent are DROPPED but already consumed from ring buffer!
```

**Fix:** Changed to `>=` to handle multiple logs with same timestamp:
```cpp
// NEW CODE (CORRECT):
if (entry.timestamp >= lastSentTimestamp) {
    // Send log
    lastSentTimestamp = entry.timestamp;
}
```

**Result:** All logs sent to client, even when multiple fire in the same millisecond.

**Performance Impact:**
- Debug server initialization: ~1ms (one-time at startup)
- Message formatting: ~100-500ns per log (acceptable for DEVELOPMENT_BUILD only)
- Lock-free ring buffer writes: ~10-20ns (unchanged)
- **Total impact: Negligible** (~1ms startup delay, sub-microsecond per log)

**Testing:**
- Verified DEBUG logs from all categories appear in developer client
- Verified console still filters logs by level (UI category set to Info, DEBUG logs hidden in console but visible in browser)
- Verified early startup logs appear in browser
- Verified repeating logs (frame counter) stream correctly
- Verified all INFO logs appear (no more missing logs due to timestamp collision)

**Files Modified:**
- `libs/foundation/utils/log.cpp` - Reordered Logger::Log() to send to debug server before filtering
- `libs/foundation/debug/debug_server.cpp` - Fixed timestamp comparison (> to >=)
- `apps/ui-sandbox/main.cpp` - Moved debug server initialization to very beginning

**Impact:**
Complete observability for development - ALL logs now stream to developer client regardless of console filtering, enabling full debugging without console noise.

### 2025-10-27 - String Hashing System Implementation

**Core Engine Pattern Complete:**

Implemented a production-ready compile-time string hashing system using the FNV-1a algorithm. This is a foundational pattern that will be used throughout the codebase for fast string comparisons and lookups.

**Implementation:**
- **FNV-1a hash function**: 64-bit constexpr hash function for compile-time and runtime hashing
- **HASH() macro**: Convenience macro for compile-time hashing of string literals
- **Common hash constants**: Pre-defined hashes for common strings (Transform, Position, Velocity, etc.)
- **Debug collision detection**: Debug-only hash registry that asserts on collisions
- **String lookup**: Reverse lookup function for debugging (maps hash back to original string)

**Key Features:**
- **Compile-time evaluation**: String literal hashes computed at compile-time (zero runtime cost)
- **100-1000x faster** than string comparison for lookups in hot paths
- **Header-only**: No .cpp file needed, easy to use anywhere
- **Type-safe**: Using `StringHash` type alias (uint64_t) instead of raw integers
- **Debug support**: Collision detection and reverse lookup only in Debug builds

**Use Cases:**
- ECS component type identification
- Resource loading and caching (texture/shader paths)
- Config file JSON key lookups
- Event system type identification
- Debug command dispatch

**Performance:**
- Compile-time hashing: Zero runtime cost (inlined constant)
- Runtime hashing: ~10-20 CPU cycles for typical strings
- Collision probability: ~0.00000003% for 10,000 strings in 64-bit space

**Files Created:**
- `libs/foundation/utils/string_hash.h` - Complete implementation with documentation

**Next Integration Points:**
- ECS system (component type lookups)
- Resource manager (asset path caching)
- Config system (JSON key parsing)

**Documentation:**
- Design: `/docs/technical/string-hashing.md` (pre-existing)
- Implementation follows spec exactly (FNV-1a, 64-bit, compile-time)

### 2025-10-27 - Developer Client Implementation - Complete Feature Set

**Client-Side History, localStorage Persistence, and SVG Charting:**

Implemented all features designed in the technical documentation with clean separation of concerns and proper styling architecture.

**Core Infrastructure:**
- **CircularBuffer utility class**: Generic fixed-size rolling window with O(1) insert, properly handles capacity changes
- **LocalStorageService**: State persistence with automatic cleanup, quota management, error handling, graceful degradation

**TimeSeriesChart Component (Generic, SVG-based):**
- **Generic reusable component** - no hardcoded metric types
- Real-time time-series visualization using SVG with normalized viewBox (0-1 coordinates)
- Auto-scaling Y-axis based on data range with 10% padding
- Compact 60px height (less than 100px requirement)
- Current value displayed from last item in array
- **All styling in CSS** - no inline styles, colors, or sizes in React code
- CSS class variants for different metric colors (fps, frameTime, drawCalls, vertices, triangles)

**Multiple Metrics Display:**
- **5 separate charts** displayed simultaneously in column layout:
  - FPS (green)
  - Frame Time (yellow)
  - Draw Calls (blue)
  - Vertices (magenta)
  - Triangles (cyan)
- Min/Max frame time stats row
- All charts share single circular buffer for history
- Each chart extracts its metric values from shared history

**LogViewer Component:**
- Array-based log storage with count limits (500 / 1000 / 2000 / 5000)
- Filter by log level (Debug+ / Info+ / Warning+ / Error)
- Text search (case-insensitive)
- Auto-scroll detection (preserves manual scroll position)
- Color-coded by level (DEBUG gray, INFO white, WARN yellow, ERROR red)
- File:line display for warnings/errors
- Count limit dropdown integrated into component header
- localStorage integration (restore logs on mount)

**App.tsx Integration:**
- **Single circular buffer** for all metrics (not per-chart)
- localStorage persistence on mount/unmount (not continuous)
- State restoration from localStorage on page load
- **Proper retention window handling**: Recreates buffer when window changes, preserves existing history
- "Clear History" button in header (affects both metrics and logs)
- **Retention window control with metrics** (30s/1min/5min/10min) - doesn't affect logs
- System log entries for connection events

**UI Layout:**
- Time window selector moved to Metrics section (only affects metrics)
- Clear History button in header (affects both metrics and logs)
- Connection status indicator in header
- Two-column layout: Metrics (left) | Logs (right)

**Design Changes:**
- **Canvas → SVG**: Per user preference, using declarative SVG rendering
- **Generic chart component**: TimeSeriesChart takes `values` array, no metric-specific logic
- **CSS-only styling**: All colors, sizes, spacing controlled via CSS modules and variables
- Updated documentation throughout to reflect SVG approach

**Build Output:**
- Single HTML file: 671 KB (gzip: 201 KB)
- All JavaScript and CSS inlined
- Works with `file://` protocol

**Files Created:**
- `apps/developer-client/src/utils/CircularBuffer.ts` - Generic circular buffer utility
- `apps/developer-client/src/services/LocalStorageService.ts` - Persistence service
- `apps/developer-client/src/components/TimeSeriesChart.tsx` - Generic SVG chart component
- `apps/developer-client/src/components/TimeSeriesChart.module.css` - Chart styles with color variants
- `apps/developer-client/src/components/LogViewer.tsx` - Log display with filtering
- `apps/developer-client/src/components/LogViewer.module.css` - Log viewer styles

**Files Modified:**
- `apps/developer-client/src/App.tsx` - Circular buffer integration, localStorage, 5 metric charts
- `apps/developer-client/src/App.module.css` - Layout for charts column, metrics header
- `apps/developer-client/src/styles/globals.css` - Added --accent-blue variable
- `/docs/technical/observability/developer-client.md` - Canvas → SVG throughout

**Build System:**
- Integrated with CMake (make developer-client)
- Auto-builds in Development/Debug mode
- Output copied to build/developer-client/

### 2025-10-27 - Developer Client Documentation - Architecture & Design Refinement

**Documentation Quality Improvement:**

Refactored and expanded the developer client technical documentation to follow project best practices for design documents.

**Phase 1: Refactoring (920 → 359 lines)**

Removed production-ready code implementations and replaced with architectural design:
- Removed complete React component implementations (MetricsChart, LogViewer, HoverInspector with full code)
- Removed complete CSS modules with every style property
- Removed line-by-line tutorial code
- Replaced with component responsibilities described conceptually
- Replaced with architectural patterns and design decisions
- Focused on WHY decisions were made, not HOW to implement

**Phase 2: Expansion (359 → 681 lines)**

Added comprehensive design for client-side data management:

**Client-Side History Aggregation:**
- Server does NOT aggregate or store history (streams current values only)
- Client maintains rolling history buffer for visualization
- Rationale: Server stays stateless, client controls retention, browser has sufficient memory

**Configurable Retention Policies:**
- Metrics: Time-based (30s / 60s / 5min / 10min)
- Logs: Count-based (500 / 1000 / 2000 / 5000 entries)
- Different strategies because metrics are dense time-series, logs are sparse events

**localStorage Persistence Strategy:**
- Preserves history and preferences across page reloads
- Persistence lifecycle: Read on mount, write on unmount (not continuous)
- Automatic cleanup: Age-based trimming, size monitoring, quota management
- Error handling: Graceful degradation if disabled, automatic recovery on quota exceeded

**Time-Series Graphing:**
- Canvas 2D rendering for performance
- Rolling time window with auto-scrolling X-axis
- Auto-scaling Y-axis based on data range
- Multi-series support (multiple metrics on one chart)
- Grid rendering and current value overlay

**Performance & Storage Considerations:**
- Memory calculations: < 400 KB metrics, < 1.5 MB logs, < 2 MB total typical
- localStorage performance: 10-50ms read/write on mount/unmount only
- Canvas rendering: 2-12ms per frame (well within 16ms budget)
- Circular buffer for metrics (O(1) insert, fixed memory)
- Array for logs (simpler, order matters for historical record)
- Storage quota: < 5 MB target (works on all browsers)

**Files Modified:**
- `/docs/technical/observability/developer-client.md` - Refactored and expanded (920 → 359 → 681 lines)

**Documentation Standard Achieved:**
- Focuses on architecture and design decisions (WHY/WHAT)
- Explains rationale and tradeoffs for key decisions
- Includes small conceptual code snippets only where helpful (circular buffer example, quota management pattern)
- No production-ready copy-paste implementations
- Actual codebase remains source of truth for implementation
- Follows project standard: "Code in technical docs should describe or demonstrate HOW something complex should be done, not have actual production code"

### 2025-10-27 - Developer Client - TypeScript Web UI for Debug Server

**Production-Quality Developer Tool Complete:**

Built a standalone TypeScript/React web application for monitoring C++ applications during development. Replaces the embedded HTML UI with a professional single-page application with advanced features.

**Technology Stack:**
- **React 18** with TypeScript for type-safe UI development
- **Vite** for fast development and optimized production builds
- **vite-plugin-singlefile** to bundle everything into a single HTML file
- **CSS Modules** for component-scoped styling
- **EventSource API** for Server-Sent Events streaming

**Application Features:**

**Real-Time Metrics Dashboard:**
- FPS and frame time monitoring (current, min, max)
- Draw call tracking
- Vertex and triangle count
- Updates at 10 Hz via SSE stream from debug server

**Live Log Viewer:**
- Color-coded log levels (DEBUG, INFO, WARN, ERROR)
- Category badges (Renderer, Physics, Network, etc.)
- Timestamps with file:line for warnings/errors
- Auto-scroll with 1000-log history buffer
- System logs for connection events

**Graceful Connection Handling:**
- Automatic reconnection when server restarts (built-in EventSource retry)
- Visual connection status indicator (green/yellow)
- System log entries for connect/disconnect events
- No manual intervention required during development cycles
- Handles multiple restart cycles seamlessly

**Developer Experience Improvements:**
- Inline source maps for debugging (TypeScript → browser dev tools)
- Single 614 KB HTML file (no external dependencies)
- Works via `file://` protocol (no web server needed)
- Type-safe interfaces matching C++ server output
- Strict TypeScript compilation (`noUnusedLocals`, `noUnusedParameters`)

**Build Integration:**
- Integrated into CMake build system
- Auto-builds on `make` in Debug/Development builds
- Output copied to `build/developer-client/index.html`
- npm install and Vite build happen automatically
- Skips gracefully if npm not installed

**Connection Management:**
- `ServerConnection` class wraps EventSource API
- Per-stream connection state tracking
- Callbacks for connect/disconnect events
- Error handling with JSON parse protection
- Support for multiple stream types (metrics, logs, future: profiler, events)

**Architecture:**
```
┌─────────────────────────────────────────────┐
│  Developer Client (TypeScript/React SPA)    │
│  - Metrics Dashboard                        │
│  - Log Viewer                               │
│  - Connection Status                        │
└──────────────────┬──────────────────────────┘
                   │ Server-Sent Events (SSE)
                   │ /stream/metrics (10 Hz)
                   │ /stream/logs (10 Hz)
┌──────────────────▼──────────────────────────┐
│  Debug Server (C++ cpp-httplib)             │
│  - Lock-free ring buffers                   │
│  - SSE streaming endpoints                  │
│  - Running in ui-sandbox/world-sim          │
└─────────────────────────────────────────────┘
```

**Usage Workflow:**
```bash
# Build C++ app with developer client
cd build && make

# Run application (debug server starts on port 8081)
./apps/ui-sandbox/ui-sandbox

# Open developer client in browser
open build/developer-client/index.html

# Restart app as many times as needed - client auto-reconnects
```

**Files Created:**
- `apps/developer-client/src/App.tsx` - Main application component
- `apps/developer-client/src/App.module.css` - Scoped styles
- `apps/developer-client/src/main.tsx` - Application entry point
- `apps/developer-client/src/services/ServerConnection.ts` - SSE connection manager
- `apps/developer-client/src/styles/globals.css` - Global theme variables
- `apps/developer-client/vite.config.ts` - Build configuration with inline source maps
- `apps/developer-client/tsconfig.json` - TypeScript compiler settings
- `apps/developer-client/package.json` - Dependencies (React, Vite, TypeScript)
- `apps/developer-client/index.html` - Vite entry point

**Build Configuration:**
- CMake target `developer-client` (ALL builds in Debug/Development)
- Runs `npm install` and `npm run build` automatically
- Copies output to `build/developer-client/`
- world-sim and ui-sandbox depend on developer-client target

**Design Decisions:**
- Single HTML file for portability (works without web server)
- Inline source maps (debugging without separate .map files)
- EventSource over WebSocket (simpler for one-way streaming, auto-reconnect)
- 1000-log buffer limit (prevents browser memory issues)
- System category for client-side events (connection status)
- Matching C++ log level strings (`WARN` not `WARNING`)

**Testing:**
- Verified metrics update in real-time at 10 Hz
- Confirmed logs stream with correct color coding
- Tested disconnect/reconnect with ui-sandbox restarts
- Validated source maps in browser dev tools
- Confirmed build integration with CMake

**Next Steps:**
- Add log filtering by category/level (UI controls)
- Add log search functionality
- Consider adding performance charts (FPS over time)
- Consider adding profiler stream visualization
- Add download logs as text file

### 2025-10-26 - Observability Web UI with Real-Time Logging

**Complete Observability Stack Operational:**

Extended the debug server with real-time log streaming and a tabbed web interface, providing full observability for development.

**Architecture - Lock-Free Performance Guarantee:**

**CRITICAL DESIGN CONSTRAINT: Performance > Complete Logs**
- Logger writes to lock-free ring buffer (~10-20ns, never blocks game thread)
- If ring buffer full (1000 entries): **oldest logs silently dropped**
- Zero mutex/locks in game thread path
- HTTP server reads from ring buffer at 10 Hz (throttled)

**LogEntry Structure** (`Foundation::LogEntry`):
```cpp
struct LogEntry {
    LogLevel level;           // Debug/Info/Warning/Error
    LogCategory category;     // Renderer/Physics/Audio/Network/Game/World/UI/Engine/Foundation
    char message[256];        // Formatted log message
    uint64_t timestamp;       // Unix timestamp in milliseconds
    const char* file;         // Source file (static string pointer)
    int line;                 // Line number
};
```

**Integration Flow:**
1. `LOG_INFO(Renderer, "message")` → Logger::Log()
2. Logger formats message, writes to console (colored)
3. Logger calls `debugServer->UpdateLog()` (lock-free, ~10-20ns)
4. DebugServer writes to `LockFreeRingBuffer<LogEntry, 1000>`
5. HTTP `/stream/logs` endpoint reads from buffer at 10 Hz
6. Browser receives Server-Sent Events with JSON log entries

**Web UI - Tabbed Interface:**

**Tab 1: Performance** (existing)
- Real-time metrics: FPS, frame time (min/max/current), draw calls, vertices, triangles
- Updates via `/stream/metrics` SSE endpoint (10 Hz)

**Tab 2: Logs** (NEW)
- Real-time log viewer with auto-scroll
- Color-coded by level:
  - Debug: Gray (#808080)
  - Info: White (#d4d4d4)
  - Warning: Yellow (#dcdcaa)
  - Error: Red (#f48771)
- Shows: timestamp, category, level, message
- File:line for warnings/errors
- Limit: 500 logs (prevents browser memory issues)
- Updates via `/stream/logs` SSE endpoint (10 Hz)

**HTTP Endpoints:**
- `GET /` - Tabbed web UI (HTML/CSS/JavaScript)
- `GET /api/health` - Health check (JSON)
- `GET /api/metrics` - Current metrics snapshot (JSON)
- `GET /stream/metrics` - Real-time metrics stream (SSE, 10 Hz)
- `GET /stream/logs` - Real-time log stream (SSE, 10 Hz) **NEW**

**Logger Integration:**
- Added `Logger::SetDebugServer(DebugServer*)` static method
- Logger stores static pointer to debug server
- On each log call, sends to debug server (only in DEVELOPMENT_BUILD)
- Conversion between `foundation::` and `Foundation::` enums
- Properly disconnects on shutdown (prevents use-after-free)

**ui-sandbox Wiring:**
```cpp
// After creating debug server
foundation::Logger::SetDebugServer(&debugServer);

// Before destroying debug server
foundation::Logger::SetDebugServer(nullptr);
```

**Files Modified/Created:**
- `libs/foundation/debug/debug_server.{h,cpp}` - Added LogEntry struct, UpdateLog(), /stream/logs endpoint
- `libs/foundation/debug/debug_server.cpp` - Rewrote HTML UI with tabs, log viewer, SSE streaming
- `libs/foundation/utils/log.{h,cpp}` - Added DebugServer integration, enum conversion
- `apps/ui-sandbox/main.cpp` - Wired Logger to DebugServer

**Testing:**
- Verified logger connects to debug server on startup
- Confirmed logs stream to browser in real-time
- Tested tab navigation (Performance ↔ Logs)
- Verified color-coded log levels
- Confirmed auto-scroll and 500-log limit
- Tested under load: no frame drops, logs may be dropped (by design)

**Performance Characteristics:**
- Game thread log overhead: ~10-20ns (lock-free write to ring buffer)
- HTTP streaming: 10 Hz (not real-time, throttled to prevent bandwidth issues)
- If logging faster than HTTP can consume: oldest logs dropped silently
- Zero performance impact even if debug server is slow/stuck

**Usage:**
```bash
# Run ui-sandbox (debug server on port 8081 by default)
./ui-sandbox

# Open web UI in browser
open http://localhost:8081

# Switch between Performance and Logs tabs
# Logs stream in real-time with color coding
```

**Next Steps:**
- Consider adding log filtering in web UI (by category/level)
- Consider adding log search functionality
- Consider adding download logs as text file

### 2025-10-26 - Structured Logging System Implementation

**Core Engine Pattern Complete:**

Implemented a production-ready structured logging system for the entire project, providing organized diagnostic output with categories and log levels.

**System Architecture:**

**Logger Class** (`libs/foundation/utils/log.{h,cpp}`):
- Four log levels: Debug, Info, Warning, Error
- Nine categories: Renderer, Physics, Audio, Network, Game, World, UI, Engine, Foundation
- Per-category level filtering (set different verbosity for each system)
- Automatic timestamping (HH:MM:SS format)
- ANSI color codes for terminal output (gray/white/yellow/red)
- File and line number capture for warnings and errors

**Convenience Macros:**
```cpp
LOG_DEBUG(category, format, ...)
LOG_INFO(category, format, ...)
LOG_WARNING(category, format, ...)
LOG_ERROR(category, format, ...)
```

**Build Configuration:**
- Development builds: All log levels available, `DEVELOPMENT_BUILD` flag enables Debug/Info/Warning
- Release builds: Only Error logs remain, Debug/Info/Warning compile to `((void)0)`
- CMake automatically sets flag for Debug and RelWithDebInfo build types

**Usage Examples:**
```cpp
LOG_INFO(Renderer, "Initializing renderer: %dx%d", width, height);
LOG_ERROR(Network, "Failed to connect to server");
LOG_DEBUG(Physics, "Tick took %f ms", deltaTime);
```

**Output Format:**
```
[19:08:10][UI][INFO] UI Sandbox - Component Testing & Demo Environment
[19:08:10][Renderer][INFO] OpenGL Version: 4.1 ATI-7.0.23
[19:08:11][Foundation][INFO] Debug server: http://localhost:8081
```

**Design Decision - Macro Naming:**

Chose **unprefixed global macros** (`LOG_ERROR` not `WSIM_LOG_ERROR`) for developer experience:
- **Pros**: Cleaner code, better readability, shorter is better for ubiquitous operations
- **Cons**: Potential conflicts with other libraries defining similar macros
- **Mitigation**: Game project (not library), we control dependencies, can refactor if needed
- **Documented**: Tradeoff explicitly documented in `/docs/technical/logging-system.md`

**Integration:**
- ui-sandbox fully converted to use logging system (replaced all `std::cout`/`std::cerr`)
- Foundation library exports logger for use by all other libraries
- Initialized in `main()` before any other systems

**Testing:**
- Verified colored output with timestamps in terminal
- Confirmed different categories display correctly
- Tested log level filtering (Debug logs visible in development builds)
- Verified ANSI color codes work on macOS terminal

**Future HTTP Streaming Integration:**

Documentation includes design for lock-free ring buffer + Server-Sent Events streaming to external debug app (from `/docs/technical/observability/developer-server.md`), to be implemented when needed.

**Files Created/Modified:**
- `libs/foundation/utils/log.h` - Logger class, enums, macros (NEW)
- `libs/foundation/utils/log.cpp` - Implementation with console output (NEW)
- `libs/foundation/CMakeLists.txt` - Added log.cpp, DEVELOPMENT_BUILD flag
- `apps/ui-sandbox/main.cpp` - Converted all output to logging system
- `docs/technical/logging-system.md` - Added macro naming convention section

**Next Engine Pattern:**
String hashing system (FNV-1a with compile-time hashing)

### 2025-10-26 - Pixel-Perfect UI Rendering & Window Sizing

**Fixed Critical Rendering Bug:**

The primitive rendering system was using a hardcoded 800x600 virtual coordinate system, causing shapes to physically change pixel dimensions when the window was resized. This violated the design specification for 1:1 pixel mapping.

**Root Cause:**
- Projection matrix was `ortho(0, 800, 600, 0)` regardless of actual framebuffer size
- `Rect(50, 50, 200, 100)` would be 320px wide in an 1280x720 window, but 640px wide in a 2560x1440 window
- Shapes scaled with window resize, breaking pixel-perfect rendering

**Fix Applied:**
- Changed projection matrix to use actual framebuffer dimensions: `ortho(0, m_viewportWidth, m_viewportHeight, 0)`
- Added `SetViewport(width, height)` to BatchRenderer and Primitives API
- Called `SetViewport()` on window creation and framebuffer resize callback
- Now `Rect(50, 50, 200, 100)` is **always exactly 200×100 pixels**, regardless of window size

**UI Sandbox Improvements:**
- Window now launches at 80% of screen size (was hardcoded 800x600)
- Queries primary monitor via GLFW to calculate appropriate initial size
- Created demo system structure (`demos/demo.h`, `demos/shapes_demo.cpp`)
- Moved rendering code from `main.cpp` to proper demo implementation
- Console output shows screen and window dimensions on startup

**Industry Standard Alignment:**
- RmlUI uses pixel coordinates with 1:1 mapping (per our documentation)
- ImGui uses pixel coordinates with 1:1 mapping
- Unity UI default is "Constant Pixel Size" mode
- Our implementation now matches these standards

**Files Modified:**
- `libs/renderer/primitives/batch_renderer.{h,cpp}` - Added viewport tracking, fixed projection matrix
- `libs/renderer/primitives/primitives.{h,cpp}` - Exposed SetViewport() API
- `apps/ui-sandbox/main.cpp` - Added monitor querying, demo system integration, viewport updates
- `apps/ui-sandbox/demos/demo.h` - Created demo interface (NEW)
- `apps/ui-sandbox/demos/shapes_demo.cpp` - Implemented shapes demo (NEW)
- `apps/ui-sandbox/CMakeLists.txt` - Added shapes_demo.cpp to build

**Test Results:**
```
Screen: 3200x1800
Window: 2560x1440 (80% of screen)
```

Shapes now maintain constant pixel dimensions when window is resized. ✅

### 2025-10-27 - UI Sandbox Implementation + Lock-Free Performance Monitoring

**UI Sandbox Foundation - Fully Operational:**

Built complete ui-sandbox development tool with working primitive rendering and real-time performance monitoring:

**Primitive Rendering API Implemented:**
- `DrawRect()`, `DrawLine()`, `DrawRectBorder()` - Basic 2D shape primitives
- Batching system with OpenGL 3.3 shaders (vertex + fragment)
- Batch accumulator minimizes draw calls (single draw per batch)
- Transform and scissor stacks (for world-space rendering and clipping)
- Color type with common presets (Red, Green, Blue, etc.)
- Rect type with collision helpers (Contains, Intersects, Intersection)

**Performance Monitoring System:**
- **Lock-free ring buffer** (atomic operations only, zero mutex contention)
- HTTP Debug Server on port 8081 using cpp-httplib
- REST endpoints: `/api/health`, `/api/metrics`
- Server-Sent Events stream: `/stream/metrics` (10 Hz updates)
- HTML UI at `http://localhost:8081` with live metrics
- Metrics tracked: FPS, frame time (min/max/current), draw calls, vertices, triangles

**Critical Architecture Fix:**
- Initial implementation used `std::mutex` (WRONG - could block game thread!)
- Replaced with lock-free ring buffer per observability spec
- Game thread writes: ~10-20 nanoseconds (was 100-1000ns uncontended, 1-10ms contended)
- HTTP thread reads: ~10-20 nanoseconds
- **Zero possibility of frame drops from monitoring** ✅

**Performance Test Results:**
- Normal operation: **~1370 FPS** (0.73ms frame time)
- Stress test (50 concurrent curl requests): **9009 FPS** (0.11ms frame time)
- Frame time range: 0.08ms - 2.34ms (max likely OS scheduler)
- Draw calls: 1 (batching working correctly)
- **No frame drops observed under HTTP load** ✅

**Files Created (17 files):**
- `libs/foundation/math/types.h` - GLM type aliases (Vec2, Vec3, Mat4)
- `libs/foundation/graphics/{color.h, rect.h}` - Core graphics types
- `libs/foundation/metrics/performance_metrics.{h,cpp}` - Metrics data structure + JSON serialization
- `libs/foundation/debug/debug_server.{h,cpp}` - HTTP server with SSE streaming
- `libs/foundation/debug/lock_free_ring_buffer.h` - Lock-free template from observability spec
- `libs/renderer/primitives/primitives.{h,cpp}` - Public 2D drawing API
- `libs/renderer/primitives/batch_renderer.{h,cpp}` - OpenGL batching implementation
- `libs/renderer/metrics/metrics_collector.{h,cpp}` - Frame timing + stats collection
- `apps/ui-sandbox/main.cpp` - Complete rewrite with metrics integration

**Build System:**
- vcpkg.json baseline updated (8f54ef5453e7e76ff01e15988bf243e7247c5eb5)
- CMake configured with toolchain
- All dependencies installed (GLFW, GLEW, GLM, cpp-httplib)
- Foundation library changed from INTERFACE to regular library

**Testing:**
```bash
# Run ui-sandbox with debug server (default port 8081)
./ui-sandbox

# Query current metrics
curl http://localhost:8081/api/metrics
# {"fps": 1369.86, "frameTimeMs": 0.73, "drawCalls": 1, ...}

# Stream live metrics (10 updates/second)
curl -N http://localhost:8081/stream/metrics

# Open browser for visual monitoring
open http://localhost:8081
```

**Key Architectural Decisions:**
- Lock-free observability is **non-negotiable** for zero-overhead monitoring
- ui-sandbox is always a dev tool (no Release build guards needed)
- Default port 8081 for ui-sandbox debug server
- Primitive API as foundation for both custom rendering AND RmlUI backend

**Next Steps:**
1. Add text rendering to Primitive API (basic font support)
2. Integrate RmlUI for complex UI panels
3. Implement RmlUI backend using Primitive API
4. Build developer client (TypeScript/Vite) for advanced metrics visualization

### 2025-10-26 - UI Framework Architecture - Complete Design

**Critical Gap Addressed:**

Comprehensive planning exists for vector graphics rendering and procedural tile systems, but **no technical design for UI framework** - a foundational requirement for ui-sandbox and complex game UI (inventory grids, skill trees, resource management).

**Production game context established:** This is not a learning project - need production-quality UI for complex management game with thousands of dynamic UI elements.

**User Context:**
- Web/React developer background (familiar with declarative UI)
- Previous C++ project used modern designated initializer syntax (`.position = value`)
- Hit scrolling container issues (performance/clipping in OpenGL)
- Wants crisp text rendering matching vector aesthetic

**Documentation Created** (`/docs/technical/ui-framework/`):

**Seven comprehensive design documents:**

1. **INDEX.md** - Navigation hub for UI framework documentation
   - Desired API style examples (CSS-like `.Args{}` syntax)
   - Integration with observability system
   - Common UI patterns (menus, forms, HUD)

2. **ui-architecture-fundamentals.md** - Foundational concepts for production games
   - Immediate vs retained mode explained in React/web terms
   - Scene graphs = React's virtual DOM
   - What production games actually use (Unity, Unreal, indies)
   - RmlUI as HTML/CSS for C++ games
   - Clear recommendation: Retained mode for complex game UI

3. **library-options.md** - Initial comparative analysis (6 libraries)
   - Later superseded by fundamentals doc with production focus

4. **integration-analysis.md** - Technical integration deep dive
   - Immediate vs retained mode in React terms
   - OpenGL rendering models (who draws what)
   - Transitive dependencies for each library
   - Rendering conflict analysis (NanoVG issues)
   - Backend implementation examples
   - Dependency table (RmlUI: FreeType, NanoGUI: NanoVG+Eigen)

5. **library-isolation-strategy.md** - Risk mitigation and architecture isolation
   - Interface pattern to isolate RmlUI from game code
   - What CAN be isolated (game logic, event handlers)
   - What CANNOT be isolated (.rml files, data binding syntax)
   - Hidden constraints (threading, memory management, font rendering)
   - Conflicts with project goals analysis
   - Migration cost assessment (1-2 weeks simple, 4-8 weeks complex)
   - Prototype-first recommendation

6. **rendering-boundaries.md** - Critical architectural boundaries
   - **The "two different APIs" problem identified**
   - RmlUI for screen-space panels only (menus, inventory, skill trees)
   - Custom rendering for game world (tiles, entities)
   - Custom rendering for world-space UI (health bars, tooltips)
   - **Solution: Unified Primitive Rendering API**
   - Complete render loop architecture
   - Real-world examples (RimWorld, Factorio, Unity, Unreal)

7. **primitive-rendering-api.md** - Foundation layer design
   - Unified API for all 2D drawing (DrawRect, DrawText, DrawTexture)
   - Used by RmlUI backend, game rendering, world-space UI
   - Immediate mode API, retained mode implementation (batching)
   - Scissor/clipping stack for scrollable containers
   - Transform stack for world-space rendering
   - Integration with existing renderer and vector graphics
   - Performance targets (<1ms per frame for all primitives)
   - Complete code examples (health bars, tooltips, minimap)

8. **rmlui-integration-architecture.md** - Complete integration design
   - Multi-layer architecture (game → interface → adapter → RmlUI → primitives)
   - IUISystem, IDocument, IElement abstract interfaces
   - RmlUI adapter hidden in .cpp files (game never sees RmlUI headers)
   - Renderer backend (RmlUI geometry → Primitives API)
   - Three usage patterns: XML, programmatic, hybrid
   - Complete game loop integration example
   - CMake configuration with private RmlUI linking
   - Testing strategy (unit tests with mock, integration tests with real)
   - Migration strategy (game code unchanged, rewrite adapter only)

**Key Architectural Decisions:**

**1. RmlUI for Screen-Space Complex UI**
- HTML/CSS-like markup for complex layouts (inventory grids, skill trees)
- Flexbox layout engine (handles nested containers automatically)
- Can be used programmatically (NO .rml files required)
- Isolated via interface layer (game code never depends on RmlUI)

**2. Unified Primitive Rendering API**
- **Solves "two different APIs for rectangles" problem**
- One DrawRect/DrawText/DrawTexture API used by everything:
  - RmlUI backend
  - Game world rendering
  - World-space UI (health bars)
  - Custom UI components
- Batching implementation (minimize draw calls)
- Integration with existing renderer

**3. Clear Rendering Boundaries**
- **RmlUI**: Screen-space panels only (menus, inventory, dialogs)
- **Custom/Primitives**: Game world + world-space UI + simple HUD
- **Vector Graphics**: Complex SVG entities (trees, decorations)
- No overlap or confusion about which system to use

**4. Isolation Strategy**
- Game code depends on IUISystem/IDocument/IElement interfaces
- RmlUI adapter hidden in private .cpp files
- CMake links RmlUI privately (not exposed to game)
- Migration cost: Rewrite adapter (~500 lines), game code unchanged

**Critical Insights:**

**Production Game Requirements Clarified:**
- Not a learning project or toy
- Complex management game UI (thousands of elements)
- Inventory grids, skill trees, resource management panels
- Performance critical (60 FPS with dynamic updates)

**RmlUI Programmatic API Discovery:**
- Can create all UI from C++ (NO .rml files required)
- Avoids markup file lock-in
- Enables `.Args{}` wrapper pattern
- Still get flexbox layout engine benefits

**Text Rendering:**
- RmlUI requires FreeType (can't avoid)
- Can write custom font backend using msdfgen for SDF fonts
- Integration point with vector graphics system

**Scrolling Containers:**
- OpenGL scissor test (hardware clipping)
- RmlUI handles automatically
- Primitives API exposes PushScissor/PopScissor for custom usage

**Next Steps:**

1. ✅ **Complete architecture documentation** (DONE)
2. **Prototype RmlUI integration** (1-2 weeks)
   - Implement primitive rendering API
   - Build RmlUI backend + isolation layer
   - Create one complex UI panel (inventory or skill tree)
   - Validate performance and integration
3. **Evaluate prototype results**
   - Does flexbox handle layouts?
   - Is performance acceptable?
   - Are constraints manageable?
4. **Make final decision**
   - Commit to RmlUI if prototype succeeds
   - Build custom if critical issues found

**Documentation Organization:**
- Created `/docs/technical/ui-framework/` subdirectory
- Updated `/docs/technical/INDEX.md` with UI Framework section
- All documents cross-referenced and navigable

### 2025-10-26 - OpenGL + RmlUI Implementation Guide - Production-Ready Rendering Layer

**Critical Missing Piece:**

Architecture was complete, but **no implementation guidance** for actually building the OpenGL rendering layer with RmlUI integration. User asked: "How should we set up our rendering layer according to RmlUI best practices? Do they have a guide for a production ready game for the OpenGL setup?"

**Research Conducted:**

Comprehensive research of RmlUI's official documentation, reference implementations, and production best practices:

1. **RmlUI Official Documentation** (mikke89.github.io/RmlUiDoc)
   - RenderInterface specification (`<RmlUi/Core/RenderInterface.h>`)
   - Integration guide and main loop patterns
   - Troubleshooting and common mistakes
   - Rendering conventions and coordinate system requirements

2. **GL3 Reference Backend Analysis** (`RmlUi_Renderer_GL3.cpp`)
   - Complete class structure and implementation patterns
   - Vertex buffer management (VAO/VBO/IBO)
   - Shader architecture (9 programs for various effects)
   - State backup/restore implementation
   - Scissor region handling with Y-flip
   - Transform dirty flag pattern

3. **Production Integration Patterns**
   - Main loop order (Input → Update → Render)
   - Initialization sequence (interfaces → initialize → context → fonts → documents)
   - State management requirements
   - Performance profiling approaches

**Documentation Created:**

**opengl-rmlui-implementation-guide.md** - Comprehensive production-ready implementation guide (~2000 lines)

**10 Major Sections:**

1. **OpenGL Rendering Architecture**
   - Complete render pipeline (world → world UI → screen UI)
   - Three-layer architecture (RmlUI → Backend → Primitives → OpenGL)
   - Batching strategy decision (where to batch, how it works)

2. **Coordinate System Conventions**
   - RmlUI vs OpenGL origin mismatch (top-left vs bottom-left)
   - Y-axis flipping for projection matrix
   - Scissor region Y-coordinate transformation
   - Color space (sRGB with premultiplied alpha)
   - Blend function: `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`
   - Face culling (CCW winding)

3. **RmlUI Backend Implementation**
   - Complete `RmlUIBackend` class implementing `Rml::RenderInterface`
   - Geometry compilation strategy (immediate GPU upload with VAO/VBO/IBO)
   - `CompileGeometry()` implementation with vertex attributes
   - `RenderGeometry()` options (direct OpenGL vs Primitive API)
   - Texture management (`LoadTexture()` and `GenerateTexture()`)
   - Scissor implementation with Y-flip
   - Transform support with dirty flags

4. **Primitive API OpenGL Implementation**
   - `BatchAccumulator` class design
   - Batching by texture/state/transform/scissor
   - Flush triggers (state changes, max batch size, frame end)
   - Vertex buffer strategy (dynamic upload, capacity management)
   - Draw call accumulation and rendering

5. **Integration Patterns**
   - **Critical main loop order** (violations cause crashes/glitches)
   - Initialization sequence (order matters!)
   - Shutdown sequence (backend must outlive RmlUI)
   - Input injection (RmlUI doesn't convert keys to text!)
   - Viewport handling on resize

6. **State Management** - **CRITICAL**
   - Why state backup/restore is mandatory
   - Complete `GLState` structure (20+ state variables)
   - `BackupGLState()` implementation
   - `RestoreGLState()` implementation
   - `BeginFrame()` / `EndFrame()` lifecycle

7. **Production Best Practices**
   - Always backup/restore state (non-negotiable)
   - Preserve render order (never batch/reorder RmlUI calls)
   - Handle viewport changes correctly
   - Use visual testing suite
   - Integrate RmlUI debugger
   - Profile early and often
   - Load fonts before documents
   - Handle interface lifetimes correctly
   - Implement high-resolution timer
   - Copy reference backend, don't link it

8. **Common Pitfalls** (10 documented issues)
   - Face culling blank screen (DirectX vs OpenGL winding)
   - Upside-down UI (forgot Y-flip)
   - Scissor regions incorrect (forgot Y-flip)
   - Crash on shutdown (destroyed backend too early)
   - Text not rendering (fonts not loaded)
   - Slow animations (low-resolution timer)
   - State corruption (no backup/restore)
   - Modifying elements after Update (causes crashes)
   - Incorrect initialization order
   - Premultiplied alpha confusion

9. **Performance Considerations**
   - Expected performance (reference vs batched)
   - Profiling strategy (separate Update vs Render time)
   - Optimization targets (< 2ms for complex UI)
   - Batching effectiveness measurement
   - Memory usage expectations

10. **Testing and Validation**
    - RmlUI visual test suite usage
    - Integration testing examples
    - Debug visualization
    - Logging and diagnostics

**Key Technical Discoveries:**

**RmlUI Reference Backend Does NOT Batch:**
- Each `RenderGeometry()` call = one draw call
- Prioritizes correctness and transform flexibility
- Simple, battle-tested approach
- **Our architecture batches at Primitive API layer below**

**Critical Coordinate System Details:**
```cpp
// Projection matrix MUST flip Y-axis
glm::ortho(0, width, height, 0, -1, 1);  // Note: bottom/top swapped

// Scissor regions need Y-flip
int flippedY = viewportHeight - region.p1.y;
glScissor(region.p0.x, flippedY, width, height);
```

**State Management is Non-Negotiable:**
- Must backup/restore 20+ OpenGL state variables
- Reference backend does comprehensive state preservation
- Failure causes subtle rendering corruption bugs
- `BeginFrame()` → backup + setup, `EndFrame()` → restore

**Main Loop Order is Critical:**
```cpp
// CORRECT order (violations cause crashes/glitches):
ProcessInput();           // 1. Input BEFORE update
UpdateUIDataBindings();   // 2. Modify elements
context->Update();        // 3. Update (resolves layout)
// 4. NEVER modify elements here!
context->Render();        // 5. Render
```

**Initialization Order Matters:**
```cpp
// CORRECT sequence:
Rml::SetRenderInterface(backend);   // 1. Set interfaces
Rml::SetSystemInterface(&system);
Rml::Initialise();                  // 2. Initialize
CreateContext();                    // 3. Create context
Rml::LoadFontFace("font.ttf");      // 4. Load fonts
context->LoadDocument("menu.rml");  // 5. Load documents
```

**Batching Strategy Decision:**
- **Reference backend**: No batching (immediate rendering)
- **Our architecture**: Batch at Primitive API layer
- RmlUI backend uploads geometry to GPU (like reference)
- But calls Primitive API instead of raw OpenGL
- Primitive API accumulates and batches draw calls
- **Result**: Battle-tested geometry compilation + performance batching

**Performance Targets:**
- Simple UI: < 0.5ms per frame
- Complex UI (inventory grid): < 2ms per frame
- Expected batching ratio: 5-10x (1000 calls → 100 draw calls)

**Production Patterns from Reference Backend:**
- `GL_STATIC_DRAW` for vertex/index data (RmlUI guarantees immutability)
- Comprehensive state backup (blend, depth, stencil, culling, scissor, viewport, bindings)
- Y-flip transformation for scissor regions
- Clamping scissor to viewport (prevents WebGL validation errors)
- Dirty flag pattern for transform matrices
- Per-program uniform location caching

**Documentation Updated:**
- Updated `INDEX.md` status to "Implementation Phase: Architecture complete"
- Added implementation guide to "Implementation Guides" section
- Updated "Next Steps" with concrete implementation tasks
- Added revision history entry

**Next Steps:**

Architecture and implementation guide complete. Ready to begin coding:

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

### 2025-10-26 - Procedural Tile System Game Design Documentation

**Major Architectural Clarification:**

Comprehensive game design documentation for the procedural tile rendering system. **Critical discovery**: Tiles are procedurally generated in code (not SVG-based), with SVG assets serving as decorations and entities placed on top of the procedural ground.

**Core Innovation - Biome Influence Percentage System:**

Tiles don't have a single "type" but instead have percentage influences from multiple biomes:
```
Example: { meadow: 80%, forest: 20% }
- Ground: 80% grass appearance, 20% forest floor appearance
- Entities: 80% wildflower spawn probability, 20% pine tree spawn probability
- Movement speed: Interpolated between meadow and forest values
```

This creates **natural ecotones** (transition zones) hundreds of tiles deep between biomes, with gradual species mixing and visual blending. Every forest-meadow edge looks different, every coastline is unique.

**Game Design Documentation Created** (7 documents):

**Core Tile System** (`/docs/design/`):
1. `visual-style.md` - Overall aesthetic direction: hand-crafted feel, recognizable biomes, organic appearance
2. `biome-ground-covers.md` - Seven ground cover types (grass, forest floor, dirt, sand, rock, wetland, water)
3. `biome-influence-system.md` - Percentage-based biome blending, natural ecotone creation
4. `tile-transitions.md` - Visual appearance of biome transition zones (complete rewrite for percentage system)
5. `procedural-variation.md` - Creating unique tiles while maintaining biome recognizability

**SVG Asset Organization** (`/docs/design/features/vector-graphics/`):
6. `svg-decorations.md` - SVG assets as placed objects (flowers, trees, boulders)
7. `svg-texture-patterns.md` - SVG patterns as fills for code-drawn shapes (brick, wood, concrete)

**Documentation Index Updates**:
- Updated `/docs/design/INDEX.md` with "Visual Design & Procedural Tile System" section
- Updated vector graphics section with organized SVG documentation links

**Three Distinct SVG Use Cases Identified:**

1. **Decorations/Entities**: Placed objects on top of procedural tiles
   - Ground decorations: flowers, pebbles, grass tufts (5-10 per tile)
   - Standing entities: trees, bushes, boulders (0-3 per tile)
   - Placement follows biome influence percentages
   - Biome-appropriate spawning (meadow flowers only in meadow-influenced tiles)

2. **Texture Patterns**: Seamlessly tiling fills for code-drawn shapes
   - Building materials: brick, wood planks, concrete, thatch
   - Terrain features: cobblestone paths, rock formations
   - Any shape/size polygon filled with hand-crafted texture
   - Pattern must tile perfectly (edges match)

3. **Animated Vegetation**: Spline-based deformation (documented previously in animated-vegetation.md)
   - Grass swaying, tree movement, player interaction
   - Real-time Bezier curve manipulation

**Key Terminology Clarifications:**

- **Ground Covers**: Physical surface types (grass, sand, rock) - permanent, code-generated
- **Biomes**: Ecological zones that determine which ground covers appear and which decorations spawn
- **Tiles**: Individual game world cells with multiple biome influence percentages
- **Ecotones**: Natural transition zones between biomes (emergent from percentage system)
- **Decorations**: Small SVG objects (flowers, pebbles)
- **Entities**: Larger SVG objects (trees, boulders, creatures)
- **Texture Patterns**: Seamlessly tiling SVG patterns for filling shapes

**Visual Rendering Layers** (bottom to top):
1. Procedural ground covers (code-generated, biome-blended)
2. SVG texture pattern fills (code-drawn shapes with SVG fills)
3. Ground SVG decorations (flowers, pebbles)
4. Standing SVG entities (trees, structures)
5. Animated SVG elements (swaying grass, trees)

**Deterministic Variation System:**

- Each tile gets deterministic seed from world position (x, y)
- Same position always generates same appearance
- Color variation within biome palette (±10-20% lightness/saturation)
- Entity placement varies (rotation, scale, position)
- Balance: recognizable biomes with endless visual variety
- Multiplayer compatible (all clients see same world)

**Design Philosophy Established:**

- **Hand-crafted illusion**: Player should think "someone carefully designed this", not "obviously procedural"
- **Recognizable not readable**: Biomes must be instantly identifiable by players
- **Variation within coherence**: Every tile unique, but biome identity maintained
- **Performance in technical docs**: Game design docs focus on player experience, not implementation
- **Separation of concerns**: Design docs describe "what/why", technical docs describe "how"

**Iterative Refinement Process:**

Multiple rounds of user feedback refined the documentation:
- Changed "readable" → "recognizable" (it's a game, not text)
- Removed performance concerns from design docs
- Removed prototyping plans from design docs
- Changed "terrain types" → "tile types" → "ground covers" (terminology evolution)
- Removed snow as ground cover (now seasonal overlay system)
- Removed code examples from design docs (belongs in technical docs)
- Split SVG documentation by use case (decorations vs patterns vs animation)

**Open Questions Documented** (for future technical design):

1. Color variation ranges (how much can grass green vary?)
2. Entity density variation (fixed or varied per tile?)
3. Scale variation ranges (trees 80%-120% of base?)
4. Cross-tile decoration continuity (coordinate or independent?)
5. Regional vs per-tile variation (macro trends or purely local?)
6. Pattern library size (how many texture patterns needed?)
7. LOD transition smoothness (fade decorations gracefully?)

**Next Steps:**

1. Create technical design documents for tile system implementation
2. Define edge blending algorithms (geometric, alpha, marching squares)
3. Specify procedural variation implementation (noise functions, seed-based generation)
4. Document terrain layer composition system
5. Specify SVG-tile integration architecture
6. Prototype procedural tile rendering in ui-sandbox

### 2025-10-24 - Vector Graphics System Research & Documentation

**Vector Graphics Rendering Strategy Complete:**

Comprehensive research and documentation phase for vector graphics rendering system. Analyzed approaches from multiple game engines (Godot, Unity, Bevy, Phaser, LibGDX) and created detailed comparative analysis of all key components.

**Documentation Created** (18 documents):

**Technical Documentation** (`/docs/technical/vector-graphics/`):
1. `INDEX.md` - Navigation hub for all vector graphics documentation
2. `architecture.md` - Four-tier rendering system (static, semi-static, dynamic, GPU compute)
3. `tessellation-options.md` - Comparative analysis: libtess2 vs Earcut vs Poly2Tri vs custom ear clipping
4. `svg-parsing-options.md` - Comparative analysis: NanoSVG vs LunaSVG vs PlutoVG vs custom parser
5. `rendering-backend-options.md` - Comparative analysis: NanoVG vs Blend2D vs custom batched renderer vs Vello
6. `batching-strategies.md` - GPU batching techniques, streaming VBOs, texture atlasing
7. `animation-system.md` - Spline-based deformation for grass/trees, wind simulation, trampling
8. `collision-shapes.md` - Dual representation (render geometry vs physics shapes)
9. `lod-system.md` - Level of detail strategies for zoom-based rendering
10. `memory-management.md` - Memory architecture across all tiers (~350 MB budget)
11. `performance-targets.md` - Performance budgets, profiling methodology (60 FPS @ 10k entities)
12. `asset-pipeline.md` - Moved from root, updated with comprehensive cross-references

**Game Design Documentation** (`/docs/design/features/vector-graphics/`):
13. `README.md` - Asset creation workflow for artists, SVG guidelines, procedural variation
14. `animated-vegetation.md` - Grass swaying, tree movement, player interaction behavior
15. `environmental-interactions.md` - Trampling mechanics, harvesting, wind effects

**Index Updates**:
16. Updated `/docs/technical/INDEX.md` with vector graphics section
17. Updated `/docs/design/INDEX.md` with vector graphics features

**Key Architectural Decisions**:

- **Four-Tier System**: Static backgrounds (pre-rasterized) → Semi-static structures (cached meshes) → Dynamic entities (real-time tessellation) → GPU compute (future)
- **Desktop-First**: OpenGL 3.3+ target, leverage full desktop GPU capabilities
- **CPU Tessellation Primary**: Proven approach (Godot, Unity, Phaser pattern), defer GPU compute to Tier 4
- **Custom Batched Renderer Recommended**: Best fit for 10,000+ dynamic entities @ 60 FPS
- **Hybrid Parsing**: NanoSVG (already in project) + custom metadata parsing for game data
- **Spline-Based Animation**: Real-time Bezier curve deformation for organic movement
- **Dual Geometry**: Separate render (complex) vs collision (simplified) shapes

**Comparative Analysis Completed**:
- Each component analyzed with 3+ library options plus "no library" custom implementation
- Objective pros/cons for all options
- Decision criteria frameworks (not decisions)
- Performance estimates and complexity assessments

**Performance Targets Defined**:
- 60 FPS with 10,000+ animated entities
- <2ms tessellation budget
- <100 draw calls per frame
- ~350 MB memory budget

**Next Steps**:
1. Begin prototyping in ui-sandbox (Phase 1: Basic tessellation + rendering)
2. Test tessellation options with real SVG assets
3. Validate performance assumptions
4. Make library selection decisions based on prototype results
5. Implement core rendering pipeline

### 2025-10-24 - Networking & Multiplayer Architecture

**Client/Server Architecture Finalized:**
- Two-process design from day one: `world-sim` (client) + `world-sim-server` (headless)
- Server spawns only when needed (new game/load game), not during main menu
- Client manages server lifecycle: spawn, health monitoring, graceful shutdown
- Process management handles crash detection, zombie prevention, cross-platform spawning

**Network Protocol Defined:**
- HTTP (REST) for control plane: create game, load game, chunk requests, health checks
- WebSocket for real-time gameplay: 60 Hz entity updates, player input, world events
- Hybrid approach: Control vs. data plane separation
- JSON messages initially, binary protocol (MessagePack/Protobuf) for future optimization

**Colony Sim Synchronization Strategy:**
- Four object types: Terrain (rarely changes), Flora (event-based), Structures (deltas), Entities (60 Hz)
- Batch updates for mass events: Plant plagues, fires, meteors use efficient area mutations
- Chunk-based world streaming: Client requests chunks as viewport moves, aggressive LRU caching
- SVG assets local to client, server only sends metadata (position, rotation, variant seed)

**HTTP Debug Server Specification:**
- Separate debugging system on port 8080 (independent of game server on port 9000)
- Server-Sent Events (SSE) for real-time metrics streaming at configurable rates (10 Hz default)
- Web-based debug app (TypeScript + Vite) with custom Canvas chart rendering
- Lock-free ring buffers for metrics collection (game thread writes, server thread reads)
- Rate-limited streams prevent bandwidth issues: metrics 10 Hz, logs 20 Hz throttled, profiler 5 Hz

**Technical Documentation Created:**
- `/docs/design/features/debug-server/README.md` - Debug server game design doc
- `/docs/design/features/multiplayer/README.md` - Multiplayer architecture game design doc
- `/docs/technical/http-debug-server.md` - Debug server implementation design
- `/docs/technical/multiplayer-architecture.md` - Client/server protocol and synchronization
- `/docs/technical/process-management.md` - Cross-platform process lifecycle management

**Key Technical Decisions:**
- **cpp-httplib** chosen for both game server and debug server (header-only, SSE support, simple integration)
- Server-Sent Events preferred over WebSocket for debug server (one-way streaming, simpler, auto-reconnect)
- WebSocket required for game server (bidirectional, low-latency gameplay)
- Process spawning: fork/exec on Unix, CreateProcess on Windows
- Health monitoring: HTTP polling every 5s + PID checks for crash detection

**Library Organization Clarified:**
- `game-systems` is SERVER ONLY (colony simulation logic, must not depend on renderer)
- `renderer` and `ui` are CLIENT ONLY (never linked by server)
- `world` is SHARED (procedural generation algorithms)
- `engine/shared`, `engine/client`, `engine/server` split for clear separation

**Next Session Should:**
1. Begin implementing process management (client spawning server)
2. Set up cpp-httplib in vcpkg.json
3. Create basic HTTP server skeleton in `world-sim-server`
4. Implement health check endpoint
5. Test cross-platform process spawning (macOS, Windows, Linux)

### 2025-10-12 - Initial Planning Session

**Documentation System Created:**
- Established three-tier docs: status.md, technical/, design/
- Created workflows.md for common development tasks
- Streamlined CLAUDE.md from 312→124 lines (navigation only, no duplication)
- Technical docs use topic-based organization (no ADR numbering)

**Project Architecture Defined:**
- Monorepo with 6 libraries in dependency layers: foundation → renderer → ui/world/game-systems → engine
- Headers and implementation side-by-side (not separate include/src dirs)
- Custom ECS in engine (roll our own, not EnTT)
- Philosophy: Build core systems ourselves, only use external libs for platform/formats

**Major Architectural Decision: Vector-Based Assets**
- All game assets are SVG files with dynamic rasterization
- Procedural variation per tile (color, scale, rotation)
- Inter-tile blending for seamless appearance
- Aggressive caching strategy (hybrid: pre-rasterize common + LRU for variants)
- LOD system (16/32/64/128px based on zoom)
- Created comprehensive tech doc: vector-asset-pipeline.md

**C++ Standards Established:**
- Naming: PascalCase classes/functions, camelCase variables, m_ members, k constants
- `#pragma once` for headers (not traditional guards)
- User's clang-format config: tabs, 140 col limit, align declarations
- clang-tidy with naming enforcement + modernization checks
- Manual formatting (Shift+Alt+F), not on-save

**Essential Engine Patterns Documented:**
- String hashing (FNV-1a) - compile-time hash for fast lookups
- Structured logging - categories + levels, debug logs compile out
- Memory arenas - linear allocators for temp data (10-100x faster)
- Resource handles - 32-bit IDs with generation (safe, hot-reload friendly)
- Immediate mode debug draw - simple API for visualization

**UI Testability Strategy:**
- Scene graph JSON export for inspection
- HTTP debug server (debug builds only)
- Visual regression tests (screenshot comparison)
- Inspector overlay (F12 in debug builds)
- Event recording/playback

**World Generation:**
- Pluggable IWorldGenerator interface
- Initial Perlin noise generator (TEMPORARY - marked for replacement)
- Progress reporting during generation
- Integration with 3D preview and 2D sampling

**Game Development Conventions:**
- Use "scene" terminology (SplashScene), not "screen"
- Assets called "SVG files", not "images" or "textures"
- Test UI components in ui-sandbox before using in main game

**Project Structure Created:**
- All library directories with CMakeLists.txt
- VSCode config (launch.json, tasks.json, settings.json)
- .clang-format and .clang-tidy configs
- .gitignore configured
- vcpkg.json with dependencies (including nanosvg for SVG)
- Asset directories: assets/tiles/{terrain,vegetation,structures}
- README.md with full setup instructions

**Next Session Should:**
1. Test that CMake configures properly
2. Implement string hashing system
3. Implement logging system
4. Begin creating actual library implementations
