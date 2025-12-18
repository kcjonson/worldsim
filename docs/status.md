# Project Status

Last Updated: 2025-12-17 (BiomeGenerator system with grass variant blending)

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

## In Progress Epics

*No epics currently in progress.*

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

**Result:** Colonist survives indefinitely, discovers entities through wandering, fulfills needs autonomously ✅

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
