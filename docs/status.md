# Project Status

Last Updated: 2025-12-03 (Folder-Based Asset Migration Complete)

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

### ✅ Unit Testing Infrastructure
**Spec/Documentation:** `/docs/technical/unit-testing-strategy.md`, `/docs/research/cpp-test-framework-research.md`
**Status:** complete

**Completed Tasks:**
- [x] Framework Research & Selection
  - [x] Write research document comparing frameworks
  - [x] Create technical specification
  - [x] Get approval on framework choice (Google Test + Google Benchmark)
  - [x] Document test organization (collocated `*.test.cpp` and `*.bench.cpp`)
- [x] Test Infrastructure Setup
  - [x] Add gtest and benchmark to vcpkg.json
  - [x] Update all library CMakeLists.txt with file globbing
  - [x] Create example tests to verify infrastructure
  - [x] Verify local execution via ctest
  - [x] Move enable_testing() before subdirectories in root CMakeLists.txt
- [x] Unit Tests - Foundation Library (50+ tests)
  - [x] Logging system tests (12 tests)
  - [x] Memory arena tests (18 tests)
  - [x] Memory arena benchmarks
  - [x] String hashing tests (20 tests)
- [x] Unit Tests - Engine Library (80+ tests)
  - [x] ChunkCoordinate tests (hashing, neighbors, transforms)
  - [x] ChunkManager tests (load/unload lifecycle)
  - [x] MockWorldSampler tests (biome distribution, determinism)
  - [x] DependencyGraph tests (topological sort)
  - [x] SpatialIndex tests (grid-based queries)
  - [x] PlacementExecutor tests (entity placement)
- [x] Unit Tests - Renderer Library (25+ tests)
  - [x] ResourceHandle/ResourceManager tests
  - [x] CoordinateSystem tests
  - [x] Clipping benchmarks
- [x] Unit Tests - UI Library
  - [x] FocusManager tests
  - [x] Layer tests
- [x] GitHub Actions CI/CD Integration
  - [x] tests.yml workflow (runs on PRs, excludes benchmarks)
  - [x] Test results uploaded as artifacts
  - [x] PRs fail if tests fail
- [x] Documentation
  - [x] README.md testing section
  - [x] workflows.md testing workflow

**Result:** 298+ test assertions across 7 test executables, full CI/CD integration ✅

---

### ✅ Folder-Based Asset Migration
**Spec/Documentation:** `/docs/technical/asset-system/folder-based-assets.md`
**Dependencies:** Asset System Architecture - Foundation
**Status:** complete

**Goal:** Migrate from flat file structure to folder-per-asset organization for cleaner modding and content management.

**Completed Tasks:**
- [x] File Structure Migration
  - [x] Create 3-level hierarchy (`assets/world/flora/GrassBlade/`)
  - [x] Move grass.xml → `assets/world/flora/GrassBlade/GrassBlade.xml`
  - [x] Move trees.xml → `assets/world/flora/MapleTree/MapleTree.xml` + `OakTree/OakTree.xml`
  - [x] Create `assets/shared/scripts/` with `@shared/` prefix support
- [x] AssetRegistry Updates
  - [x] Add `baseFolder` field to AssetDefinition for path resolution
  - [x] Update `loadDefinitionsFromFolder()` to scan `FolderName/FolderName.xml` pattern
  - [x] Implement `@shared/` prefix for shared Lua scripts
  - [x] Simplify AppConfig to single `assetsRootPath`
- [x] Validation
  - [x] All 3 existing assets load correctly
  - [x] All 7 test suites pass
  - [x] GameScene renders trees and grass correctly

**Result:** Self-contained folder-per-asset structure with shared script support via `@shared/` prefix ✅

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

### ✅ Asset System Architecture - Foundation
**Spec/Documentation:** `/docs/technical/asset-system/README.md`
**Dependencies:** None
**Status:** complete

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

**Phase 2.5: Coordinate System Standardization** ✅ COMPLETE
- [x] Standardize all assets to meter-based coordinate system
  - [x] Update grass.xml with meter dimensions (0.2-0.5m height)
  - [x] Update trees.xml with meter dimensions (1.2-1.5m trunk heights, scaled for top-down view)
  - [x] Fix EntityRenderer scale: `pixelsPerMeter * zoom`
- [x] Fix BatchRenderer metrics
  - [x] Add cumulative frame counters (frameVertexCount, frameTriangleCount)
  - [x] Track stats before buffer clear, reset in beginFrame

**Result:** Working asset system with Lua scripting, procedural trees, grass, and entity placement ✅

---

### 🔄 Entity Placement System
**Spec/Documentation:** `/docs/technical/entity-placement-system.md`
**Dependencies:** Asset System Architecture (Phase 1 complete)
**Status:** in progress

**Phase 1: Foundation** ✅ COMPLETE
- [x] Extend AssetDefinition with groups and relationships fields
- [x] Parse `<groups>` and `<relationships>` in AssetRegistry
- [x] Build group index (group name → list of defNames) at load time
- [x] Create DependencyGraph with topological sort
- [x] Create SpatialIndex with grid-based queries
- [x] Unit tests for all components (46 tests passing)

**Phase 2: Executor** ✅ COMPLETE
- [x] Create PlacementExecutor (main orchestration engine)
  - [x] Dependency graph building from "requires" relationships
  - [x] Deterministic RNG seeding from chunk coordinates
  - [x] Tile-by-tile placement with biome/proximity checks
  - [x] Relationship modifier calculation (requires/affinity/avoids)
  - [x] Cross-chunk queries via IAdjacentChunkProvider interface
- [x] Integrate with GameScene
  - [x] PlacementExecutor initialization at game start
  - [x] Chunk processing coordination (processNewChunks, cleanupUnloadedChunks)
  - [x] PlacementExecutor serves as its own adjacent chunk provider
- [x] Unit tests (17 new tests, 63 total passing)

**Phase 3: Content** ✅ COMPLETE
- [x] Add groups + relationships to existing assets (grass, trees)
  - [x] Trees: groups (trees, deciduous_trees, large_flora), relationships (avoids same/group)
  - [x] Grass: groups (grass, small_flora, ground_cover), relationships (affinity for trees)
- [x] Visual validation in world-sim (63 tests pass, entities placing in chunks)

---

## Planned Epics

### Simple Asset Support (SVG-Only)
**Spec/Documentation:** `/docs/technical/asset-system/asset-definitions.md`
**Dependencies:** Folder-Based Asset Migration
**Status:** ready

**Goal:** Support hand-crafted SVG assets without Lua scripts (flowers, mushrooms, rocks).

**Tasks:**
- [ ] Simple Asset Loader
  - [ ] Detect `assetType="simple"` in XML
  - [ ] Load SVG directly via `<svgPath>` element
  - [ ] Tessellate once at load time
  - [ ] Store in GPU buffer for instancing
- [ ] Variation System
  - [ ] Parse `<variation>` element (colorRange, scaleRange, rotationRange)
  - [ ] Apply per-instance hue/saturation/value shifts
  - [ ] Apply per-instance scale and rotation
- [ ] Multi-Variant Simple Assets
  - [ ] Parse `<components>` with `variant="random"`
  - [ ] Support multiple SVG variants per asset
  - [ ] Random variant selection by seed
- [ ] Integration
  - [ ] PlacementExecutor spawns simple assets
  - [ ] EntityRenderer handles variation params
  - [ ] Verify GPU instancing performance

---

### Flora Content Pack
**Spec/Documentation:** `/docs/technical/asset-system/`
**Dependencies:** Simple Asset Support
**Status:** ready

**Goal:** Add visual variety with flowers, mushrooms, rocks, and bushes.

**Tasks:**
- [ ] Create Simple Flora Assets
  - [ ] Daisy (white/yellow flower, Grassland)
  - [ ] Poppy (red flower, Grassland)
  - [ ] Mushroom (multiple cap color variants, Forest)
  - [ ] Fern (Forest floor cover)
- [ ] Create Terrain Assets
  - [ ] Small Boulder (gray rock, all biomes)
  - [ ] Pebbles (cluster of small stones)
- [ ] Create Procedural Flora
  - [ ] Bush (Lua generator, medium complexity)
  - [ ] Conifer Tree (Lua generator, Forest biome)
- [ ] Placement Rules
  - [ ] Configure biome spawn chances for all assets
  - [ ] Set up group relationships (flowers near grass, mushrooms under trees)
  - [ ] Test mixed flora scenes
- [ ] Visual Validation
  - [ ] Verify 60 FPS with full flora in GameScene
  - [ ] Check visual variety and distribution

---

### Variant Cache System
**Spec/Documentation:** `/docs/technical/asset-system/README.md` (World Seed section)
**Dependencies:** Folder-Based Asset Migration
**Status:** ready

**Goal:** Pre-generate and cache procedural asset variants for fast loading.

**Tasks:**
- [ ] Binary Cache Format
  - [ ] Design cache file structure (vertices, indices, colors)
  - [ ] Implement cache writer in AssetRegistry
  - [ ] Implement cache reader for fast loading
- [ ] Cache Invalidation
  - [ ] Hash asset definition XML
  - [ ] Hash Lua script contents
  - [ ] Include world seed in cache key
  - [ ] Regenerate on hash mismatch
- [ ] World Seed Integration
  - [ ] Seed Lua RNG from world seed at load time
  - [ ] Ensure deterministic variant generation
  - [ ] Per-chunk variant selection by position hash
- [ ] Performance
  - [ ] Measure load time improvement (target: 10x faster)
  - [ ] Background cache generation during loading screen

---

### Mod Support
**Spec/Documentation:** `/docs/technical/asset-system/mod-metadata.md`, `/docs/technical/asset-system/patching-system.md`
**Dependencies:** Folder-Based Asset Migration, Variant Cache System
**Status:** planned (post-MVP)

**Goal:** Enable community modding with asset additions and modifications.

**Tasks:**
- [ ] Mod Metadata (Mod.xml)
  - [ ] Parse mod ID, name, version, author
  - [ ] Parse loadAfter/loadBefore dependencies
  - [ ] Build mod load order graph
  - [ ] Detect circular dependencies
- [ ] Mod Asset Loading
  - [ ] Scan `mods/*/assets/` folders
  - [ ] Merge mod assets with core assets
  - [ ] Support asset overrides (`override="true"`)
- [ ] Patching System
  - [ ] Implement XPath patch parser
  - [ ] Support operations: replace, add, remove, addOrReplace
  - [ ] Support conditional patches (requiresMod)
  - [ ] Apply patches in load order
- [ ] Debugging
  - [ ] Log patch applications
  - [ ] `/asset dump <defName>` command for inspecting final definitions
- [ ] Documentation
  - [ ] Modder's guide: adding new assets
  - [ ] Modder's guide: patching existing assets
  - [ ] Example mod structure

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
