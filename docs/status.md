# Project Status

Last Updated: 2025-12-07 (MVP: World Entities - Capability System + Assets Complete)

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

**Result:** Capability system with 4 capability types, 3 MVP world entities (Berry Bush, Pond, Bio Pile) with full placement rules ✅

---

### 🔄 MVP: Memory System
**Spec/Documentation:** `/docs/design/game-systems/colonists/memory.md`
**Dependencies:** World Entities, Colonist Entity
**Status:** ready

**Goal:** Colonists only know about entities they have seen. No omniscient pathfinding.

**Tasks:**
- [ ] Memory Data Structure
  - [ ] Create MemoryComponent (set of known entity IDs)
  - [ ] Track known entities per colonist
- [ ] Vision System
  - [ ] Implement circular sight radius around colonist
  - [ ] Continuous observation during movement
  - [ ] Add entities within sight to memory
- [ ] Memory Queries
  - [ ] Query known entities by capability type
  - [ ] Find nearest known entity with capability
  - [ ] Filter task targets to only known entities

---

### 🔄 MVP: AI Decision System
**Spec/Documentation:** `/docs/design/game-systems/colonists/ai-behavior.md`
**Dependencies:** Needs System, Memory System
**Status:** ready

**Goal:** Implement the decision hierarchy for autonomous colonist behavior (Tiers 3, 5, 6, 7 for MVP).

**Tasks:**
- [ ] Task Queue
  - [ ] Create TaskQueue component per colonist
  - [ ] Task struct (type, target, progress, reason)
  - [ ] Display current/pending tasks
- [ ] Decision Hierarchy
  - [ ] Tier 3: Critical Needs (need < 10%) - immediate fulfillment
  - [ ] Tier 5: Actionable Needs (need < 50%) - seek fulfillment
  - [ ] Tier 6: Work - Harvest Wild (foraging) when needs satisfied
  - [ ] Tier 7: Wander - random movement when nothing else to do
- [ ] Task Selection Algorithm
  - [ ] Evaluate tiers in priority order
  - [ ] Select best task based on memory and needs
  - [ ] Re-evaluate periodically or on need threshold change

---

### 🔄 MVP: Movement & Pathfinding
**Spec/Documentation:** `/docs/design/game-systems/colonists/ai-behavior.md`
**Dependencies:** Colonist Entity
**Status:** ready

**Goal:** Colonist can move to target locations (entities, tiles).

**Tasks:**
- [ ] Basic Pathfinding
  - [ ] Simple direct movement toward target (MVP: no obstacle avoidance)
  - [ ] Arrive at destination detection
  - [ ] Movement state (idle, moving, arrived)
- [ ] Task Movement Integration
  - [ ] Move to target entity when task assigned
  - [ ] Report arrival to action system
  - [ ] Handle unreachable targets gracefully

---

### 🔄 MVP: Actions System
**Spec/Documentation:** `/docs/design/game-systems/colonists/needs.md`, `/docs/design/mvp-entities.md`
**Dependencies:** Movement & Pathfinding, World Entities, Needs System
**Status:** ready

**Goal:** Colonist performs actions that fulfill needs (Eat, Drink, Sleep, Toilet).

**Tasks:**
- [ ] Action Framework
  - [ ] Action state machine (starting, in_progress, complete)
  - [ ] Action duration/progress tracking
  - [ ] Action completion callbacks
- [ ] Eat Action
  - [ ] Move to Edible entity (Berry Bush)
  - [ ] Consume over time
  - [ ] Restore Hunger based on nutrition value
- [ ] Drink Action
  - [ ] Move to Drinkable entity (Pond)
  - [ ] Drink over time
  - [ ] Restore Thirst
  - [ ] Increase Bladder need (biological loop)
- [ ] Sleep Action
  - [ ] Move to Sleepable location (ground for MVP)
  - [ ] Enter sleep state
  - [ ] Restore Energy over time (quality affects rate)
- [ ] Toilet Action
  - [ ] Find suitable outdoor spot (not near water, prefer near existing Bio Piles)
  - [ ] Relieve bladder
  - [ ] Spawn Bio Pile entity at location

---

### 🔄 MVP: Player Observation UI
**Spec/Documentation:** `/docs/design/mvp-scope.md`
**Dependencies:** Needs System, AI Decision System
**Status:** ready

**Goal:** Player can observe colonist status and task queue (observation only, no control).

**Tasks:**
- [ ] Colonist Selection
  - [ ] Click on colonist to select
  - [ ] Visual indicator for selected colonist
- [ ] Colonist Info Panel
  - [ ] Display colonist name
  - [ ] Show all 4 need bars (Hunger, Thirst, Energy, Bladder)
  - [ ] Color coding for need urgency (green → yellow → red)
- [ ] Task Queue Display
  - [ ] Show current task with progress
  - [ ] Show pending tasks in priority order
  - [ ] Show reason for each task selection

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

### Mod Support
**Spec/Documentation:** `/docs/technical/asset-system/mod-metadata.md`, `/docs/technical/asset-system/patching-system.md`
**Dependencies:** Folder-Based Asset Migration, Variant Cache System
**Status:** planned (post-MVP)

---

## Blockers & Issues

### SVG Ellipse/Circle Tessellation Bug
**Impact:** Berry Bush and other assets using `<ellipse>` or `<circle>` SVG elements fail to render
**Workaround:** Convert ellipse/circle elements to `<path>` bezier approximations (done for Berry Bush)
**Root Cause:** Ear-clipping tessellator receives degenerate polygons when nanosvg converts circles/ellipses to paths
**Fix Needed:** Either improve tessellator robustness or convert shapes to paths in SVGLoader before tessellation

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
