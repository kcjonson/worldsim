# Project Status

Last Updated: 2025-11-30 (Asset System Architecture Epic Added)

## Epic/Story/Task Template

Use this template for all work items:

```markdown
## Epic Title
**Spec/Documentation:** /path/to/spec.md or /path/to/folder/
**Dependencies:** Epic Name (if applicable)
**Status:** ready | in progress | blocked | needs spec

**Tasks:**
- [ ] Story Title
  - [ ] Task Title
    - [ ] Sub-task Title
```

**Notes:**
- Max 3 levels of nesting (Story → Task → Sub-task)
- Only last 4 completed epics + in-progress + planned epics shown here
- Completed epics move to development-log.md with context
- An epic is only complete when ALL tasks are [x]

---

## Recently Completed Epics (Last 4)

### ✅ Uber Shader - Unified Rendering Pipeline
**Spec/Documentation:** `libs/renderer/shaders/uber.vert`, `libs/renderer/shaders/uber.frag`
**Status:** complete

**Completed Tasks:**
- [x] Phase 1: Delete Old Renderers
  - [x] Delete TextBatchRenderer
  - [x] Delete separate text flush callback system
  - [x] Delete msdf_text.vert/frag shaders
- [x] Phase 2: Create Uber Shader
  - [x] Create uber.vert (unified vertex format)
  - [x] Create uber.frag (renderMode branching: SHAPE vs TEXT)
  - [x] Preserve MSDF text math exactly (copy-paste)
  - [x] Preserve SDF shape math exactly (copy-paste)
- [x] Phase 3: Unify BatchRenderer
  - [x] Add renderMode to vertex format (kRenderModeText = -1.0)
  - [x] Add addTextQuad() method to BatchRenderer
  - [x] Text interleaved with shapes in submission order
  - [x] Bind font atlas once at frame start
- [x] Phase 4: Update Consumers
  - [x] Update Text::Render() to use BatchRenderer::addTextQuad()
  - [x] Update Primitives API (getBatchRenderer() for direct access)
  - [x] Verified text quality unchanged
  - [x] Verified correct z-ordering and alpha blending

**Result:** Unified rendering pipeline - zero shader switches, single draw call, correct z-ordering ✅

---

### ✅ UI Sandbox & Observability Foundation
**Spec/Documentation:** `/docs/technical/observability/`
**Status:** complete

**Completed Tasks:**
- [x] Primitive Rendering API
  - [x] Implement DrawRect, DrawLine, DrawRectBorder
  - [x] Create batching system with OpenGL shaders
  - [x] Add transform and scissor stacks
  - [x] Fix pixel-perfect 1:1 coordinate mapping
- [x] HTTP Debug Server
  - [x] Implement lock-free ring buffer
  - [x] Create HTTP server with cpp-httplib
  - [x] Add REST endpoints (/api/health, /api/metrics)
  - [x] Implement Server-Sent Events streaming
  - [x] Create HTML UI for metrics display
- [x] Sandbox Control Endpoints
  - [x] Implement control actions (exit, scene switch, pause/resume)
  - [x] Add port conflict detection
  - [x] Create scene manager integration
- [x] Developer Client (TypeScript/React)
  - [x] Create React + TypeScript SPA
  - [x] Implement real-time metrics dashboard
  - [x] Build live log viewer with filtering
  - [x] Add localStorage persistence
  - [x] Integrate with CMake build system

---

### ✅ Colonysim UI Integration - Font Rendering
**Spec/Documentation:** `/docs/status.md` (Colonysim Integration Plan section)
**Status:** complete

**Completed Tasks:**
- [x] Add FreeType to vcpkg.json
- [x] Port FontRenderer class to libs/ui/font/
- [x] Port text shaders to shaders/
- [x] Copy Roboto-Regular.ttf font
- [x] Port Shader wrapper class
- [x] Create font rendering demo scene
- [x] Test rendering at various sizes and colors

**Result:** Working text rendering in ui-sandbox ✅

---

### ✅ SDF Text Rendering & Batched Command Queue
**Spec/Documentation:** `/docs/technical/ui-framework/sdf-text-rendering.md`, `/docs/technical/ui-framework/batched-text-rendering.md`
**Status:** complete

**Completed Tasks:**
- [x] Phase 1: SDF Atlas Generation Tool
  - [x] Add msdfgen to vcpkg.json
  - [x] Create tools/generate_sdf_atlas executable
  - [x] Generate Roboto-Regular SDF atlas + metadata JSON
  - [x] Test atlas loading in FontRenderer
  - [x] Fix horizontal squishing bug (atlasBounds support)
- [x] Phase 2: FontRenderer SDF Support
  - [x] Add LoadSDFFont() method (atlas + metadata)
  - [x] Implement GenerateGlyphQuads() for SDF
  - [x] Add GetSDFAtlas() for batch key
- [x] Phase 3: SDF Shaders
  - [x] Create sdf_text.vert shader
  - [x] Create sdf_text.frag with median filtering + smoothstep
  - [x] Add uPixelRange uniform
  - [x] Test anti-aliasing quality at multiple scales
- [x] Phase 4: Command Queue & Two-Pass Rendering
  - [x] Implement DrawText() in Primitives API (via TextBatchRenderer)
  - [x] Implement batched rendering with z-ordering
  - [x] Sort by z-index within text batches
  - [x] Implement state change minimization
- [x] Phase 5: Integration & Testing
  - [x] Update Text::Render() to use Primitives::DrawText()
  - [x] Implement text alignment system (horizontal + vertical)
  - [x] Add LRU glyph quad cache for performance
  - [x] Test button scene - text renders on top correctly
  - [x] Test at multiple scales - crisp at all sizes
  - [x] Performance test - single draw call batching verified
  - [x] Test transparency and z-ordering - working correctly
  - [x] Convert all UI components to use Text/Button
  - [x] Fix coordinate system unification

**Result:** SDF text rendering complete with proper z-ordering, batching, alignment, and performance optimization ✅

---

## In Progress Epics

### ✅ Colonysim UI Integration
**Spec/Documentation:** `/docs/status.md` (detailed plan), `/Volumes/Code/colonysim` (source code)
**Dependencies:** None
**Status:** complete

**Tasks:**
- [x] Compatibility Analysis ✅ COMPLETE (see development-log.md 2025-10-29)
- [x] Font Rendering System ✅ COMPLETE
- [x] Rendering Integration Decision ✅ COMPLETE (2025-10-29)
  - [x] Analyzed colonysim vs worldsim rendering patterns
  - [x] Researched memory patterns (shared_ptr vs value semantics)
  - [x] Decision: Pragmatic hybrid - Port colonysim components to use Primitives API
  - [x] Documented complete architecture in colonysim-integration-architecture.md
  - [x] Created component-storage-patterns.md research doc

**Phase 1: Port Components** (from colonysim-integration-architecture.md)
- [x] CoordinateSystem ✅ COMPLETE
  - [x] Create libs/renderer/coordinate_system/ library with CMakeLists.txt
  - [x] Port CoordinateSystem class (remove singleton, update naming conventions)
  - [x] Integrate with Primitives API (SetCoordinateSystem, expose projection/percent methods)
  - [x] Keep BatchRenderer using framebuffer size (physical pixels) for backward compatibility
  - [x] Update application initialization in ui-sandbox
  - [x] Unit tests: 18 tests covering initialization, percentage helpers, DPI conversion, projections
  - [x] All tests passing (43/43 renderer tests)
  - [x] Tested - rendering preserved, percentage helpers available for future UI components
  - [x] Design note: Worldsim uses physical pixels, CoordinateSystem provides DPI utilities
- [x] Layer System ✅ COMPLETE (PR #21 + PR #23)
  - [x] Layer system already existed (PR #21) with value semantics architecture
  - [x] Refactored API for ergonomics (PR #23)
  - [x] Added Container type for pure hierarchy nodes
  - [x] Implemented auto-zIndex based on insertion order
  - [x] Simplified creation API (one-call AddChild instead of 3-step pattern)
  - [x] Stable sort for equal zIndex (CSS-like behavior)
  - [x] Z-index sorting with dirty flag optimization working
  - [x] layer_scene demo showing auto and explicit zIndex modes
  - [x] 37 tests passing including new auto-zIndex and stable sort tests
- [x] Shape System ✅ COMPLETE
  - [x] Create libs/ui/shapes/ library (already existed)
  - [x] Port Shape base class (value semantics, no base class needed)
  - [x] **CRITICAL**: Rewrite render() methods to call Primitives API
    - [x] Rectangle ✅ DONE
    - [x] Line ✅ DONE
    - [x] Circle ✅ DONE (DrawCircle implemented with tessellation)
    - [x] Text ✅ DONE (integrated FontRenderer)
  - [x] Port Rectangle, Circle, Line shapes (all complete with proper styles)
  - [x] Adapt Text shape to use worldsim's FontRenderer
  - [x] Create shapes demo (layer_scene demonstrates all shapes)
  - [x] Test all shape types rendering (verified via screenshot)
  - [x] Test shapes nested in layers (working correctly)
- [x] InputManager 
  - [x] Create libs/engine/input/ library
  - [x] Port InputManager from colonysim
  - [x] Application-owned singleton pattern (matches SceneManager)
  - [x] Adapt GLFW callbacks to worldsim conventions
  - [x] Integration with Application::Update() → InputManager::Update() → Scene::HandleInput()
  - [x] InputTestScene for testing and demonstration
- [x] Style System 
  - [x] Style system already exists in primitive_styles.h (struct-based, not class-based)
  - [x] Decision: Enhance worldsim's existing system instead of porting colonysim's classes
  - [x] BorderStyle, RectStyle, LineStyle, CircleStyle, TextStyle already exist
  - [x] Already integrated with Primitives API and shapes
  - [ ] Enhancements (via GPU-Based SDF Rendering epic):
    - [ ] BorderPosition enum will be added by SDF Rendering implementation
    - [ ] Opacity support will be added by SDF Rendering implementation
    - [ ] Corner radius rendering will be implemented in fragment shader
  - [x] Note: colonysim's class-based style hierarchy not needed (worldsim's struct approach is simpler and more performant)
- [ ] UI Components
  - [x] Create libs/ui/components/button/ library
  - [x] Port Button (state machine, onClick callbacks)
  - [x] Adapt input handling to use InputManager with abstraction enums (Key, MouseButton)
  - [x] Use Primitives API for rendering
  - [x] Create button demo scene
  - [x] Test mouse and keyboard interactions
  - [x] Implement Batched Text Rendering
  - [x] Create Keyboard Focus Manager (global Tab navigation system)
  - [x] Port TextInput (cursor, focus, text editing)
  - [x] Create text input demo
- [x] UI Component Architecture 
  - [x] Create architecture documentation (/docs/technical/ui-framework/architecture.md)
  - [x] Define ILayer interface (HandleInput/Update/Render lifecycle)
  - [x] Define IFocusable interface (focus management)
  - [x] Create LayerHandle type for safe layer references
  - [x] Add lifecycle methods to all shapes
  - [x] Add interface implementations to Button and TextInput
  - [x] Document unified layer model (shapes + components)
  - [x] Document handle-based references vs shared_ptr
  - [x] Document FocusManager with IFocusable interface


**Phase 2: Integration & Testing** ✅ COMPLETE
- [x] Integration testing
  - [x] Test component interactions (FocusManager.test.cpp: 50+ tests)
  - [x] Test event propagation (InputManager → FocusManager flow tested)
  - [x] Test layout with multiple components (Layer.test.cpp: z-index, children)
  - [x] Verify all demos working (12 demo scenes, all compile and run)
  - [x] Unit tests: 100% passing (foundation-tests, renderer-tests, ui-tests)
  - [x] CI/CD validates on every PR

**Phase 3: Profile and Measure** ✅ COMPLETE
- [x] Measure performance characteristics
  - [x] Arena allocator benchmarks (14× faster than malloc)
  - [x] Clipping system benchmarks (Clipping.bench.cpp)
  - [x] Vector rendering performance (10,000 animated stars @ 60 FPS)
  - [x] Text rendering profiled (SDF with LRU cache)
  - [x] Batched rendering verified (single draw call)

**Phase 4: Optimize If Needed** ✅ COMPLETE
- [x] Value semantics implemented throughout (Layer, shapes - no shared_ptr overhead)
- [x] Handle-based references for safe layer references
- [x] LRU glyph quad cache for text performance
- [x] Transform caching (identity transform check optimization)
- [x] Dirty flag optimization for z-index sorting

**Future Enhancements** (deferred)
- [ ] RmlUI backend implementation
- [ ] SDF font rendering integration with RmlUI

**Result:** Complete UI framework with Layer system, Shapes (Rectangle, Circle, Line, Text), Button, TextInput, FocusManager, InputManager, ClipboardManager, and Clipping/Scrolling. All components tested via 100+ unit tests and 12 demo scenes ✅

---

### ✅ PR #30 Feedback Fixes - TextInput & Focus Management
**Spec/Documentation:** PR #30 review comments, `/Users/kcjonson/.claude/plans/whimsical-crafting-snowglobe.md`
**Dependencies:** Colonysim UI Integration (TextInput component)
**Status:** complete

**Tasks:**
- [x] FocusManager Static Instance Pattern
  - [x] Add static `s_instance` member to FocusManager
  - [x] Add static `Get()` method
  - [x] Add static `SetInstance()` method
  - [x] Follow InputManager pattern
- [x] Code Quality Fixes
  - [x] Add m_tabIndex preservation in TextInput move operations
  - [x] Add m_tabIndex preservation in Button move operations
  - [x] Remove debug LOG_INFO calls in TextInput::InsertChar
- [x] Remove charValidator Functionality
  - [x] Remove charValidator from TextInput::Args struct
  - [x] Remove m_charValidator member
  - [x] Remove charValidator logic from HandleCharInput/InsertChar/Paste
  - [x] Remove numbers-only demo from text_input_scene.cpp
- [x] Create ClipboardManager Abstraction
  - [x] Create libs/engine/clipboard/clipboard_manager.h
  - [x] Create libs/engine/clipboard/clipboard_manager.cpp
  - [x] Add to libs/engine/CMakeLists.txt
  - [x] Follow InputManager singleton pattern
  - [x] Update Application to own ClipboardManager
  - [x] Update TextInput to use ClipboardManager instead of GLFW
- [x] Fix BASE_FONT_SIZE Constant
  - [x] Move to file-level kBaseFontSize constant in anonymous namespace

---

### ✅ Clipping and Scrolling System
**Spec/Documentation:** `/docs/technical/ui-framework/clipping.md`
**Dependencies:** None
**Status:** complete

**Completed Tasks:**
- [x] Phase 1: Shader Fast Path (Rect Clipping)
  - [x] Create clip_types.h with ClipShape, ClipMode, ClipSettings
  - [x] Add clipBounds to UberVertex struct
  - [x] Update VAO setup in batch_renderer.cpp
  - [x] Add clip test to uber.frag shader
  - [x] Implement PushClip/PopClip in primitives.cpp
  - [x] Fix DPI scaling for Retina displays (physical vs logical pixels)
- [x] Demo Scenes
  - [x] Create clip_scene.cpp with visual feature tests (shapes + text clipping)
  - [x] Modify vector_perf_scene.cpp for performance testing (C key toggle)
- [x] Phase 2: Content Offset (Scrolling)
  - [x] Add clip and contentOffset properties to Container
  - [x] Implement Container::Render() with clip/offset
  - [x] Wire up transform stack to BatchRenderer (bake transforms into vertices)
  - [x] Update ClipScene with scrollable Container demo (Section 4)
- [x] Automated Performance Benchmarks
  - [x] Create Clipping.bench.cpp with stack, intersection, and fragment shader benchmarks
- [x] Stub Functions for Future Phases
  - [x] Add pushClipRoundedRect(), pushClipCircle(), pushClipPath() convenience functions
  - [x] Document ClipMode::Outside as not-yet-implemented (Phase 3)

**Known Issues:**
- Primitives::DrawText is a stub (dependency architecture issue - see development-log.md 2025-11-29)
- Use UI::Text component for text rendering instead

**Result:** Shader-based clipping with full batching preservation, content offset for scrolling, and performance benchmarks ✅

---

### ✅ Vector Graphics Validation - Grass Blades
**Spec/Documentation:** `/docs/technical/vector-graphics/validation-plan.md`
**Dependencies:** None
**Status:** complete (validation phase - see Animation Performance epic for optimization)

**Tasks:**
- [x] Single Grass Blade with Bezier Curves
  - [x] Implement cubic Bezier curve tessellation (De Casteljau's algorithm)
  - [x] Create single grass blade shape with curves (GrassScene.cpp)
  - [x] Render in ui-sandbox (8-blade cluster demo)
  - [x] Verify visual quality and smoothness
- [x] 10,000 Static Grass Blades
  - [x] Generate 10,000 grass blade instances
  - [x] Apply procedural variation (height, width, curve, color)
  - [x] Implement batch rendering (single draw call with per-vertex colors)
  - [x] Performance: ~9ms frame time (24,832 triangles, 44,832 vertices)
- [x] 10,000 Animated Grass Blades ⚠️ **NO-GO for naive CPU tessellation**
  - [x] Implement wind simulation (sine waves + noise)
  - [x] Animate all 10,000 blades independently
  - [x] Retessellate curves per frame
  - [x] Profile tessellation cost: **~65ms per frame (4x budget)**
  - [x] Result: **12 FPS** (target was 60 FPS)
  - [x] **GO/NO-GO Decision: ❌ NO-GO for CPU tessellation**
  - [x] Recommendation: See Animation Performance Optimization epic

**Result:** Validation complete. Static rendering works well. Animated requires optimization (see next epic).

---

### 🔄 Animated Vector Graphics Performance Optimization
**Spec/Documentation:** `/docs/technical/vector-graphics/animation-performance.md`
**Dependencies:** Vector Graphics Validation (completed)
**Status:** in progress

**Problem:** 10,000 animated grass blades run at 12 FPS (target 60 FPS). CPU tessellation takes ~65ms/frame (77% in Bezier flattening).

**Solution:** Tiered animation system - GPU instancing for simple flora, optimized CPU tessellation for complex flora.

**Phase 1: CPU Optimization Stack** (validates Bezier deformation can hit 60 FPS)
- [ ] 1.1 Arena Allocator Integration
  - [ ] Add arena parameter to `flattenCubicBezier()`
  - [ ] Add arena to `Tessellator::Tessellate()`
  - [ ] Create per-frame arena in GrassScene
- [ ] 1.2 Temporal Coherence System
  - [ ] Store previous frame's tessellated mesh per blade
  - [ ] Calculate deformation delta threshold
  - [ ] Skip retessellation if delta < threshold
- [ ] 1.3 SIMD Bezier Flattening
  - [ ] Create `flattenCubicBezierSIMD()` (4 curves in parallel)
  - [ ] Use ARM NEON intrinsics
  - [ ] Vectorize midpoint calculations
- [ ] 1.4 Benchmark Phase 1
  - [ ] Target: 45-60 FPS with 10,000 blades

**Phase 2: GPU Instancing Path** (maximum performance for simple flora)
- [ ] 2.1 Instanced Rendering Infrastructure
  - [ ] Add `addInstancedGeometry()` to BatchRenderer
  - [ ] Create instance data buffer (VBO with divisor=1)
  - [ ] Add `glDrawElementsInstanced` path
- [ ] 2.2 Vertex Shader Animation
  - [ ] Add `u_time` uniform to uber shader
  - [ ] Add instance data attributes
  - [ ] Implement wind displacement in vertex shader
- [ ] 2.3 Instanced Grass Demo
  - [ ] Create `GrassInstancedScene.cpp`
  - [ ] Target: 100,000+ instances at 60 FPS

**Phase 3: Tiered System Integration**
- [ ] 3.1 Asset Classification (simple vs complex)
- [ ] 3.2 Runtime Tier Selection
- [ ] 3.3 Mixed Flora Demo (grass + trees + bushes)

---

## Planned Epics

### Asset System Architecture
**Spec/Documentation:** `/docs/technical/asset-system/README.md`
**Dependencies:** None
**Status:** ready

**Overview:** Data-driven asset system supporting simple (SVG) and procedural (Lua-generated) assets with modding support and pre-generation caching.

**Phase 1: Core Infrastructure**
- [ ] 1.1 Asset Registry
  - [ ] Create `libs/engine/assets/` library
  - [ ] Implement XML definition parser (RapidXML or pugixml)
  - [ ] Create `AssetRegistry` class with `LoadDefinitions()`, `GetAsset()`
  - [ ] Implement definition inheritance (`ParentDef` support)
  - [ ] Add mod loading order support
- [ ] 1.2 Simple Asset Loader
  - [ ] Implement SVG loading via existing nanosvg
  - [ ] Create `SimpleAssetLoader` that tessellates SVG → mesh
  - [ ] Store tessellated meshes in registry
  - [ ] Add color/scale/rotation variation support
- [ ] 1.3 Integration with Renderer
  - [ ] Connect `AssetRegistry` to `BatchRenderer`
  - [ ] Add `renderAsset(defName, position, seed)` API
  - [ ] Demo: Simple flower field using asset definitions

**Phase 2: Lua Scripting**
- [ ] 2.1 Lua Integration
  - [ ] Add sol2 or LuaJIT to vcpkg.json
  - [ ] Create `LuaEngine` class with sandbox restrictions
  - [ ] Expose `VectorAsset`, `VectorPath` API to Lua
  - [ ] Expose `Color`, `Vec2`, `Math`, `Ease` utilities
- [ ] 2.2 Procedural Generator
  - [ ] Create `ProceduralAssetGenerator` class
  - [ ] Implement variant pre-generation at load time
  - [ ] Add seeded RNG for deterministic generation
  - [ ] Demo: Procedural bush with Lua script

**Phase 3: Variant Caching**
- [ ] 3.1 Binary Cache Format
  - [ ] Design cache file format (header + tessellated meshes)
  - [ ] Implement save/load for variant cache
  - [ ] Add cache invalidation (script hash comparison)
- [ ] 3.2 Load-Time Generation
  - [ ] Generate all variants during loading screen
  - [ ] Progress reporting for loading UI
  - [ ] Parallel generation where possible

**Phase 4: Full Tree Demo**
- [ ] 4.1 Deciduous Tree Generator
  - [ ] Implement Weber & Penn-style branching in Lua
  - [ ] Leaf placement from SVG template
  - [ ] Generate 200 variants at load time
- [ ] 4.2 Mixed Flora Scene
  - [ ] Create scene with procedural trees + simple flowers
  - [ ] Verify GPU instancing for simple assets
  - [ ] Verify variant selection for procedural assets
  - [ ] Performance target: 60 FPS with 1000 trees + 10,000 flowers

---

### Unit Testing Infrastructure
**Spec/Documentation:** `/docs/technical/unit-testing-strategy.md`, `/docs/technical/testing-guidelines.md` (TBD)
**Research:** `/docs/research/cpp-test-framework-research.md`
**Dependencies:** None
**Status:** in progress

**Tasks:**
- [x] Framework Research & Selection
  - [x] Write research document comparing frameworks (cpp-test-framework-research.md)
  - [x] Create technical specification (unit-testing-strategy.md)
  - [x] Get approval on framework choice (Google Test + Google Benchmark)
  - [x] Document test organization decision in strategy doc (collocated tests with `*.test.cpp` and `*.bench.cpp` naming)
- [x] Test Infrastructure Setup ✅ COMPLETE
  - [x] Add chosen framework to vcpkg.json (gtest, benchmark)
  - [x] Update all library CMakeLists.txt to use file globbing for test discovery
  - [x] Create example tests to verify infrastructure (arena.test.cpp, arena.bench.cpp)
  - [x] Verify local execution via ctest (100% tests passed)
  - [x] Move enable_testing() before subdirectories in root CMakeLists.txt
- [x] Unit Tests - Foundation Library ✅ COMPLETE
  - [x] Logging system tests ✅ 12 tests passing
  - [x] Memory arena tests (Arena, FrameArena, ScopedArena) ✅ 18 tests passing
  - [x] Memory arena benchmarks (vs malloc, batch allocations, alignment) ✅ passing
  - [x] String hashing tests (FNV-1a, collision detection) ✅ 20 tests passing
- [ ] Unit Tests - Engine Library
  - [ ] Application lifecycle tests (with mocked GLFW)
  - [ ] Scene management tests
  - [ ] Core ECS tests (entity creation, component storage)
- [ ] Unit Tests - Renderer Library
  - [ ] Shader compilation tests (mock GL context)
  - [ ] Vertex buffer management tests
  - [x] Resource handle tests (ResourceHandle, ResourceManager) ✅ 25 tests passing
- [x] GitHub Actions CI/CD Integration ✅ COMPLETE
  - [x] Created three separate workflow files (build.yml, code-quality.yml, tests.yml)
  - [x] Build workflow: validates Release builds on Ubuntu
  - [x] Code quality workflow: clang-format and clang-tidy checks
  - [x] Tests workflow: runs CTest with BUILD_TESTING=ON, excludes benchmarks
  - [x] All workflows trigger on PRs and pushes to main
  - [x] Test results uploaded as artifacts
  - [x] PRs fail if any check fails
  - [x] Environment matched to local setup (Unix Makefiles, no Ninja)
  - [x] All X11 dependencies configured for Ubuntu (libxmu-dev, libxi-dev, libgl-dev, libxrandr-dev, libxinerama-dev, libxcursor-dev, libx11-dev)
- [ ] Documentation
  - [ ] Create testing-guidelines.md (patterns, conventions, mocking)
  - [ ] Update README.md with testing section
  - [ ] Update workflows.md with testing workflow
  - [ ] Update development-log.md with implementation notes

---

### Vector Graphics System - Full Implementation
**Spec/Documentation:** `/docs/technical/vector-graphics/INDEX.md`
**Dependencies:** Vector Graphics Validation - Grass Blades
**Status:** needs validation results

**Tasks:**
- [ ] Foundation
  - [ ] Design core architecture (AssetManager, Tessellator, Renderer)
  - [ ] Implement SVG parser (nanosvg or similar)
  - [ ] Implement path tessellation (lines, curves, arcs)
  - [ ] Basic shape rendering (no animation)
  - [ ] Integration with BatchRenderer
- [ ] Animation System
  - [ ] Design animation API (keyframes, interpolation, spline deformation)
  - [ ] Implement spline deformation for organic shapes
  - [ ] Implement vertex shader transforms
  - [ ] Create animation demo scene
- [ ] Batching + Atlasing
  - [ ] Implement texture atlas system
  - [ ] Implement instanced rendering
  - [ ] Implement dynamic atlas updates
  - [ ] Profile and optimize draw call count
  - [ ] Verify: <100 draw calls for 10,000+ shapes
- [ ] Tiered System
  - [ ] Design tier architecture (baked, cached, dynamic)
  - [ ] Implement baked tier (atlas at load time)
  - [ ] Implement cached tier (periodic rebuilds)
  - [ ] Implement dynamic tier (per-frame updates)
  - [ ] Create heuristics for tier assignment
- [ ] Collision + Interaction
  - [ ] Generate collision shapes from SVG paths
  - [ ] Integrate with physics system
  - [ ] Implement click/hover detection
  - [ ] Add debug visualization for collision shapes
- [ ] Optimization + Polish
  - [ ] Performance Profiling
    - [ ] Measure tessellation time per frame
    - [ ] Count draw calls per frame
    - [ ] Monitor VBO update bandwidth
    - [ ] Check texture atlas usage
    - [ ] Profile memory allocations during animation updates
    - [ ] Verify 60 FPS with target entity count
  - [ ] Profile full system end-to-end
  - [ ] Optimize hot paths
  - [ ] Add memory budget management
  - [ ] Implement asset streaming
  - [ ] Write performance documentation
  - [ ] Create artist guidelines for SVG creation


---

### Vector Asset Pipeline - Tile Rendering
**Spec/Documentation:** `/docs/technical/vector-graphics/asset-pipeline.md`
**Dependencies:** Vector Graphics System - Full Implementation
**Status:** planned

**Tasks:**
- [ ] Core Pipeline
  - [ ] SVG loading system
  - [ ] Vector to raster conversion
  - [ ] Tile variation system (procedural parameters)
  - [ ] Raster caching with LRU eviction
  - [ ] Memory management and budgets
- [ ] Advanced Features
  - [ ] Inter-tile blending for visual cohesion
  - [ ] LOD system for distant tiles
  - [ ] Performance profiling and optimization

---

### Observability System - Full Feature Set
**Spec/Documentation:** `/docs/technical/observability/INDEX.md`
**Dependencies:** None
**Status:** in progress

**Tasks:**
- [x] Developer Server Backend - Core
  - [x] Metrics collection (FPS, frame time, draw calls)
  - [x] Log streaming with ring buffer
  - [x] SSE streaming endpoints
  - [x] JSON serialization
- [ ] Developer Server Backend - Advanced
  - [ ] Frame profiler with hierarchical zones
  - [ ] GPU profiler integration
  - [ ] Export profiling data
  - [ ] Custom metrics API for game systems
- [x] Developer Client Frontend - Advanced Features
  - [x] Time-series charts for metrics
  - [x] Log filtering by category/level
  - [x] Log search functionality
  - [x] Color coding by level
  - [x] Configurable retention policies
- [ ] UI Inspection System
  - [ ] Scene graph JSON serialization
  - [ ] UI state streaming via SSE
  - [ ] Hover data collection
  - [ ] Event streaming
  - [ ] Hierarchy path display

---

### World Generation System
**Spec/Documentation:** `/docs/technical/world-generation-architecture.md`
**Dependencies:** None
**Status:** needs spec

**Tasks:**
- [ ] Core Architecture
  - [ ] Define generator interface (abstract base class)
  - [ ] Create generator registry system
  - [ ] Implement seed-based deterministic generation
  - [ ] Integrate with world-creator scene
- [ ] Temporary: Perlin Noise Generator
  - [ ] Implement 2D noise function
  - [ ] Add octave layering for detail
  - [ ] Create height map generation
  - [ ] Map elevation to tile types
  - [ ] Implement simple biome placement
- [ ] Future: Spherical World Generation
  - [ ] 3D spherical coordinate system
  - [ ] Latitude-based biome distribution
  - [ ] Tectonic plate simulation
  - [ ] Erosion and river formation
  - [ ] Ocean and continent generation

---

### Diagnostic Drawing System
**Spec/Documentation:** `/docs/technical/diagnostic-drawing.md`
**Dependencies:** None
**Status:** Deferred

**Tasks:**
- [ ] Core Implementation
  - [ ] Create DebugDraw class (immediate mode API)
  - [ ] Implement line primitive
  - [ ] Implement box primitive
  - [ ] Implement sphere primitive
  - [ ] Implement 2D primitives
  - [ ] Add batched rendering
  - [ ] Add runtime toggle (development builds only)
- [ ] Integration
  - [ ] Text rendering integration
  - [ ] Example usage documentation
  - [ ] Integration with ui-sandbox
  - [ ] Integration with main game

---

### GPU-Based SDF Rendering for UI Primitives
**Spec/Documentation:** `/docs/technical/ui-framework/sdf-rendering.md`
**Dependencies:** None
**Status:** ready

**Performance Goals:**
- 5x geometry reduction (4 vertices vs 20 per bordered rect)
- <1ms frame time for 1000 bordered rectangles
- Corner radius support with perfect anti-aliasing
- Inside/Outside/Center border positioning

**Tasks:**
- [ ] Phase 1: Core Implementation
  - [ ] Update PrimitiveVertex struct with new fields (rectLocalPos, borderData, shapeParams)
  - [ ] Implement SDF vertex shader (pass-through with new attributes)
  - [ ] Implement SDF fragment shader with sdRoundedBox function
  - [ ] Add border positioning logic (Inside/Center/Outside)
  - [ ] Add anti-aliasing with smoothstep and screen-space derivatives
  - [ ] Update BatchRenderer::Init() for new vertex attribute layout
  - [ ] Modify BatchRenderer::AddQuad() to calculate rect-local coordinates
  - [ ] Update primitive_styles.h with BorderPosition enum
  - [ ] Remove DrawLine calls from DrawRect in primitives.cpp
  - [ ] Update CompileShader() with new shader sources
- [ ] Phase 2: Testing
  - [ ] Write unit tests for vertex data correctness
  - [ ] Write unit tests for SDF function accuracy (CPU reference implementation)
  - [ ] Write unit tests for border positioning logic
  - [ ] Create integration tests for rendering correctness (framebuffer pixel checking)
  - [ ] Create visual test scene (sdf_test_scene.cpp) with corner radius variations
  - [ ] Create visual test scene for all border positions (Inside/Center/Outside)
  - [ ] Test anti-aliasing quality at different zoom levels
  - [ ] Test edge cases (zero radius, zero border, tiny rects)
- [ ] Phase 3: Performance Validation
  - [ ] Implement benchmark suite for 1000+ rectangles with borders
  - [ ] Measure frame time vs old implementation (target: 3x faster)
  - [ ] Verify geometry reduction (4 vertices per rect, not 20)
  - [ ] Profile GPU with RenderDoc or Nsight (fragment shader cost should be <0.5ms)
  - [ ] Verify batching preserved (single draw call)
  - [ ] Measure upload bandwidth reduction (target: 2.5x)
- [ ] Phase 4: Polish & Documentation
  - [ ] Test on various DPI displays (Retina, standard)
  - [ ] Tune anti-aliasing parameters if needed
  - [ ] Verify color accuracy and visual quality
  - [ ] Capture reference screenshots for regression testing
  - [ ] Update primitive-rendering-api.md with SDF section
  - [ ] Update sdf-rendering.md spec with findings
  - [ ] Code review and address feedback

---

### Core System Integrations & Polish
**Spec/Documentation:** Various technical docs in `/docs/technical/`
**Dependencies:** ECS System, Config System, World Generation System
**Status:** planned (blocked on dependencies)

**Tasks:**
- [ ] String Hashing System Integrations
  - [ ] ECS integration (depends on ECS system)
  - [ ] Resource manager integration (depends on named resource system)
  - [ ] Config system integration (depends on config system)
- [ ] Logging System Enhancements
  - [ ] File output support
  - [ ] Config integration (depends on config system)
- [ ] Memory Arena Integrations
  - [ ] Integration with chunk generation (depends on world generation)
  - [ ] Integration with frame loop (verify FrameArena usage)
  - [ ] Performance profiling and optimization

---

### Game Design Documentation
**Spec/Documentation:** `/docs/design/game-overview.md`, `/docs/design/INDEX.md`
**Dependencies:** None
**Status:** needs completion

**Tasks:**
- [ ] Game Overview Completions
  - [ ] Define game win condition
  - [ ] Define game lose condition
  - [ ] Define core game loop
- [ ] Application Flow Design
  - [ ] Splash screen sequence
  - [ ] Main menu navigation
  - [ ] Scene transitions and state management
- [ ] Main Menu Design
  - [ ] Menu options and layout
  - [ ] Settings screens
  - [ ] Save/load game UI
- [ ] Game Scene Design
  - [ ] Top-down 2D tile-based view
  - [ ] Camera system
  - [ ] HUD and UI overlay
- [ ] Camera Controls Specification
  - [ ] Pan controls (WASD, arrows, edge scrolling)
  - [ ] Zoom levels and limits
  - [ ] Focus and tracking modes
- [ ] Infinite World Design
  - [ ] Chunk loading system from player perspective
  - [ ] Streaming and LOD behavior
  - [ ] Performance implications
- [ ] UI Component Library
  - [ ] Overview of UI elements from player perspective
  - [ ] Interaction patterns
  - [ ] Visual style guide

---

## Blockers & Issues

None currently
