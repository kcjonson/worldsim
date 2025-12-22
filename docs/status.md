# Project Status

Last Updated: 2025-12-22 (Simple Asset Support audit - confirmed complete)

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

### ✅ Basic Crafting System
**Spec/Documentation:** `/docs/development-log/plans/2025-12-19-basic-crafting-system.md`
**Dependencies:** MVP Complete
**Status:** complete

**Goal:** Enable basic crafting so players can place a Crafting Spot and queue orders for items like a Primitive Axe.

**Completed Tasks:**
- [x] Epic 1: Placement + Discovery (PR #75)
  - [x] Knowledge component for per-colonist discovery tracking
  - [x] RecipeDef.h data structures and RecipeRegistry singleton
  - [x] Recipe XML files (CraftingSpot innate, AxePrimitive)
  - [x] VisionSystem integration (colonists learn when they see things)
  - [x] PlacementMode state machine (B key, mouse click to place)
  - [x] BuildToolbar, BuildMenu, GhostRenderer components
  - [x] CraftingSpot asset definition
- [x] Epic 2: Crafting Execution (PR #78)
  - [x] WorkQueue component (per-station job queue)
  - [x] CraftingAdapter for station panel content
  - [x] EntityInfoPanel updates for station selection
  - [x] Craft task type and CraftingEffect action
  - [x] AIDecisionSystem Tier 6.5 (evaluate crafting work)
  - [x] Tier 6.6 gathering logic (gather missing materials before crafting)
  - [x] ActionSystem craft execution (consume inputs, produce outputs)
  - [x] Reed plant and PlantFiber assets for Primitive Axe recipe
  - [x] "Aha!" notifications when recipes unlock
  - [x] "Crafted" notifications when items complete
  - [x] Input validation using colonist Memory

**Result:** Colonists can discover recipes by seeing ingredients, player can queue crafting orders at stations, colonists gather missing materials and craft items autonomously. ✅

---

### ✅ Tabbed Colonist Info Panel
**Spec/Documentation:** `/docs/development-log/plans/2025-12-18-tabbed-colonist-info-panel.md`
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
**Spec/Documentation:** `/docs/development-log/plans/2025-12-18-shore-tiles-surface-edges.md`
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

### ✅ Simple Asset Support (SVG-Only)
**Spec/Documentation:** `/docs/technical/asset-system/asset-definitions.md`, `/docs/technical/asset-system/folder-based-assets.md`
**Dependencies:** None (Folder-Based Asset Migration was done as part of this)
**Status:** complete

**Goal:** Support hand-crafted SVG assets without Lua scripts via data-driven XML definitions.

**Completed Tasks:**
- [x] Folder-per-asset structure (`FolderName/FolderName.xml` pattern)
- [x] XML parsing with pugixml (defName, label, assetType, svgPath, worldHeight)
- [x] Rendering settings (complexity, tier)
- [x] Per-biome placement with spawn chance, distribution
- [x] Clumping parameters (clumpSize, clumpRadius, clumpSpacing)
- [x] Entity relationships (affinity, avoids, requires)
- [x] Groups with group index for lookups
- [x] Animation parameters (windResponse, swayFrequency)
- [x] Capabilities (edible, drinkable, harvestable, carryable, craftable, etc.)
- [x] Item properties (stackSize, nutrition, spoilable)
- [x] `@shared/` prefix resolution for shared scripts
- [x] Template caching (tessellated meshes cached)
- [x] DefName → ID interning for runtime performance

**Optional Future Enhancements:** Variation (colorRange, scaleRange), inheritance (parent attribute), LOD levels, shared SVG components (@components/).

**Result:** Assets like Reed, BerryBush, Berry, Stick, SmallStone, CraftingSpot all use this system. New simple assets can be added by creating a folder with XML + SVG. ✅

---

### ✅ Ground Texture System
**Spec/Documentation:** `/docs/technical/ground-textures.md`, `/docs/development-log/plans/2025-12-22-ground-texture-system.md`
**Dependencies:** None
**Status:** complete

**Goal:** Replace flat-color tiles with textured terrain using hybrid Tier 1 (rasterized texture) + Tier 3 (vector grass) approach, with hard/soft edge blending.

**Completed Tasks:**
- [x] Render-to-Texture Infrastructure
  - [x] Create RenderToTexture class (FBO wrapper with RAII)
  - [x] Unit tests for RenderToTexture
- [x] Tile Texture Atlas
  - [x] TileTextureAtlas class (atlas allocator)
  - [x] TileAtlasBuilder (builds atlas from SVG patterns)
  - [x] TilePatternBaker (rasterizes SVG to RGBA)
  - [x] Fallback checker pattern for missing SVGs
- [x] Tile Pattern Assets (6 of 10 surfaces)
  - [x] Grass, GrassShort, GrassTall, GrassMeadow (basic patterns)
  - [x] Dirt (rich: pebbles, cracks, dried grass)
  - [x] Water (rich: ripples, reflections, depth patches)
- [x] Surface Family System
  - [x] SurfaceFamily enum (Ground, Water, Rock)
  - [x] getSurfaceFamily() and getHardEdgeMaskByFamily()
  - [x] getSurfaceStackOrder() for blend direction
- [x] Shader Integration
  - [x] uber.frag tile render mode (kRenderModeTile = -3.0)
  - [x] tile.glsl with blend weight functions
  - [x] Early-out optimizations for interior tiles/pixels
- [x] ChunkRenderer Integration
  - [x] drawTile() with all 8 neighbor IDs + masks
  - [x] World tile coordinates for procedural variation
- [x] Soft Edge Blending
  - [x] computeHigherBleedWeights() - cardinal blending
  - [x] computeDiagonalCornerWeights() - diagonal corners
  - [x] computeTileEdgeDarkening() - procedural edge noise

**Optional Future Work:** Add missing patterns (Sand, Rock, Snow, Mud), enrich grass patterns, tune grass entity density.

**Result:** Tiles render with textured patterns, soft blending between same-family surfaces, hard edges at family boundaries (shorelines, cliffs). ✅

---

## In Progress Epics

(None currently - all epics complete or planned)

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

### Flora Content Pack
**Spec/Documentation:** `/docs/technical/asset-system/`
**Dependencies:** ~~Simple Asset Support~~ (complete)
**Status:** ready

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
