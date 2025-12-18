# Project Status

Last Updated: 2025-12-18 (Performance Optimization: VisionSystem)

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
- Completed epics get a development log entry in `/docs/development-log/entries/`
- An epic is only complete when ALL tasks are [x]

---

## Recently Completed Epics (Last 4)

### ✅ Tabbed Colonist Info Panel
**Spec/Documentation:** `/Users/kcjonson/.claude/plans/bubbly-sleeping-thompson.md`
**Dependencies:** MVP: Player Observation UI
**Status:** complete

**Goal:** Add tabbed interface to EntityInfoPanel for colonists (Status, Inventory tabs).

**Completed Tasks:**
- [x] TabBar Component (PR #70)
  - [x] TabBarStyle.h with 5-state styling (Normal, Hover, Active, Disabled, Focused)
  - [x] TabBar component with IFocusable integration
  - [x] Keyboard navigation (arrow keys, Enter)
  - [x] TabBarScene demo in ui-sandbox
- [x] EntityInfoPanel Integration (PR #71)
  - [x] Add TabBar to colonist panels
  - [x] Status tab with existing content (needs, task, action)
  - [x] Inventory tab with slot count and item list
  - [x] Fixed panel height to prevent layout jumping
  - [x] Proper click event handling (tabs don't close panel)
- [x] SelectionAdapter Updates
  - [x] Split adaptColonist into adaptColonistStatus/adaptColonistInventory
  - [x] Inventory content shows slot usage and item names

**Result:** Colonist info panel has tabbed interface for organized display of growing colonist state ✅

---

### ✅ Shore Tiles + Surface Edge Rendering
**Spec/Documentation:** `/Users/kcjonson/.claude/plans/tranquil-discovering-globe.md`
**Dependencies:** Flat Tile Storage Refactor
**Status:** complete

**Goal:** Pre-compute tile adjacency for fast shore detection, add visual edge decorations at surface transitions, and generate organic mud around water bodies.

**Completed Tasks:**
- [x] TileAdjacency System
  - [x] Create TileAdjacency.h with 64-bit packed neighbor data (8 directions × 6 bits)
  - [x] Direction enum (NW, W, SW, S, SE, E, NE, N) with getNeighbor/setNeighbor helpers
  - [x] Surface stacking order (Water < Mud < Sand < Dirt < Soil < Rock < Snow)
  - [x] getEdgeMaskByStack() and getCornerMaskByStack() for edge rendering
- [x] TilePostProcessor
  - [x] Flood-fill mud generation around water bodies (contiguous rings, 3-tile max)
  - [x] Adjacency computation for all tiles after initial surface assignment
  - [x] Tunable parameters (kMudMaxDistance=3, kMudProbability=0.95)
- [x] Surface Edge Rendering
  - [x] Higher-stacked surfaces draw darkened edges when adjacent to lower surfaces
  - [x] Asymmetric stroke widths for depth effect (1px light N/W, 2px shadow S/E)
  - [x] Corner rectangles sized to match adjacent edges (NW=1×1, SE=2×2, etc.)
- [x] Surface::Mud Type
  - [x] Add Mud to Surface enum with darker brown color
  - [x] Reduced dirt threshold (0.88) for less random dirt in grassland

**Result:** Tiles have pre-computed adjacency for O(1) shore detection. Visual depth effect on all surface transitions. Organic mud rings around water bodies. ✅

---

### ✅ Flat Tile Storage Refactor
**Spec/Documentation:** `/Users/kcjonson/.claude/plans/fluffy-cooking-russell.md`
**Dependencies:** None
**Status:** complete

**Goal:** Replace layered/lazy tile generation with flat tile array per chunk. Fixed flora-on-water bug and eliminated class of bugs where systems disagree about tile state.

**Completed Tasks:**
- [x] Phase 0: Debug Code Removal & Terminology Rename
  - [x] Removed ~100 lines of debug logging across 4 files
  - [x] Renamed `GroundCover` enum → `Surface`
  - [x] Renamed `Grass` → `Soil` (plants grow on soil, grass IS a plant)
  - [x] Renamed `selectGroundCover()` → `selectSurface()`
- [x] Phase 1: Data Structure Changes
  - [x] Updated TileData struct (8 bytes: surface, primaryBiome, secondaryBiome, biomeBlend, elevation, moisture, flags)
  - [x] Added `tiles` array to Chunk struct (262,144 TileData = ~2.1 MB/chunk)
  - [x] Added `std::atomic<bool> generationComplete` for thread safety
  - [x] Added `Chunk::generate()` that pre-computes all tiles
  - [x] Added `BiomeWeights::secondary()` and `primaryWeight()` methods
- [x] Phase 2: System Updates
  - [x] Updated ChunkRenderer: check `isReady()`, read from tiles[] array
  - [x] Removed pure chunk optimization (was hiding ponds!)
  - [x] Updated VisionSystem to check `isReady()` before querying terrain
- [x] Phase 3: Cleanup
  - [x] Removed `Chunk::isPure()` method
  - [x] Removed `ChunkSampleResult::isPure` and `singleBiome` fields
  - [x] Removed `MockWorldSampler::isChunkPure()` method
  - [x] Updated unit tests

**Result:** Single source of truth for tile data. Ponds now visible. Flora-on-water bug architectural fix in place. ✅

**Additional Fix (2025-12-12):** Discovered and fixed Retina coordinate mismatch:
- `getViewport()` returned physical pixels (2688×1680), not logical pixels (1344×840)
- VisionSystem was detecting water at wrong world positions
- Tiles were rendering at 2× offset positions
- **Fix:** Implemented `getLogicalViewport()` and updated all render/input code to use it
- Colonists now correctly find and drink from actual water tiles

---

### ✅ Enhanced Performance Metrics
**Spec/Documentation:** `.claude/plans/performance-metrics-epic.md`
**Dependencies:** None
**Status:** complete

**Goal:** Add comprehensive performance metrics to developer-client to diagnose performance issues.

**Tasks:**
- [x] Story 1: Display Existing Timing Breakdown (Quick Win)
  - [x] Update MetricsData interface to include tileRenderMs, entityRenderMs, updateMs
  - [x] Add FrameBudgetBar hero component with timing breakdown
  - [x] Display tileCount and entityCount in StatsRow
- [x] Story 5: Visible Chunk Count
  - [x] Add visibleChunkCount to PerformanceMetrics
  - [x] Track in ChunkRenderer::m_lastChunkCount
  - [x] Display in developer-client
- [x] Story 4: Frame Histogram & Spike Detection
  - [x] Add histogram buckets to PerformanceMetrics (0-8ms, 8-16ms, 16-33ms, 33ms+)
  - [x] Add spike counters and 1% low FPS
  - [x] Add FrameHistogram visualization to developer-client
- [x] Story 2: Per-ECS-System Timing
  - [x] Add name() pure virtual to ISystem, implemented in all 7 systems
  - [x] Instrument World::update() to time each system
  - [x] Add EcsSystemTiming to PerformanceMetrics
  - [x] Add EcsSystemsBar component to developer-client
- [x] Story 3: GPU Timing
  - [x] Create GPUTimer class with GL_TIME_ELAPSED queries
  - [x] Add gpuRenderMs to PerformanceMetrics
  - [x] Display GPU time in developer-client (Note: May show 0 on macOS due to deprecated OpenGL)

**Result:** Developer-client now provides professional-grade performance profiling:
- Frame budget visualization with component breakdown
- Per-ECS-system timing (VisionSystem identified as dominant at ~1.5ms)
- Frame time histogram and spike detection
- Compact stat cards and sparkline trends ✅

---

## In Progress Epics

### Basic Crafting System
**Spec/Documentation:** `/Users/kcjonson/.claude/plans/melodic-chasing-ember.md`
**Dependencies:** MVP Complete
**Status:** in progress (Epic 1 complete, Epic 2 planned)

**Goal:** Enable basic crafting so players can place a Crafting Spot and queue orders for items like a Primitive Axe.

**Epic 1: Placement + Discovery** ✅ COMPLETE
- [x] Phase 1: Foundation
  - [x] Create Knowledge component (per-colonist permanent discovery tracking)
  - [x] Create RecipeDef.h data structures (inputs, outputs, station, workAmount)
  - [x] Create RecipeRegistry singleton (loads from XML, query by station/knowledge)
  - [x] Create recipe XML files (CraftingSpot.xml innate, AxePrimitive.xml)
- [x] Phase 2: Discovery Integration
  - [x] Integrate Knowledge into VisionSystem (colonists learn when they see things)
  - [x] Add Knowledge component to colonist initialization
- [x] Phase 3: Placement Mode
  - [x] Create PlacementMode state machine (None → MenuOpen → Placing → None)
  - [x] Create BuildToolbar component (Build button in overlay)
  - [x] Create BuildMenu popup (shows innate recipes)
  - [x] Create GhostRenderer (semi-transparent placement preview)
  - [x] Integrate placement into GameScene (B key, mouse click to place)
  - [x] Create CraftingSpot asset definition (SVG + XML)

**Epic 2: Crafting Execution** (Planned)
- [ ] Phase 4: Work Queue + Crafting Menu
  - [ ] WorkQueue component (per-station job queue)
  - [ ] CraftingStationSelection variant
  - [ ] CraftingAdapter for station panel content
  - [ ] EntityInfoPanel updates for station selection
- [ ] Phase 5: AI + Action Integration
  - [ ] Add Craft task type to Task.h
  - [ ] Add Craft action + CraftingEffect to Action.h
  - [ ] AIDecisionSystem Tier 6.5 (evaluate crafting work)
  - [ ] ActionSystem handles craft execution (consume inputs, produce outputs)
- [ ] Phase 6: Polish
  - [ ] "Aha" notifications when recipes unlock
  - [ ] Input validation before starting craft
  - [ ] Progress bar for active crafting

---

### Performance Optimization
**Spec/Documentation:** `.claude/plans/performance-optimization-epic.md`
**Dependencies:** Enhanced Performance Metrics (complete)
**Status:** in progress

**Goal:** Improve game performance to eliminate lag and stuttering.

**Confirmed bottlenecks (from metrics):**
- **VisionSystem**: 1.5ms/frame (92% of update time) - scans chunks for entities
- **Tile Rendering**: 3-5ms/frame - iterates all visible tiles each frame
- **Entity Rendering**: 3-4ms/frame - renders 2000+ entities per frame

**Tasks:**
- [x] VisionSystem optimization
  - [x] Shore tile caching (pre-compute during chunk generation)
  - [x] Throttle to every 5 frames (12x/sec instead of 60x/sec)
- [x] Tile render data caching
  - [x] Pre-compute adjacency masks and neighbors during chunk generation
  - [x] ChunkRenderer uses cached data instead of extracting per-frame
- [ ] Entity culling/LOD (skip offscreen entities)
- [ ] Dynamic LOD for zoom levels

---

## Completed MVP (Consolidated)

The following MVP epics have all been completed. Detailed task breakdowns are preserved here for reference:

### ✅ MVP Complete - All Core Systems Implemented
**Status:** complete (Dec 2025)

**Summary:** Full MVP implementation with autonomous colonist survival loop.

**Completed MVP Epics:**
- **Colonist Entity + ECS Foundation** - Custom ECS with sparse set pattern, colonist spawning
- **Needs System** - 4 needs (Hunger, Thirst, Energy, Bladder) with decay and thresholds
- **World Entities** - Capability system, Berry Bush, Water Tiles, Bio Pile
- **Memory System** - Vision-based entity discovery, no omniscient pathfinding
- **AI Decision System** - Priority-based decision hierarchy (Tiers 3, 5, 7)
- **Movement & Pathfinding** - Direct movement to targets with arrival detection
- **Actions System** - Eat, Drink, Sleep, Toilet actions with side effects
- **Player Observation UI** - Selection, need bars, task queue display

**Key Technical Components:**
- ECS: EntityID with generations, ComponentPool, Registry, View, ISystem, World
- Systems: VisionSystem, NeedsDecaySystem, AIDecisionSystem, MovementSystem, PhysicsSystem, ActionSystem, DynamicEntityRenderSystem
- Components: Position, Rotation, Velocity, MovementTarget, Appearance, Colonist, NeedsComponent, Memory, Task, Action, DecisionTrace

**Result:** Colonist survives indefinitely, discovers entities through wandering, fulfills needs autonomously, and proactively gathers food when inventory is empty ✅

---

## Deferred Epics

### ⏸️ Animated Vector Graphics Performance Optimization
**Spec/Documentation:** `/docs/technical/vector-graphics/animation-performance.md`
**Dependencies:** Vector Graphics Validation (completed)
**Status:** deferred

**Reason:** Setting aside performance work to focus on MVP game systems. GPU instancing (Phase 2) is complete and provides sufficient performance (34k+ entities at 60 FPS).

**Remaining Work (deferred):**
- Phase 1: CPU Optimization Stack (arena allocators, temporal coherence, SIMD)
- Phase 2.2: Vertex Shader Animation (wind displacement)
- Phase 3: Tiered System Integration

---

## Planned Epics (Post-MVP)

### Simple Asset Support (SVG-Only)
**Spec/Documentation:** `/docs/technical/asset-system/asset-definitions.md`
**Dependencies:** Folder-Based Asset Migration
**Status:** planned

**Goal:** Support hand-crafted SVG assets without Lua scripts (flowers, mushrooms, rocks).

---

### Flora Content Pack
**Spec/Documentation:** `/docs/technical/asset-system/`
**Dependencies:** Simple Asset Support
**Status:** planned

**Goal:** Add visual variety with flowers, mushrooms, rocks, and bushes.

---

### Variant Cache System
**Spec/Documentation:** `/docs/technical/asset-system/README.md` (World Seed section)
**Dependencies:** Folder-Based Asset Migration
**Status:** planned

**Goal:** Pre-generate and cache procedural asset variants for fast loading.

---

### Observability System - Full Feature Set
**Spec/Documentation:** `/docs/technical/observability/INDEX.md`
**Dependencies:** None
**Status:** planned

---

### GPU-Based SDF Rendering for UI Primitives
**Spec/Documentation:** `/docs/technical/ui-framework/sdf-rendering.md`
**Dependencies:** None
**Status:** planned

---

### UI Event System
**Spec/Documentation:** `/docs/technical/ui-framework/event-system.md`
**Dependencies:** None
**Status:** planned

**Goal:** Implement event propagation with consumption (like HTML DOM) and debug introspection via HTTP.

**Tasks:**
- [ ] Event Infrastructure
  - [ ] Create InputEvent struct with type, position, consumed flag
  - [ ] Add handleEvent() to IComponent interface
  - [ ] Create HitTestResult and HitTestEntry types
- [ ] Component Updates
  - [ ] Update Button to use handleEvent() and consume clicks
  - [ ] Update TextInput similarly
  - [ ] Add bounds tracking for hit testing
- [ ] Dispatch System
  - [ ] Create z-index sorted event dispatch
  - [ ] Wire up to InputManager in main loop
  - [ ] Short-circuit on consumed events
- [ ] Debug Integration
  - [ ] Add /api/ui/hit-test endpoint to Developer Server
  - [ ] Add click events with layer stack to SSE stream
  - [ ] Update Developer Client to display layer stack
- [ ] QUICKFIX Cleanup
  - [ ] Remove isPointOverUI() from GameUI, GameOverlay
  - [ ] Remove isPointOver() from ZoomControl
  - [ ] Migrate manual handleInput() to handleEvent()

---

### Mod Support
**Spec/Documentation:** `/docs/technical/asset-system/mod-metadata.md`, `/docs/technical/asset-system/patching-system.md`
**Dependencies:** Folder-Based Asset Migration, Variant Cache System
**Status:** planned (post-MVP)

---

### Ground Texture System (Rimworld-Style)
**Spec/Documentation:** `/docs/technical/ground-textures.md`
**Dependencies:** None
**Status:** ready

**Goal:** Replace solid-color tiles with rich, earth-like terrain using hybrid Tier 1 (rasterized texture) + Tier 3 (vector grass) approach, with proper hard/soft edge rendering.

**Key Design Decisions:**
- **Three Surface Families**: Ground (traversable), Water (impassable), Rock (impassable cliffs)
- **Hard edges between families**: Shorelines, cliff edges render crisp (vector precision)
- **Soft edges within families**: Natural ground transitions blend smoothly
- **Snow as overlay**: Seasonal attribute, not a surface type
- **Built/flooring**: Handled as entities, not tiles

**Tasks:**
- [ ] Render-to-Texture Infrastructure
  - [ ] Create RenderToTexture class (FBO wrapper)
  - [ ] Test rendering SVG asset to texture
- [ ] Tile Pattern Assets
  - [ ] Create Soil pattern SVG (prototype with stubble + speckles)
  - [ ] Define asset XML format for tile patterns
- [ ] Atlas Builder
  - [ ] Create TileTextureAtlas class
  - [ ] Rasterize patterns to atlas slots
- [ ] Shader Integration
  - [ ] Add tile texture render mode to uber shader
  - [ ] Update TileVertex struct with surface type
  - [ ] Update ChunkRenderer to pass world position + surface
- [ ] Surface Family + Hard Edge Mask
  - [ ] Implement SurfaceFamily enum and getFamily() lookup
  - [ ] Extend TileAdjacency with hardEdgeMask (8 bits, pre-computed)
  - [ ] Add hardEdgeMask to TileVertex struct
- [ ] Hard Edge Rendering
  - [ ] Pass hardEdgeMask to shader as vertex attribute
  - [ ] Implement edge shadow rendering (read mask, darken near edge)
- [ ] All Surface Types (16 total)
  - [ ] Ground family patterns (10 surfaces)
  - [ ] Water family patterns (3 surfaces)
  - [ ] Rock family patterns (3 surfaces)
- [ ] Grass Entity Tuning
  - [ ] Reduce density, increase blade size
  - [ ] Visual balance between texture and vector layers
- [ ] Soft Edge Blending (optional enhancement)
  - [ ] Pass neighbor surface info to shader
  - [ ] Alpha gradient blending for same-family transitions

---

## Blockers & Issues

### ✅ RESOLVED: SVG Ellipse/Circle Tessellation Bug
**Impact:** Berry Bush and other assets using `<ellipse>` or `<circle>` SVG elements fail to render
**Resolution:** Fixed in commit `e246fc0` (Dec 7, 2025) by adding convex polygon detection + fan tessellation
- Added `isConvexPolygon()` to detect circles/ellipses (inherently convex)
- Convex polygons use O(n) fan tessellation, bypassing the problematic ear-clipping algorithm
- Unit tests added in `Tessellator.test.cpp` to prevent regression
- Berry Bush SVG now renders correctly with native `<circle>` elements

### ✅ RESOLVED: Entities Spawning on Water Tiles
**Impact:** Flora entities (grass, berry bushes, trees) spawn on water tiles instead of being restricted to land
**Resolution:** Fixed by **Flat Tile Storage Refactor** + **Retina Coordinate Fix**
- Flat tile storage gives single source of truth for surface type
- Retina coordinate fix ensures correct world↔screen mapping
- Colonists now correctly find and drink from actual water tiles
- No visual grass-on-water issue observed

---

## MVP Success Criteria

From `/docs/design/mvp-scope.md`:

**Test Scenario:**
1. One colonist spawns at map center
2. Several berry bushes scattered nearby
3. Water tiles (ponds) within walking distance (tile-based, not entity)
4. Open ground for sleeping/bathroom

**Expected Behavior (leave running):**
1. Colonist wanders initially
2. Discovers berry bush and pond through sight
3. When hungry → walks to known berry bush → eats
4. When thirsty → walks to known pond → drinks
5. Drinking increases bladder need
6. When bladder urgent → finds outdoor spot → creates Bio Pile
7. When tired → sleeps on ground
8. Repeat forever

**Success:**
- Colonist survives indefinitely
- Task queue shows sensible decisions
- No player intervention required
- Colonist discovers new entities through wandering
