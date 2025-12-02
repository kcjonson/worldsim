# Project Status

Last Updated: 2025-12-02 (Entity Placement System Spec Added)

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

### ✅ Chunk-Based World Rendering System
**Spec/Documentation:** `/docs/technical/chunk-management-system.md`
**Status:** complete

**Completed Tasks:**
- [x] Coordinate System
  - [x] ChunkCoordinate type with hash for unordered_map
  - [x] WorldPosition continuous 2D coordinates
  - [x] Coordinate transforms between chunk/world/screen
- [x] World Sampling Interface
  - [x] IWorldSampler abstract interface
  - [x] MockWorldSampler with simplex noise biomes
  - [x] ChunkSampleResult with sector grid (32×32) for O(1) tile lookup
- [x] Chunk System
  - [x] Chunk class with biome data and noise-based ground cover
  - [x] ChunkManager with dynamic load/unload (5×5 load, 7×7 unload)
  - [x] ChunkRenderer for tile grid rendering
- [x] Camera & Controls
  - [x] WorldCamera with WASD panning and scroll wheel zoom
  - [x] GameOverlay HUD (FPS, chunk count, camera position)
  - [x] ZoomControl UI component
- [x] World-Sim App Bootstrap
  - [x] SplashScene, MainMenuScene, GameScene
  - [x] Full game flow: Splash → Menu → Game
- [x] Testing
  - [x] ChunkCoordinate tests (hashing, neighbors, transforms)
  - [x] ChunkManager tests (load/unload lifecycle)
  - [x] MockWorldSampler tests (biome distribution, determinism)

**Result:** Playable world-sim with infinite pannable world and biome-based terrain ✅

---

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

## In Progress Epics

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

### 🔄 Asset System Architecture
**Spec/Documentation:** `/docs/technical/asset-system/README.md`
**Dependencies:** None
**Status:** in progress

**Phase 1: Core Infrastructure** ✅ COMPLETE
- [x] 1.1 Asset Registry
  - [x] Create `libs/engine/assets/` library
  - [x] Implement XML definition parser (pugixml)
  - [x] Create `AssetRegistry` class with `loadDefinitions()`, `getDefinition()`, `getTemplate()`
  - [x] Create `GeneratorRegistry` for procedural generators
  - [x] Template caching system
- [x] 1.2 Asset Loading Infrastructure
  - [x] SVG loading via nanosvg (SVGLoader)
  - [x] Bezier curve flattening (flattenCubicBezier, flattenQuadraticBezier)
  - [x] Tessellator (ear-clipping algorithm)
  - [x] GrassBladeGenerator (procedural Bezier blade generation)
  - [x] Asset definition XML format (grass.xml)

**Phase 1.5: Tile Integration** ✅ COMPLETE
- [x] 1.5.1 Wire Up Asset System
  - [x] Register GrassBlade generator at startup
  - [x] Load asset definitions at startup
- [x] 1.5.2 Tile System Foundation
  - [x] Create Tile struct (position, biome, dimensions)
  - [x] Create Biome enum (Grassland, Forest, etc.)
  - [x] Add Placement parsing to AssetRegistry (spawn chance, clumping)
- [x] 1.5.3 GrassScene Tile Integration
  - [x] Convert GrassScene to use tile grid
  - [x] Load grass via AssetRegistry::getTemplate()
  - [x] Spawn grass per-tile based on XML placement rules
  - [x] Test: verified 497 grass blades spawning via asset system

**Phase 2: Lua Scripting** ✅ COMPLETE
- [x] 2.1 Lua Integration
  - [x] Add sol2 (Lua 5.4.7) to vcpkg.json
  - [x] Create `LuaEngine` class with sandbox and seeded randomness
  - [x] Expose `Path`, `asset:addPath()` API to Lua
  - [x] Create `LuaGenerator` implementing `IAssetGenerator` interface
- [x] 2.2 Procedural Generator
  - [x] Implement seed-based variant generation (math.randomseed from C++)
  - [x] Demo: Deciduous tree generator in Lua (3/4 top-down Rimworld style)
  - [x] TreeScene demo rendering 40 unique trees
  - [x] XML definition format with scriptPath parameter

**Phase 3: Variant Caching** (FUTURE)
- [ ] Binary cache format for pre-generated variants
- [ ] Cache invalidation (script/definition hash comparison)

**Phase 4: Full Tree Demo** (FUTURE)
- [ ] Deciduous tree generator (Weber & Penn branching)
- [ ] Mixed flora scene (trees + flowers + grass)
- [ ] Performance target: 60 FPS with 1000 trees + 10,000 flowers

---

## Planned Epics

### Entity Placement System
**Spec/Documentation:** `/docs/technical/entity-placement-system.md`
**Dependencies:** Asset System Architecture (Phase 1 complete)
**Status:** ready

**Phase 1: Foundation**
- [ ] Extend AssetDefinition with groups and relationships fields
- [ ] Parse `<groups>` and `<relationships>` in AssetRegistry
- [ ] Build group index (group name → list of defNames) at load time
- [ ] Create DependencyGraph with topological sort
- [ ] Create SpatialIndex with grid-based queries
- [ ] Unit tests for all components

**Phase 2: Executor**
- [ ] Create PlacementExecutor
- [ ] Integrate with ChunkManager (including adjacent chunk queries)
- [ ] Wire up to GameScene

**Phase 3: Content**
- [ ] Add groups + relationships to existing assets (grass, trees)
- [ ] Visual validation in world-sim

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
