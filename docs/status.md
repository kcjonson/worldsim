# Project Status

Last Updated: 2025-12-11 (Added Flat Tile Storage Refactor epic)

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

### ✅ Entity Placement System
**Spec/Documentation:** `/docs/technical/entity-placement-system.md`
**Dependencies:** Asset System Architecture (Phase 1 complete)
**Status:** complete

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

**Result:** Complete entity placement system with dependency ordering, spatial awareness, and biome filtering ✅

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

### ✅ MVP: Colonist Entity + ECS Foundation
**Spec/Documentation:** `/Users/kcjonson/.claude/plans/curried-hopping-eagle.md`
**Dependencies:** Asset System Architecture
**Status:** complete

**Goal:** Build ECS infrastructure and implement first colonist entity.

**Completed Tasks:**
- [x] ECS Core Infrastructure
  - [x] EntityID with generation counter for safe handle reuse
  - [x] ComponentPool with sparse set pattern for O(1) operations
  - [x] Registry for entity lifecycle management
  - [x] View for component queries
  - [x] ISystem base interface with priority-based scheduling
  - [x] World top-level ECS container
- [x] Core Components
  - [x] Position, Rotation (Transform)
  - [x] Velocity, MovementTarget (Movement)
  - [x] Appearance (Rendering)
  - [x] Colonist (Identity tag)
- [x] Core Systems
  - [x] MovementSystem (priority 100)
  - [x] PhysicsSystem (priority 200)
  - [x] DynamicEntityRenderSystem (priority 900)
- [x] Colonist Visual Asset
  - [x] Create simple colonist SVG (polygon-based, ~1m diameter)
  - [x] Add colonist asset definition XML
  - [x] Test rendering in GameScene
- [x] GameScene Integration
  - [x] Create ECS World in onEnter()
  - [x] Register all systems
  - [x] Spawn colonist at map center (0,0)
  - [x] Call ecsWorld->update(dt) in game loop
  - [x] Pass dynamic entities to EntityRenderer

**Result:** Colonist "Bob" spawns at map center and renders via GPU instancing ✅

---

### ✅ MVP: Needs System
**Spec/Documentation:** `/docs/design/game-systems/colonists/needs.md`
**Dependencies:** Colonist Entity
**Status:** complete

**Goal:** Implement the four MVP needs (Hunger, Thirst, Energy, Bladder) with decay and threshold triggers.

**Completed Tasks:**
- [x] Need Data Structure
  - [x] Create Need struct (value 0-100%, decayRate, seekThreshold, criticalThreshold)
  - [x] Create NeedsComponent with 4 needs (Hunger, Thirst, Energy, Bladder)
  - [x] Configure thresholds per design doc (~50% seek, ~10% critical)
  - [x] Helper methods: needsAttention(), isCritical(), mostUrgentNeed()
- [x] Need Decay System
  - [x] Create NeedsDecaySystem (priority 50, runs before movement)
  - [x] Implement per-frame decay for all needs
  - [x] Different decay rates per need type (Thirst fastest at 1.2, Bladder slowest at 0.3)
  - [x] Game-time scaling (gameMinutesPerSecond)
- [x] Integration
  - [x] Add NeedsComponent to colonist spawn
  - [x] Register NeedsDecaySystem in GameScene

**Result:** Colonists have 4 decaying needs with configurable thresholds ✅

**Note:** Bladder acceleration from drinking and threshold event handling will be implemented as part of the Actions System and AI Decision System epics.

---

### ✅ MVP: World Entities
**Spec/Documentation:** `/docs/design/mvp-entities.md`, `/docs/design/game-systems/world/entity-capabilities.md`
**Dependencies:** Asset System Architecture
**Status:** complete

**Goal:** Create the MVP world entities (Berry Bush, Pond) with capabilities for need fulfillment.

**Completed Tasks:**
- [x] Capability System
  - [x] Define capability types (Edible, Drinkable, Sleepable, Toilet)
  - [x] Add capability data to entity definitions (AssetDefinition.h)
  - [x] Parse capabilities from asset XML (AssetRegistry.cpp)
- [x] Berry Bush Asset
  - [x] Create Berry Bush SVG asset
  - [x] Add Edible capability (nutrition: 0.3, quality: normal, spoilable: true)
  - [x] Configure spawning in Grassland/Forest biomes with clumping
- [x] Water Tiles (Ponds)
  - [x] Implement as tile-based (GroundCover::Water), not entity
  - [x] Add water generation to Chunk::selectGroundCover() for Grassland/Forest
  - [x] Use fractal noise for organic pond-like clusters (~3-8 tiles across)
- [x] Bio Pile Entity
  - [x] Create Bio Pile SVG (simple waste marker)
  - [x] Entity spawns when colonist uses ground as toilet
  - [x] No capabilities (just exists as marker)

**Deferred to Actions System:**
- Ground Capabilities (Sleepable/Toilet on tiles) - handled by game logic, not spawned assets

**Result:** Capability system with 4 capability types, 2 MVP world entities (Berry Bush, Bio Pile) and Water Tiles with full placement rules ✅

---

### ✅ MVP: Memory System
**Spec/Documentation:** `/docs/design/game-systems/colonists/memory.md`
**Dependencies:** World Entities, Colonist Entity
**Status:** complete

**Goal:** Colonists only know about entities they have seen. No omniscient pathfinding.

**Completed Tasks:**
- [x] Memory Data Structure
  - [x] Create MemoryComponent (set of known entity IDs with last-known positions)
  - [x] Track known world entities per colonist (position quantization for deduplication)
  - [x] Configurable sight radius (default 10 meters)
- [x] Vision System
  - [x] Implement VisionSystem (priority 45, runs before NeedsDecay)
  - [x] Circular sight radius observation
  - [x] Query multiple chunks based on position ± sightRadius
  - [x] Add entities within sight to Memory component
- [x] Memory Queries
  - [x] findKnownWithCapability() - filter by capability type
  - [x] findNearestWithCapability() - find closest entity with capability
  - [x] countKnownWithCapability() - count entities with capability
- [x] GameScene Integration
  - [x] Register VisionSystem with PlacementExecutor and processedChunks
  - [x] Add Memory component to colonist spawn

**Result:** Colonists can only interact with entities they have observed within their sight radius ✅

---

### ✅ MVP: AI Decision System
**Spec/Documentation:** `/docs/design/game-systems/colonists/ai-behavior.md`
**Dependencies:** Needs System, Memory System
**Status:** complete

**Goal:** Implement the decision hierarchy for autonomous colonist behavior (Tiers 3, 5, 7 for MVP).

**Completed Tasks:**
- [x] Task Component
  - [x] Task struct with type, state, targetPosition, needToFulfill, reason
  - [x] TaskType enum (None, FulfillNeed, Wander)
  - [x] TaskState enum (Pending, Moving, Arrived)
- [x] Decision Hierarchy
  - [x] Tier 3: Critical Needs (need < 10%) - immediate fulfillment
  - [x] Tier 5: Actionable Needs (below seek threshold) - seek fulfillment
  - [x] Tier 7: Wander - random movement when all needs satisfied
  - [x] Ground fallback for Energy/Bladder (sleep/toilet on ground)
- [x] Task Selection Algorithm
  - [x] Evaluate tiers in priority order
  - [x] Query memory for entities with required capabilities
  - [x] Re-evaluate every 0.5s or when critical needs emerge
- [x] GameScene Integration
  - [x] AIDecisionSystem registered (priority 60)
  - [x] Task component added to colonist spawn
  - [x] MovementTarget wired up by AI system

**Deferred to Post-MVP:**
- Tier 6: Work - Harvest Wild (foraging) when needs satisfied

**Result:** Colonists autonomously navigate to entities based on need priorities ✅

---

### ✅ MVP: Movement & Pathfinding
**Spec/Documentation:** `/docs/design/game-systems/colonists/ai-behavior.md`
**Dependencies:** Colonist Entity, AI Decision System
**Status:** complete

**Goal:** Colonist can move to target locations (entities, tiles).

**Completed Tasks:**
- [x] Basic Pathfinding
  - [x] Simple direct movement toward target (MVP: no obstacle avoidance)
  - [x] Arrive at destination detection (distance check in MovementSystem)
  - [x] Movement state tracking via TaskState (Pending → Moving → Arrived)
- [x] Task Movement Integration
  - [x] AIDecisionSystem sets MovementTarget when task assigned
  - [x] MovementSystem updates TaskState to Arrived when reached

**Result:** Colonists move to target positions and report arrival ✅

---

### ✅ MVP: Actions System
**Spec/Documentation:** `/docs/design/game-systems/colonists/needs.md`, `/docs/design/mvp-entities.md`
**Dependencies:** Movement & Pathfinding ✅, World Entities ✅, Needs System ✅
**Status:** complete

**Goal:** Colonist performs actions that fulfill needs (Eat, Drink, Sleep, Toilet).

**Completed Tasks:**
- [x] Action Framework
  - [x] Action component with ActionType enum (Eat, Drink, Sleep, Toilet)
  - [x] Action state machine (Starting, InProgress, Complete)
  - [x] Action duration/progress tracking with elapsed time
  - [x] std::variant-based polymorphic effect system (NeedEffect, ProductionEffect)
  - [x] Action interruption policy documentation
- [x] Need Fulfillment Actions (all 4 MVP actions)
  - [x] Eat: Restores Hunger from Edible entities
  - [x] Drink: Restores Thirst from Drinkable, increases Bladder (side effect)
  - [x] Sleep: Restores Energy over time (ground for MVP)
  - [x] Toilet: Relieves Bladder
- [x] ActionSystem Integration
  - [x] ActionSystem (priority 70) processes actions and applies effects
  - [x] Updates NeedsComponent based on NeedEffect data
  - [x] Registered in GameScene with proper priority ordering
- [x] Unit Tests
  - [x] Comprehensive test suite for ActionSystem

**Result:** Colonists can now perform actions to fulfill their needs, completing the need-fulfillment loop ✅

---

### ✅ MVP: Player Observation UI
**Spec/Documentation:** `/docs/design/mvp-scope.md`
**Dependencies:** Needs System ✅, AI Decision System ✅, Actions System ✅
**Status:** complete

**Goal:** Player can observe colonist status and task queue (observation only, no control).

**Completed Tasks:**
- [x] UI Framework Visibility System
  - [x] Add `visible` flag to IComponent interface
  - [x] Component::render() skips invisible children
  - [x] Component::handleInput() skips invisible children
  - [x] Component::update() skips invisible children
- [x] EntityInfoPanel Performance Optimization
  - [x] CachedSelection for detecting structure vs value changes
  - [x] Three-tier update system (visibility/structure/value)
  - [x] Replace position-offscreen hiding with visibility flags
  - [x] Value-only updates for progress bar changes
- [x] Colonist Selection
  - [x] Click on colonist to select (GameScene::handleEntitySelection)
  - [x] Visual indicator for selected colonist (gold circle via drawCircle)
- [x] Colonist Info Panel
  - [x] Display colonist name (EntityInfoPanel with SelectionAdapter)
  - [x] Show all 4 need bars (Hunger, Thirst, Energy, Bladder)
  - [x] Color coding for need urgency (green → yellow → red)
- [x] Task Queue Display
  - [x] Show current task with progress (Task/Action in panel)
  - [x] DecisionTrace component and AIDecisionSystem integration (backend complete)
  - [x] TaskListPanel: Expanded view showing full task queue
  - [x] ClickableTextSlot for toggle affordance in EntityInfoPanel

**Result:** Player can select colonists, view their need bars, and expand a full task queue showing DecisionTrace priorities ✅

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

## Planned Epics (Ready)

### Flat Tile Storage Refactor
**Spec/Documentation:** `/docs/technical/flat-tile-storage-refactor.md`
**Dependencies:** None
**Status:** ready

**Goal:** Replace layered/lazy tile generation with flat tile array per chunk. Fixes flora-on-water bug and eliminates class of bugs where systems disagree about tile state.

**Key Insight:** `GroundCover` is misnamed — it's actually `Surface` (terrain material). Real ground cover (plants) are entities placed ON surfaces. Surface must be computed FIRST, then flora placement can reliably skip water tiles.

**Tasks:**
- [ ] Phase 0: Terminology Fix (Prerequisite)
  - [ ] Rename `GroundCover` enum → `Surface`
  - [ ] Rename `Grass` → `Soil` (plants grow on soil, grass IS a plant)
  - [ ] Rename `selectGroundCover()` → `selectSurface()`
  - [ ] Update all references and documentation
- [ ] Phase 1: Data Structure Changes
  - [ ] Update TileData struct (8 bytes: surface, primaryBiome, secondaryBiome, biomeBlend, elevation, moisture, flags)
  - [ ] Add `tiles` array to Chunk struct (262,144 TileData = ~2.1 MB/chunk)
  - [ ] Add `std::atomic<bool> generationComplete` for thread safety
  - [ ] Change `Chunk::generate(worldSeed, sampleResult)` — sampleResult is temporary, discarded after
  - [ ] Add `BiomeWeights::secondary()` method for ecotone blend storage
- [ ] Phase 2: System Updates
  - [ ] Update ChunkRenderer: check `isReady()`, read from tiles[] array
  - [ ] Remove pure chunk optimization (no longer needed)
  - [ ] Update PlacementExecutor to query tiles[] for surface (**fixes flora-on-water bug**)
  - [ ] Update VisionSystem to query tiles[] for terrain
  - [ ] Update AIDecisionSystem to use definitive tile data
- [ ] Phase 3: Cleanup & Testing
  - [ ] Remove selectSurface() from query path (keep in generate())
  - [ ] Simplify ChunkSampleResult (remove isPure flag, sector grid is now temporary)
  - [ ] Add determinism tests (same seed = identical tiles)
  - [ ] Add thread safety tests
  - [ ] Add test: no flora entities placed on Surface::Water
  - [ ] Add test: adjacent chunk edges match (seamless ecotones)

**Success Criteria:**
- All systems read tile data from single source (tiles[] array)
- No flora spawning on water (flora-on-water bug fixed)
- Deterministic generation from world seed
- Thread-safe chunk streaming (isReady() pattern)
- Biome blending preserved (primaryBiome, secondaryBiome, biomeBlend)
- Memory usage < 250 MB for 25-chunk load (~52 MB expected)
- All existing tests pass

---

## Blockers & Issues

### SVG Ellipse/Circle Tessellation Bug
**Impact:** Berry Bush and other assets using `<ellipse>` or `<circle>` SVG elements fail to render
**Workaround:** Convert ellipse/circle elements to `<path>` bezier approximations (done for Berry Bush)
**Root Cause:** Ear-clipping tessellator receives degenerate polygons when nanosvg converts circles/ellipses to paths
**Fix Needed:** Either improve tessellator robustness or convert shapes to paths in SVGLoader before tessellation

### Entities Spawning on Water Tiles
**Impact:** Flora entities (grass, berry bushes, trees) spawn on water tiles instead of being restricted to land
**Workaround:** None currently - visual issue only, doesn't affect gameplay
**Root Cause:** Surface type (water vs soil) is computed lazily via noise, not stored. PlacementExecutor can't reliably check surface because it would need to recompute the same noise with exact same parameters.
**Fix:** The **Flat Tile Storage Refactor** epic solves this architecturally — surface is computed FIRST and stored in `tiles[]` array, then PlacementExecutor queries definitive surface data.
**Attempted (failed):** Added `groundCovers` field to BiomePlacement and checks in PlacementExecutor, but filtering didn't work because of the lazy computation mismatch.

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
