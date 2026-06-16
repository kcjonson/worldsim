# Project Status

Last Updated: 2026-06-16 (Dev/Test Tools — new /api/dev verbs (spawn/colonist/give/need/time/teleport/select/kill/complete) + synchronous /api/state readback + developer-client Dev Tools tab; API verified end-to-end. Prior: Fluvial Erosion PR #149 draft; Water Availability PRs #144/#146)

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

### ✅ Water Availability (worldgen + landing UX)
**Spec/Documentation:** `/docs/development-log/entries/2026-06-15-worldgen-water-and-plate-realism.md`, `.claude/plans/water-hydrology.md`
**Dependencies:** Climate/Biome/Shelf Retune
**Status:** complete (PRs #144, #146 merged)

**Goal:** Fix the coarse drainage so flow reaches the sea or ponds into basins, set the dead kFlagRiver/kFlagLake flags, and tell the player whether a landing site has fresh water (details pane + globe Hydrology mode). Worldgen stays coarse (amount + direction); actual river/lake geometry is deferred to chunk load.

**Completed Tasks:**
- [x] W-0: water stats baseline (WorldStats + worldgen-cli)
- [x] W-1: priority-flood depression routing + coarse kFlagRiver/kFlagLake + basin spill levels (resolution-invariant river quantile)
- [x] W-2: globe Hydrology color mode
- [x] W-3: landing-site water signal + details pane (river/lake on tile-or-neighbor, coast, rain-fed) + freshwater-biased default site
- [x] W-4: docs + PR; two Copilot rounds cleared; sub-1m lake spill-depth truncation fix (#146)

**Result:** Every land tile routes to the sea or a marked endorheic basin; lakes form in real basins; rivers ~4-5% of land, stable across resolution; landing pane reports water/terrain/climate/difficulty. Deterministic across thread counts. Follow-ups: valley erosion, 2D chunk-time river/lake rendering. ✅

### ✅ Plate Boundary Realism (straight-mountains fix)
**Spec/Documentation:** `/docs/development-log/entries/2026-06-15-worldgen-water-and-plate-realism.md`
**Dependencies:** Tectonic History Simulation
**Status:** complete (PR #145 merged)

**Goal:** Kill the dead-straight mountain walls that appeared across many generated worlds.

**Completed Tasks:**
- [x] Multi-agent diagnosis: root cause is the great-circle rift cut (tryRift plane-split), amplified by a flat plate-size distribution and a uniform belt-lift pedestal
- [x] Curved rift cut: two-front noisy Dijkstra split replacing the great-circle plane (oversized + suture paths)
- [x] Power-law plate-size hierarchy at seeding (few large, many small)
- [x] Along-belt lift modulation so ranges get passes/saddles instead of a uniform wall
- [x] Ultra 2048 generator option; golden hash re-pin; six-seed biome acceptance test; Copilot cleared

**Result:** Rift seams meander like real margins, no straight great-circle cuts on any seed (including low plate counts); the plate inventory has an Earth-like size hierarchy; mountain crests break into passes. Deterministic across thread counts. ✅

### ✅ Climate, Biome & Shelf Realism Retune (worldgen)
**Spec/Documentation:** `/docs/development-log/entries/2026-06-14-climate-biome-shelf-retune.md`
**Dependencies:** Tectonic History Simulation
**Status:** complete (PR #139 merged)

**Goal:** Retune the climate/biome stages (written against the old smooth terrain) for the new linear belts + bimodal hypsometry.

**Completed Tasks:**
- [x] C-1..C-5: meridional wind + distance-to-ocean moisture-advection sweep (rain shadows that scale to wide belts) + continentality temperature; piecewise continental shelf; biome rebalance + montane decouple + biome-fraction acceptance stats; dead-flag hygiene (kFlagCoast/kFlagGlacier); docs + dev log
- [x] Self-review (2 agents) + fixes; two Copilot rounds cleared; CI timeout fix (trimmed heavy acceptance tests)

**Result:** ArcticTundra ~47%→8-16%, HotDesert restored to ~8-10%, forests ~35-40%; visible rain shadows behind belts; shelves submerged ~6%; biomes bend around terrain instead of rigid latitude stripes. Deterministic across thread counts, n=1024 ~16.6s. Follow-up: water-availability hydrology, valley erosion. ✅

### ✅ Tectonic History Simulation (worldgen realism overhaul)
**Spec/Documentation:** `.claude/plans/tectonic-history.md`, `/docs/development-log/entries/2026-06-13-tectonic-history-simulation.md`
**Dependencies:** World Generation & Creator (M1–M6)
**Status:** complete (PR #136 merged)

**Goal:** Replace single-pass Voronoi plates + Gaussian-kernel terrain with a coarse time-stepped tectonic history (Euler-pole motion, subduction, ridges with crust age, collisions/sutures, rifting, slab pull, hotspots) upsampled to full resolution; elevation from Airy isostasy + seafloor depth-age law.

**Completed Tasks:**
- [x] M-T0–M-T6: headless worldgen-cli + WorldStats; PlateSim coarse sim; Wilson-cycle events (incl. M-T2.5 arc crust, M-T2.6 slab pull, M-T2.7 coherence); CrustStage + crustAge/orogenyAge + PlanetIO v3 (incl. M-T3.5/3.6 signed-distance coastlines); TerrainStage rewrite (incl. M-T4.5 linear belts + geodesic metric); deletion/hardening/edge cases; docs + dev log
- [x] Self-review (3 agents) + fixes; Copilot review cleared; flaky cancel test fixed

**Result:** Hexagonal continents and smooth dome mountains gone. Crisp organic coastlines, linear ridged mountain belts (geodesic aspect 5–7), bimodal hypsometry, Earth-like ocean ages, deterministic across thread counts, n=1024 in ~11s. Follow-ups: climate/biome retune, fluvial hydrology. ✅

### ✅ GameScene Subsystem Extraction
**Spec/Documentation:** `/docs/technical/gamescene-refactoring.md`
**Dependencies:** None
**Status:** complete

**Goal:** Extract placement and selection logic from GameScene into dedicated subsystems. Prevent god-class anti-pattern as gameplay systems grow.

**Completed Tasks:**
- [x] PlacementSystem Extraction
  - [x] Create `scenes/game/world/placement/PlacementSystem.h/cpp`
  - [x] Create `scenes/game/world/placement/PlacementTypes.h` (shared BuildMenuItem)
  - [x] Move PlacementMode and GhostRenderer ownership
  - [x] Move spawning/relocation logic (spawnEntity, m_relocatingEntityId)
  - [x] Move UI callbacks (toggleBuildMenu, selectBuildItem, beginRelocation)
- [x] SelectionSystem Extraction
  - [x] Create `scenes/game/world/selection/SelectionSystem.h/cpp`
  - [x] Create `scenes/game/world/selection/SelectionTypes.h` (moved from Selection.h)
  - [x] Move Selection state ownership
  - [x] Move handleClick with priority logic
  - [x] Move renderIndicator
  - [x] Selection priority constants in header
- [x] GameScene Cleanup
  - [x] GameScene is now thin coordinator
  - [x] Subsystems initialized with dependency injection
  - [x] All UI files updated to use new include paths

**Result:** GameScene reduced from ~900 lines to ~620 lines (placement: ~180 lines, selection: ~100 lines extracted). Subsystems now own their own state and logic. ✅

---

### ✅ Main Game UI: Core HUD
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Sections 1, 2, 5, 9)
**Dependencies:** Main Game UI: Primitives Foundation, UI Architecture: ViewModel Pattern
**Status:** complete

**Goal:** Implement main gameplay HUD (top bar, colonist list, gameplay bar, zoom).

**Completed Tasks:**
- [x] TimeSystem Foundation
  - [x] GameSpeed enum (Paused, Normal, Fast, VeryFast)
  - [x] Season enum with advancement
  - [x] effectiveTimeScale() for time-scaled systems
  - [x] NeedsDecaySystem uses TimeSystem
- [x] Top Bar
  - [x] Date/Time display (Day X, Season | HH:MM)
  - [x] Game speed controls ([⏸] [▶] [▶▶] [▶▶▶])
  - [x] Hotkeys (Space pause, 1/2/3 speed)
  - [x] Menu button
- [x] Zoom Controls
  - [x] Floating [+] [⟳] [-] buttons in viewport
  - [x] Home key to reset zoom
- [x] Colonist List Enhancements
  - [x] Portrait Card (avatar + mood bar + name)
  - [x] Status tint (green/yellow/red background based on mood)
  - [x] Click to select, double-click to follow
- [x] Gameplay Bar
  - [x] Category dropdowns: [Actions▾] [Build▾] [Production▾] [Furniture▾]
  - [x] Upward menu expansion (openUpward flag)
  - [x] B key for build toggle
- [x] UI Restructure
  - [x] Created DebugOverlay (extracted from GameOverlay)
  - [x] Created ZoomControlPanel (wrapper for ZoomControl)
  - [x] Deleted GameOverlay and BuildToolbar
  - [x] Layer system refactor for all new components

**Deferred:**
- Vertical scrollable colonist list (works fine at current scale, can add if needed)
- A/Q/R hotkeys (conflict with WASD camera movement)

**Result:** Full HUD implementation with TimeSystem, TopBar with speed controls, GameplayBar with category dropdowns, enhanced colonist list with mood tints and double-click to follow, and floating zoom controls. ✅

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

### ✅ UI Event System
**Spec/Documentation:** `/docs/technical/ui-framework/event-system.md`
**Dependencies:** None
**Status:** complete

**Goal:** Replace manual hit-testing boilerplate (~69 lines of QUICKFIX code) with event consumption. Components call `event.consume()` to stop propagation. MouseMove events handle hover state.

**Completed Tasks:**
- [x] Event Infrastructure
  - [x] Create InputEvent struct with type, position, consumed flag
  - [x] Add handleEvent() to IComponent interface
  - [x] Add containsPoint() to IComponent for hit testing
- [x] Component Updates
  - [x] Update Button to use handleEvent() and consume clicks
  - [x] Update TabBar to use handleEvent()
  - [x] Migrate BuildToolbar, BuildMenu, ColonistListPanel
  - [x] Migrate EntityInfoPanel, TaskListPanel
- [x] Dispatch System
  - [x] Add dispatchEvent() to Component base class
  - [x] GameUI dispatches to all children in z-order
  - [x] GameScene dispatches MouseMove, MouseDown, MouseUp events
- [x] QUICKFIX Cleanup
  - [x] Remove isPointOverUI() from GameUI, GameOverlay
  - [x] Remove isPointOver() from ZoomControl, BuildToolbar, BuildMenu
  - [x] Remove handleInput() polling from all migrated components

**Deferred:** Debug Integration (hit-test endpoint, SSE events) - can be added when needed.

**Result:** Clean event propagation model. Adding new UI panels no longer requires updating multiple files. Components consume events to prevent click-through. ✅

---

### ✅ UI Architecture: Input System Cleanup
**Spec/Documentation:** `/docs/technical/ui-framework/event-system.md`
**Dependencies:** UI Event System
**Status:** complete

**Goal:** Complete the event-based input architecture. Remove `handleInput(float dt)` (wrong pattern per [Game Programming Patterns](https://gameprogrammingpatterns.com/game-loop.html)) in favor of `handleInput(InputEvent&)`.

**Completed Tasks:**
- [x] Remove handleInput() from ILayer/Component (components don't poll)
- [x] Add handleEvent() to TextInput (was missing)
- [x] Container::handleEvent() dispatches to children
- [x] Application creates InputEvents and dispatches to scene
- [x] Rename handleEvent to handleInput on IScene (consistency)
- [x] Remove handleInput(float dt) from IScene (wrong pattern)
- [x] Remove handleInput(float dt) from SceneManager
- [x] Update all scenes to implement handleInput(InputEvent&)
- [x] Add IOverlay interface for cross-scene UI (NavigationMenu, future ESC menu)
- [x] SceneManager manages overlays as peers to scenes
- [x] Test all scenes

**Architecture (Industry Best Practice):**
```
while (running) {
    processInput();    // No dt - discrete events
    update(elapsed);   // dt here - advance simulation
    render();
}
```

**Key insight:** Input handling does NOT need delta time. It captures discrete events ("what happened?"). Delta time belongs in `update()` where simulation advances ("how much time passed?").

**Result:** Clean separation between discrete input events and continuous simulation updates. Scenes receive `InputEvent&` from Application and forward to UI components. Keyboard polling (WASD, ESC) happens in `update()`. ✅

---

### ✅ UI Architecture: FocusManager Simplification
**Spec/Documentation:** `/docs/technical/ui-framework/focus-management.md`
**Dependencies:** UI Architecture: Input System Cleanup
**Status:** complete

**Goal:** Reduce ~40 lines of FocusManager boilerplate per focusable component via CRTP base class.

**Completed Tasks:**
- [x] Create FocusableBase<T> CRTP template (`libs/ui/focus/FocusableBase.h`)
  - [x] Auto-register in constructor
  - [x] Auto-unregister in destructor
  - [x] Handle move semantics correctly (unregister old, register new)
- [x] Migrate Button to use FocusableBase<Button>
- [x] Migrate TabBar to use FocusableBase<TabBar>
- [x] Migrate TextInput to use FocusableBase<TextInput>

**Result:** Components now inherit from `FocusableBase<T>` and use `= default` for move constructor/assignment. FocusManager registration is automatic. ~120 lines of boilerplate removed across 3 components. ✅

---

### ✅ UI Architecture: ViewModel Pattern
**Spec/Documentation:** `/docs/technical/ui-framework/data-binding.md`, `/docs/development-log/plans/2025-12-24-viewmodel-pattern.md`
**Dependencies:** None
**Status:** complete

**Goal:** Establish clear data ownership and efficient updates via ViewModel pattern.

**Completed Tasks:**
- [x] Create directory structure (scenes/game/ui/{adapters,models,panels,components})
- [x] Create UIState.h with Selection struct
- [x] Create ColonistAdapter (ECS query layer)
- [x] Create ColonistListModel with change detection
- [x] Migrate ColonistListPanel to use ViewModel
- [x] Move SelectionAdapter to adapters folder
- [x] Move CraftingAdapter to adapters folder
- [x] Create EntityInfoModel with UpdateType enum
- [x] Migrate EntityInfoPanel to use ViewModel
- [x] File reorganization
  - [x] Move panels to panels/ (EntityInfoPanel, TaskListPanel, BuildMenu, BuildToolbar, GameOverlay)
  - [x] Move reusable components to components/ (NeedBar, ZoomControl, InfoSlot, Selection)
  - [x] Move world rendering to world/ (GhostRenderer, PlacementMode, NotificationManager)
  - [x] Move GameUI to scenes/game/ui/
  - [x] Delete old components/ directory

**Architecture Notes:**
- Pull-based models: `refresh(world)` returns bool for change detection
- Adapter layer: Centralizes ECS queries (ColonistAdapter, SelectionAdapter, CraftingAdapter)
- UpdateType enum: `None`, `Values`, `Structure`, `Show`, `Hide` for tiered updates

**Result:** Clean separation between data (models), ECS queries (adapters), rendering (panels), and reusable UI elements (components). ✅

---

### ✅ UI Architecture: Theme System
**Spec/Documentation:** `/docs/development-log/plans/2025-12-25-ui-theme-system.md`
**Dependencies:** None
**Status:** complete

**Goal:** Centralize UI styling (colors, spacing, typography) via design tokens. Eliminate hardcoded color values across views.

**Terminology:**
- **Panel** (design) = Visual style (dark bg, 1px border)
- **View** (structural) = Composite UI element (EntityInfoView, TaskListView)
- **Component** = Base class for all UI elements (unchanged)

**Completed Tasks:**
- [x] Create `libs/ui/theme/` directory with `Theme.h` and `PanelStyle.h`
- [x] Design tokens: Colors (panel, text, status), Spacing, Typography, Borders
- [x] Panel style factories: `floating()`, `closeButton()`, `actionButton()`, `card()`, `selection()`
- [x] Rename `panels/` folder → `views/`
- [x] Rename `EntityInfoPanel` → `EntityInfoView`
- [x] Rename `TaskListPanel` → `TaskListView`
- [x] Rename `ColonistListPanel` → `ColonistListView`
- [x] Apply theme tokens to EntityInfoView
- [x] Apply theme tokens to TaskListView
- [x] Apply theme tokens to ColonistListView
- [x] Apply theme tokens to BuildMenu

**Deferred (Phase 2):**
- Edge mask support for docked panels (per-edge border control)
- Per-corner radius when edges are removed

**Result:** All views now use centralized theme tokens from `libs/ui/theme/`. Hardcoded colors eliminated. Consistent visual identity across all panels. ✅

---

### ✅ UI Architecture: Layout System
**Spec/Documentation:** `/docs/development-log/plans/warm-honking-hedgehog.md`
**Dependencies:** None
**Status:** complete

**Goal:** Add automatic layout containers for component positioning via uniform layout API on all UI elements.

**Design Decisions:**
- **Uniform API on IComponent** — All UI elements (components + shapes) participate in layout via `getWidth()`/`getHeight()`/`setPosition()` + `margin`
- **CSS-like margin** — Margin adds to position (content draws inside) and adds to reported size
- **Single LayoutContainer** — Direction attribute (Vertical/Horizontal) instead of separate VStack/HStack
- **No Spacer, no flex** — Use margins for spacing between elements
- **Hybrid sizing model:** Stacking axis is child-driven (query sizes), cross axis is parent-driven

**Completed Tasks:**
- [x] Add layout API to IComponent interface (getWidth/getHeight/setPosition/margin)
- [x] Add default implementation to Component base class
- [x] Implement layout API on all Shapes (Rectangle, Circle, Line, Text)
- [x] Update Button to use base class size/position
- [x] Update TextInput to use base class size/position
- [x] Update TabBar to use base class size/position
- [x] Create LayoutTypes.h (Direction, HAlign, VAlign enums)
- [x] Implement LayoutContainer (extends Container)
  - [x] Vertical stacking with HAlign (Left/Center/Right)
  - [x] Horizontal stacking with VAlign (Top/Center/Bottom)
  - [x] Dirty flag for layout caching
  - [x] Container margin offsets children
- [x] Update CMakeLists.txt
- [x] Create LayoutScene demo in ui-sandbox
- [x] Unit tests (14 tests covering all layout scenarios)
- [x] View Migrations
  - [x] Create ColonistListItem component (portrait + name + mood bar)
  - [x] Migrate ColonistListView to use ColonistListItem + LayoutContainer
  - [x] Create SectionHeader component (generic section title)
  - [x] Create StatusTextLine component (status-colored task line)
  - [x] Migrate TaskListView to use SectionHeader + StatusTextLine + LayoutContainer

**Result:** LayoutContainer automatically positions children based on their reported sizes. Components use CSS-like margin for spacing. All UI elements participate in layout through uniform API. ColonistListView and TaskListView now use automatic layout instead of manual yOffset tracking. ✅

---

### ✅ Main Game UI: Complex Components
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Section 17)
**Dependencies:** ~~Main Game UI: Primitives Foundation~~ (complete)
**Status:** complete

**Goal:** Build components for information-dense screens (resources, dropdowns, notifications).

**Completed Tasks:**
- [x] Icon Component
  - [x] SVG rendering with tessellation pipeline (loadSVG → Tessellator → drawTriangles)
  - [x] Tinting/colorization support
  - [x] Unit tests (11)
  - [x] IconScene demo
- [x] Tree View Component
  - [x] Expandable/collapsible nodes (▶/▼)
  - [x] Nested hierarchy with indentation
  - [x] Flattened rows pattern for efficient iteration
  - [x] Unit tests (16)
  - [x] TreeViewScene demo
- [x] Dropdown Button Component
  - [x] Button with ▾ indicator
  - [x] Expands menu panel on click
  - [x] Closes on focus lost via FocusableBase
  - [x] Keyboard navigation (Up/Down/Enter/Escape)
  - [x] Disabled item support
  - [x] Unit tests (17)
  - [x] DropdownScene demo
- [x] Toast + Toast Stack
  - [x] Notification popup with title, message, dismiss button
  - [x] Auto-dismiss timer with countdown display
  - [x] Persistent mode (autoDismissTime = 0)
  - [x] Severity styling (Info/Warning/Critical colors)
  - [x] Fade in/out animation
  - [x] ToastStack container with vertical stacking
  - [x] Anchor positions (TopRight, TopLeft, BottomRight, BottomLeft)
  - [x] Max toasts limit with oldest removal
  - [x] Unit tests (23: 14 Toast + 9 ToastStack)
  - [x] ToastScene demo

**Result:** All complex components implemented with theme-based styling, layout integration, and comprehensive test coverage (67 new tests total). Components use minimal constructors with sensible defaults. ✅

---

## In Progress Epics

### Dev/Test Tools (HTTP verbs + developer-client tab)
**Spec/Documentation:** `.claude/plans/alright-these-seem-good-hidden-wadler.md`, `/docs/development-log/entries/2026-06-16-dev-tools-api-and-tab.md`
**Dependencies:** None
**Status:** in progress (verbs + readback + tab done; tab not yet exercised in-browser, no PR)

**Goal:** Put the sim into a desired state on demand for manual + agent testing, driven from a Dev Tools tab in the developer-client. Debug server stays domain-agnostic; GameScene interprets the verbs.

**Tasks:**
- [x] Step 1: `spawn`, `colonist`, `give` (replaces `givewood`; site/loose/colonist/storage)
- [x] Step 2: `need`, `time` (speed/set/skip), `teleport`, `select`, `kill`, `complete`
- [x] Step 3: synchronous `/api/state?what=summary|colonists|construction|time` (screenshot-style handshake)
- [x] Phase 2: Dev Tools tab (DevToolsService + DevToolsPanel) in developer-client static build
- [x] `parseDevVerb` tests refreshed (7/7 green); API verified end-to-end via curl + readback + screenshot
- [ ] Exercise the tab in-browser (open static `dist/index.html` against a running game)
- [ ] Optional: `demolish` verb, inventory in colonist readback, `/api/state?what=query|count`
- [ ] PR

### Fluvial Erosion (worldgen)
**Spec/Documentation:** `.claude/plans/erosion.md`, `/docs/development-log/entries/2026-06-15-worldgen-fluvial-erosion.md`
**Dependencies:** Water Availability (drainage)
**Status:** in progress (PR #149, draft)

**Goal:** Carve valleys into the continental terrain so rivers (rendered later from the climate drainage) land in real valleys. Detachment-limited stream-power incision (Braun & Willett implicit solver) on the drainage stack, between Terrain and Atmosphere; downstream climate/biomes/final drainage all see the carved terrain.

**Tasks:**
- [x] E-0: dissection metrics baseline (WorldStats + worldgen-cli)
- [x] E-1: ErosionStage core (shared DrainageRouting helper + implicit stream-power) + pipeline integration
- [x] E-2: downstream re-validation (carved terrain through climate/biomes/final drainage; determinism; budget)
- [x] E-3: strength at conservative subtle default (accepted)
- [x] E-4: dev log + status (this) on the draft PR
- [ ] Mark PR #149 ready, clear CI + Copilot, merge

**Result:** Valleys carved deterministically (worldHash bit-identical across thread counts); mountains preserved (belt crests stay tall); world-tests green (174); gen ~22s at n=1024. Follow-ups: the bathymetry "comb" artifact (hex-BFS distance terracing on the shelf edge + crust-age depth field) is a separate deferred tuning task; then 2D chunk-time river/lake rendering.

### World Generation & Creator
**Spec/Documentation:** `/docs/design/features/world-generation/`, `.claude/plans/world-generation.md`
**Dependencies:** None
**Status:** in progress

**Goal:** Real 8-phase world generation (plates → terrain → climate → biomes), WorldCreator scene with 3D planet view and landing site selection, wired into New Game → 2D gameplay sampling the generated world.

**Tasks:**
- [x] M1: Foundation primitives (TaskPool, RNG, HashNoise, DeterministicMath, WorldHash)
- [x] M2: worldgen core + frozen contracts (SphereGrid, WorldData, pipeline skeleton, debug image exporter, technical doc)
- [x] M3a: Plates + plate movement (P1, P2)
- [x] M3b: Terrain + sea level (P3) — ends with USER screenshot review gate
- [x] M3c: Atmosphere + temperature (P4)
- [x] M3d: Precipitation + rivers (P5)
- [x] M3e: Oceans, biomes, snow, summary (P6–P9)
- [x] M3f: planet-view lib (renderer, camera, picker, colorizer; chunked-LOD deferred to M3f-2)
- [ ] Hex conversion + scalable crisp globe rendering (`.claude/plans/lets-work-on-the-serene-waterfall.md`)
  - [x] Phase 1: SphereGrid Goldberg hex semantics (vertex-centered tiles, 10n²+2, cube-round assignment, 6-neighbor offsets, locateHex)
  - [x] Phase 2: consumers re-baselined (neighbors=6, locateHex sampling, PlanetIO version bump)
  - [x] Phase 3: two-tier crisp rendering — base mips (async bake, dirty-flag coalescing) + detail page cache (130×130 pages, 2D-array atlas, LRU, page table), per-pixel hex assignment in planet.frag, camera deep-zoom near-plane + n-aware min distance. Supersedes the M3f-2 "chunked LOD" placeholder.
  - [x] Phase 4: docs + dev-log entry — world-generation-implementation.md (Goldberg grid, TileId amendment, frozen-contract amendments, locateHex, diagonal B-D fix, PlanetIO v2); planet-view-rendering.md (two-tier architecture, scaling table, budgets); 3d-to-2d-sampling.md (locateHex addendum); data-model.md + technical-notes.md (hexes now literal); dev log 2026-06-12-worldgen-hex-goldberg.md
  - [x] Phase 5: end-to-end verification (New Game flow, far-tier washout fix, color-mode cycling, zoom crispness, Quick Start v2 cache save/load) + PR
- [x] M3g: UI Slider + WorldCreator shell
- [x] M3h: Biome taxonomy migration (engine, 8→21 biomes, sparse BiomeWeights)
- [x] M4: PlanetSampler + GeneratedWorldSampler + GameLoading branch
- [x] M5: WorldCreator integration (real generator + planet view)
- [x] M6: Landing site + handoff + MainMenu rewire (e2e New Game)
  - [x] Quick Start: cached pre-generated planet (PlanetIO save/load), bypasses generation
  - [x] Persist accepted world on landing confirm
  - [ ] Landing site local preview + difficulty rating (UX spec, deferred)
- [ ] M7: Hardening, benchmark-gated default resolution, cross-platform determinism
- [ ] Future: planet database — mmap/streamed reads from PlanetIO files for n>=4096 planets instead of whole-planet RAM residency (PlanetIO v1 SoA layout is already offset-addressable; see development log)

---

### Building & Construction System
**Spec/Documentation:** `/docs/design/game-systems/world/building-construction.md` (design), `/docs/technical/building-construction-architecture.md` (architecture)
**Dependencies:** Goal-Driven Task Generation (build/haul task sources); Navigation & Pathfinding before walls ship as gameplay (walls don't block movement until then); Material economy — extend the early crafting implementation with choppable trees and construction material items (axe/chop work in flight; chains grow as systems need them)
**Status:** in progress

**Goal:** Freeform polygon building: foundation blueprints drawn on the map, walls on foundations with snapping and edge-fill, doors/windows as parameterized vector assets, automatic room detection. Deliver-then-build loop with work-driven procedural construction visuals.

**Tasks:**
- [x] Epic A: Geometry foundations (libs/geometry in-house: int64-mm core + Int128, exact predicates, polygon constraint primitives, planar arrangement + half-edge face extraction, ring booleans, wall band offsetting + junction trimming; 157 unit tests) — merged to main (#135)
- [x] Epic C: Foundations end-to-end — merged to main (#137, hardened by an adversarial review: goal-collision umbrella restructure, deterministic option ordering, fail-open validation, deferred-demolish UAF, more). Verified in sandbox: draw a foundation, colonist chops palms for Wood, hauls it to the site, builds it to completion, renders progress. Polish items remain (see sub-list).
  - [x] Config + ConstructionRegistry + ConfigValidator (materials/constraints/snapping, meters + mm mirrors)
  - [x] ConstructionWorld topology store (commit/add/subtract/remove via geometry booleans, overlap rejection, hit-test, version counter)
  - [x] Blueprint ECS components (Structure, StructureBlueprint manifest+work+phase, StructureHealth)
  - [x] DrawingSystem + FoundationTool + SnapEngine + ConstructionValidator + config strip (verified in sandbox: draw → commit → blueprint entity → toast)
  - [x] Selection + info panel adapter + immediate demolish (verified in sandbox)
  - [x] Material economy: Wood item + choppable Oak/Maple trees (reuses Harvestable path)
  - [x] Build/Deconstruct actions (skill-scaled workDone += rate×dt, completion callbacks; 14 tests)
  - [x] ConstructionSystem lifecycle (Clearing→AwaitingMaterials→UnderConstruction→Complete; clear/haul/build goals; blueprint-inventory delivery; build-progress render ramp)
  - [x] Choppable palm trees added to the Beach biome (Wood source at spawn) so the loop is exercisable
  - [x] End-to-end proof of chop→haul→build in a live world — verified in sandbox (foundation built to completion, ~1 min sim time). Fixed a goal-thrashing bug found in the process (harvest demand now bounded by carried Wood; harvest→haul chained for delivery stickiness).
  - [ ] Polish (deferred): baked element-emitter index-prefix progress render (D8), ConstructionProgressSlot in the info panel (D11), N discrete builder work slots from builderCap, deconstruct material refund + cascade, mining/haul-away clearing of non-harvestable obstructions
- [x] Epic D: Walls — draw/build/select/demolish, verified in sandbox (draft PR #138). Polyline wall chains on a foundation, T-junction split, band+junction rendering, build via the shared lifecycle (gated on host-foundation-built), per-segment selection/panel/demolish.
  - [x] Wall topology in ConstructionWorld (vertices/segments/openings, T-junction split + opening re-attach, X-crossing rejection, atomic commit); 18 tests
  - [x] Wall config (thickness presets Light/Standard/Heavy, wall constraints) + validation; 30 tests
  - [x] SnapEngine.snapWall (endpoint/vertex/T-junction/edge/angle) + wall draw-time validation (length/angle/containment/overlap/clearance/X-crossing); 27 tests
  - [x] WallTool (chain draw, Ctrl+click edge fill, commit→segment blueprint entities) + interim band/junction render via WallOffset + config strip wall mode; verified in sandbox
  - [x] Per-segment selection + adaptWallSegment panel + band-outline indicator + immediate per-segment demolition; verified in sandbox
  - [x] Hardened: snap endpoint-vs-T-junction, validator T-junction exemption, atomic commitSegment, harvest demand bounded to carry capacity (structures needing >1 stack build over multiple trips)
  - [ ] Deferred: D5 obstacle/portal publication to nav (no consumer until the nav epic); work-driven Deconstruct + refund + host-can't-demolish-while-walls-stand guard (same deferred polish as foundations); partial edge-fill
- [x] Epic E: Rooms — detection + room entities + room-formed toast + identity persistence (feature/construction-rooms). Verified in sandbox: a closed built-wall loop fires "Room formed"; an interior divider splits it and the rep-containing half keeps its name.
  - [x] RoomDetection pure core (built wall centerlines → geometry arrangement/half-edge face extraction → bounded faces as rooms; openings don't break enclosure); 6 tests
  - [x] RoomDetectionSystem (polls ConstructionWorld.version(), reconciles against persistent room records, max-overlap identity so names survive edits, spawns/refreshes/retires room entities, room-formed callback seam); 6 tests
  - [x] Room ECS component + GameScene wiring (register system, "Room formed" toast via the engine→UI callback seam)
  - [x] /api/dev/walls dev command (stamp a built wall loop in one call; rooms testable without the draw tool); reused for sandbox verification
  - [x] Hardened (adversarial review): OnBoundary fallback so a divider through a room's rep keeps identity on one side; dev-walls T-split only force-builds chain segments (not split halves of pre-existing blueprints)
  - [x] Rooms overlay UI: scene-owned RoomOverlay tints/outlines/labels each room (concave-correct via renderer::Tessellator), click-to-select (overlay-gated) → read-only info panel (Name/Area/enclosing walls), HUD "Rooms" toggle synced with the R hotkey (feature/rooms-overlay)
  - [ ] Deferred: nested room-in-room (loop inside a loop, no connecting wall) identity/area needs hole-aware face extraction (pinned by a test); room types/functions/bonuses (post-v1)
- [x] Epic F1: Openings end-to-end (interim visuals) — doors/windows on walls (feature/construction-openings). Verified in sandbox: a Door placed on a built wall shows a gap in the wall band with the door fill; clicking selects it (panel: Type/Material/Pathable/State/Materials/Work + Demolish).
  - [x] F1a engine: opening types config (Door 0.9m pathable, Window 0.6m) + ConfigValidator; ConstructionValidator.validateOpening (margins, overlap, length, type/material); SnapEngine.snapOpening; ConstructionWorld setOpeningState/Entity/removeOpening
  - [x] F1b lifecycle: openings in the build loop (own materials + constant work), gated on host segment built (isOpeningHostSegmentBuilt)
  - [x] F1c app: OpeningTool (slide along wall, validate, commit→addOpening+spawn blueprint) + config strip + GameplayBar Door/Window + /api/dev/opening
  - [x] F1d render: interim wall-band gap + procedural door/window fill + ghost (Primitives)
  - [x] F1e selection: OpeningSelection (priority above walls) + point-in-footprint hit test + adaptOpening panel + per-opening demolish
  - [x] Hardened (adversarial review): wall-demolish now despawns hosted openings' entities (removeSegment surfaces them); openingMarginMeters sign-checked; dev helpers include Opening
  - [ ] F2 (deferred): D9 parameter-extended procedural Lua door/window assets (cache key (defName,thicknessPreset,material), material palettes); portal publication to nav (no consumer yet). NOTE: retrofit-cut-as-work is CUT from scope — openings sit on the wall and never cut it (design decision 2026-06-15).
- [ ] Epic G: Editing & polish
  - [x] Demolish-building cascade + work-driven demolish (deconstruct task + material refund) + foundation-demolish gate (#143)
  - [x] Outer-face-flush wall snapping so walls build along foundation edges (#147)
  - [ ] G2: click-cycling selection (repeated click cycles opening → wall → foundation)
  - [ ] G3: foundation add/subtract tool (merge/subtract via the geometry booleans)
  - [ ] Deferred: vertex editing (drag/add/delete on a foundation blueprint), full Shift-click multi-select, double-click connected wall-run select

---

### Goal-Driven Task Generation
**Spec/Documentation:** `/docs/design/game-systems/colonists/task-registry.md`, `/docs/technical/task-generation-architecture.md`
**Dependencies:** ~~Task Ordering System~~ (complete)
**Status:** in progress (core landed in PR #115; item reservations and Memory push integration remain)

**Goal:** Refactor task generation from discovery-driven (see item → create task) to goal-driven (goal exists → create task using Memory for fulfillment). This fixes the issue of thousands of invalid tasks appearing in the global task list.

**Architecture Change:**
- Discovery → Updates **Memory** (what colonists know about)
- Goals → Generate **Tasks** (what needs to be done)
- Tasks exist at goal-level (~200), not item-level (~100,000)
- Reservations are item-level (allows parallel work by multiple colonists)

**Tasks:**
- [x] Phase 1: Remove Discovery-Based Task Generation
  - [x] Remove VisionSystem task creation for Carryable items
  - [x] Remove VisionSystem task creation for Harvestable items
  - [x] Clean up GlobalTaskRegistry discovery-driven methods (registry deleted entirely)
- [x] Phase 2: Goal Source Integration
  - [x] StorageGoalSystem creates Haul goals when capacity available
  - [x] CraftingGoalSystem creates Craft + Harvest + Haul goal hierarchies when recipe queued
  - [x] NeedsSystem creates FulfillNeed tasks when need below threshold (already correct)
  - [x] BuildGoalSystem creates PlacePackaged goals when build order placed
- [ ] Phase 3: Two-Level Task/Reservation Model
  - [x] GoalTask struct (destination, type, acceptedTypes)
  - [ ] Reservation system at item-level (prevents conflicts; the unwired API was removed in PR #115, needs real integration with AIDecisionSystem/ActionSystem)
  - [ ] availableCount() computation (known items minus reserved; currently target minus delivered)
- [ ] Phase 4: Memory Integration
  - [ ] Memory notifies registry of fulfillment options (not tasks)
  - [ ] Task availability updates based on Memory changes
  - [x] Colonist filtering by what they know (AIDecisionSystem matches goals against colonist Memory)
- [ ] Phase 5: UI Updates
  - [x] Task list shows goal-level tasks with parent/child hierarchy
  - [ ] "Blocked" status when no fulfillment options known
  - [ ] Sub-demand display for multi-type storage

---

### ✅ Storage and Hauling System
**Spec/Documentation:** `/docs/design/features/storage-system.md`, `.claude/plans/storage-and-hauling.md`
**Dependencies:** ~~Main Game UI: Colonist Details Dialog~~ (complete)
**Status:** complete

**Goal:** Allow crafting furniture (shelves, boxes), automatic hauling of loose items to storage.

**Completed Tasks (All Phases):**
- [x] Phase 1+3: ItemCategory, StorageCapability, handsRequired parsing
  - [x] ItemCategory enum (None, RawMaterial, Food, Tool, Furniture)
  - [x] StorageCapability struct (acceptedCategories, maxCapacity, maxStackSize)
  - [x] handsRequired field on AssetDefinition (1 or 2 hands)
  - [x] XML parsing for category, handsRequired, storage capability
- [x] Phase 2: Inventory hand slots
  - [x] leftHand/rightHand optional ItemStack
  - [x] Hand query methods (freeHandCount, hasHandsFree, isHolding)
  - [x] Hand mutation methods (pickUp, putDown, stowToBackpack, takeFromBackpack)
  - [x] Two-handed items can't fit in backpack (enforced by stowToBackpack)
- [x] Phase 4: Packaged component + Place/Package UI
  - [x] Packaged ECS component (marker for unplaced furniture)
  - [x] FurnitureSelection type with isPackaged flag
  - [x] ActionButtonSlot for [Place]/[Package] buttons
  - [x] Selection adapter and UI rendering support
- [x] Phase 5: Container assets & recipes
  - [x] BasicShelf asset (Tool storage, 20 slots)
  - [x] BasicBox asset (RawMaterial storage, 50 slots)
  - [x] Placeholder SVGs for both
  - [x] Recipes (10 Sticks + 5 PlantFiber for shelf, 8 Sticks + 3 PlantFiber for box)
  - [x] Added category to Stick, SmallStone, PlantFiber (RawMaterial) and Berry (Food)
- [x] Phase 6-7: Haul task/action + ground item spawning
  - [x] TaskType::Haul in TaskType enum
  - [x] ActionType::Deposit for storage deposits
  - [x] Two-phase haul action (Pickup at source → Deposit at storage)
  - [x] Haul task fields (haulItemDefName, haulSourcePosition, haulTargetStorageId, haulTargetPosition)
  - [x] Storage containers get Inventory component when spawned
- [x] Phase 8: AI haul evaluation
  - [x] evaluateHaulTask in AIDecisionSystem::buildDecisionTrace
  - [x] Priority Tier 6.4 (between crafting and gathering)
  - [x] Find loose Carryable items and match to storage containers by ItemCategory
  - [x] ECS view for storage containers with actual entity IDs

---

### ✅ Main Game UI: Colonist Details Dialog
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Section 8)
**Dependencies:** ~~Main Game UI: Interaction Components~~ (complete), ~~Main Game UI: Primitives Foundation~~ (complete), ~~UI Architecture: ViewModel Pattern~~ (complete)
**Status:** complete (merged PR #96)

**Goal:** Full colonist information display with 5 tabs, live updates while game runs.

**Completed Tasks:**
- [x] Dialog Structure (ColonistDetailsModel, ColonistDetailsDialog, GameUI integration)
- [x] Bio Tab (name, mood, current task)
- [x] Health Tab (8 needs as ProgressBars, mood summary)
- [x] Social Tab (placeholder)
- [x] Gear Tab (inventory items)
- [x] Memory Tab (TreeView with collapsible categories, scrollable)
- [x] Refactoring (tabs/ subfolder, TabStyles.h)

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

## Planned Epics

### 2D Render Performance Overhaul
**Spec/Documentation:** `.claude/plans/render-performance-overhaul.md`, `/docs/development-log/entries/2026-06-09-render-performance-analysis.md`
**Dependencies:** None
**Status:** ready

**Goal:** Fix the measured collapse at zoom-out (4 FPS at 0.25x) and scroll hitches (112-443ms). Persistent GPU tile geometry, async entity bake, far-zoom impostor handoff. Target 120 FPS at every zoom at current flora density (headroom for massively increased density later); 4x density stress must hold 60+.

**Tasks:**
- [x] Profiling tooling (camera/vsync control endpoints, perf-capture.ps1, draw call metrics fix)
- [x] Phase 1: Per-chunk tile-data textures (one quad per chunk; deleted per-frame drawTile path)
- [x] Phase 2: Async chunk generation + worker entity mesh bake + budgeted GPU uploads
- [x] Phase 3: Far-zoom impostor handoff (height-bucketed meshes, zoom cutoff + fade)
- [x] Windows frame pacing fix (timeBeginPeriod + sleep-then-spin; was capped ~60 FPS)
- [ ] Phase 4: Remaining metrics correctness (GPU timer, Windows SystemResources, per-window max breakdown)
- [ ] Phase 5: Small wins (AABB cache, LRU eviction, zoom-aware load radius, View smallest-pool, uniform-chunk fast path: skip the full 512x512 computeTile loop when all 4 sampled corners agree, e.g. deep-ocean chunks)
- [ ] 4x-density scroll hitch attribution (p99 64ms at 4x; within criteria but worth chasing before density increase lands)

---

### Living Environment Rendering
**Spec/Documentation:** `.claude/plans/living-environment-rendering.md`
**Dependencies:** 2D Render Performance Overhaul (Phases 1-3)
**Status:** ready

**Goal:** Wind-blown grass at scale, grass parting around colonists with footprint trails, animated reactive water. Vertex-shader animation + interaction displacement maps; no per-frame CPU tessellation.

**Tasks:**
- [ ] M-A: WindSystem + vertex-shader wind for instanced and baked flora
- [ ] M-B: Interaction displacement map (trampling) + footprint persistence
- [ ] M-C: Water ripple/foam/sparkle + interaction rings
- [ ] M-D: Far-zoom wind sheen, tree sway, particles, blob shadows

---

### ✅ Task Ordering System
**Spec/Documentation:** `/docs/design/game-systems/colonists/task-registry.md`, `.claude/plans/task-ordering-system.md`
**Dependencies:** ~~Storage and Hauling System~~ (complete)
**Status:** complete

**Goal:** Global task prioritization where all colony tasks are computed with numerical priority (int16), each colonist filters/modifies priorities based on skills/preferences, and in-progress/follow-up tasks receive priority bonuses.

**Tasks:**
- [x] Phase 1: Config Infrastructure
  - [x] Create `assets/config/work/` folder structure
  - [x] Implement ActionTypeRegistry (action-types.xml)
  - [x] Implement TaskChainRegistry (task-chains.xml)
  - [x] Implement WorkTypeRegistry (work-types.xml)
  - [x] Implement PriorityConfig (priority-tuning.xml)
  - [x] Implement ConfigValidator (fail-fast validation)
  - [x] Unit tests for config loading and validation (19 tests)
- [x] Phase 1.5: Config Loading Integration
  - [x] Add loadWorkConfigs() to GameLoadingScene::initializeWorldSystems()
  - [x] Add ConfigError loading phase with error display
  - [x] Update status text ("Loading configuration...")
  - [x] Test reload cycle (menu → game → menu → game)
  - [x] Refactor GameLoadingScene.cpp to remove m_ prefixes (coding standards)
- [x] Phase 2: Global Task Registry
  - [x] Create GlobalTask struct (worldEntityKey, type, position, knownBy, reserved, chainId)
  - [x] Implement GlobalTaskRegistry with memory-sourced tasks
  - [x] Wire VisionSystem to notify registry on entity discovery
  - [x] Implement reservation system with timeout
  - [x] Spatial indexing for radius queries
  - [x] Unit tests (10 tests passing)
- [x] Phase 3: Skills Component
  - [x] Create Skills component with skill→level map
  - [x] Add skill bonus calculation to priority formula
  - [x] Wire skill requirements to work type filtering
- [x] Phase 4: Priority Scoring Integration
  - [x] Add chainId, chainStep fields to Task component (moved from Phase 5)
  - [x] Add priority bonus fields to EvaluatedOption (distance, chain, inProgress, taskAge)
  - [x] Implement full priority formula using PriorityConfig
  - [x] Wire AIDecisionSystem to use GlobalTaskRegistry for task age lookup
  - [x] Add in-progress bonus for current task (+200)
  - [x] Performance: TaskCache for O(1) registry lookup (batch query + hash map)
  - Note: GlobalTaskRegistry used for task age only; full integration (registry as task source) in Phase 5/6
- [x] Phase 5: Task Chaining
  - [x] Add chain ID generation and assignment for Haul/PlacePackaged tasks
  - [x] Implement chain continuation bonus (+2000) in AIDecisionSystem
  - [x] Track chainStep progression in ActionSystem (phase transitions)
  - [x] Handle chain interruption based on handsRequired (stow 1-hand, drop 2-hand)
  - [x] Add 6 unit tests for chain scenarios
  - Note: "Refactor Haul to use Chain_PickupDeposit" deferred - current implementation works correctly with chainId/chainStep
- [x] Phase 6: Task List UI
  - [x] Part A: Global Task List Panel (top-right, collapsible, all tasks)
    - [x] GlobalTaskAdapter (ECS query layer)
    - [x] GlobalTaskListModel (cache + 5Hz throttle)
    - [x] GlobalTaskRow component (two-line display)
    - [x] GlobalTaskListView (collapsible panel with scroll)
    - [x] GameUI integration
  - [x] Part B: Tasks Tab in Colonist Details Dialog
    - [x] TasksTabView (scrollable task list for colonist)
    - [x] ColonistDetailsModel.extractTasksData()
    - [x] ColonistDetailsDialog integration (6th tab)

**Related Specs:**
- [Task Registry](./design/game-systems/colonists/task-registry.md) — Core architecture
- [Priority Config](./design/game-systems/colonists/priority-config.md) — Tunable weights
- [Work Types Config](./design/game-systems/colonists/work-types-config.md) — Work type definitions
- [Task Chains](./design/game-systems/colonists/task-chains.md) — Multi-step tasks
- [Task Generation Architecture](./technical/task-generation-architecture.md) — Technical deep-dive
- [Config Loading](./technical/config-loading.md) — When/how configs load (mod support)

---

### UI Architecture: Animation System
**Spec/Documentation:** `/docs/technical/ui-framework/animation-system.md` (to be written)
**Dependencies:** None
**Status:** needs spec

**Goal:** Add tweening system for smooth UI transitions.

**Background:** The main-game-ui-design spec calls for smooth animations (modal fade-in, expand/collapse, toast slide-up). Currently no animation support.

**Tasks:**
- [ ] Create Animator class
  - [ ] animate(float& value, target, duration, easing)
  - [ ] Per-frame update()
  - [ ] Common easing functions (linear, easeOut, easeInOut)
- [ ] Integrate with Component lifecycle
  - [ ] Components can own Animator instance
  - [ ] Update called during update() phase
- [ ] Demo animations
  - [ ] Fade in/out
  - [ ] Slide transitions
  - [ ] Progress bar value changes

---

### ✅ Main Game UI: Primitives Foundation
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Section 17)
**Dependencies:** ~~UI Architecture: Layout System~~ (complete)
**Status:** complete

**Goal:** Complete scroll container and generalize progress bar for reuse across UI.

**Completed Tasks:**
- [x] ProgressBar Component (`libs/ui/components/progress/`)
  - [x] Generic progress bar with normalized 0-1 value
  - [x] Configurable fill color (not value-based gradient)
  - [x] Optional label support (label left, bar right)
  - [x] 10 unit tests covering construction, value clamping, positioning
- [x] NeedBar Refactor
  - [x] NeedBar now wraps ProgressBar internally
  - [x] API unchanged (0-100 scale, valueToColor gradient)
  - [x] Visual appearance identical
- [x] ScrollContainer Component (`libs/ui/components/scroll/`)
  - [x] Encapsulates scroll logic (content height, viewport, scroll bounds)
  - [x] Mouse wheel event handling
  - [x] Scrollbar visuals (track + thumb using Theme colors)
  - [x] Scrollbar thumb dragging
  - [x] Click track to jump to position
  - [x] Auto content height detection from LayoutContainer child
  - [x] 15 unit tests covering construction, scroll bounds, position, viewport resize, hit testing
- [x] ScrollScene Demo in ui-sandbox
  - [x] Basic scrollable list
  - [x] Scrollable button list with LayoutContainer
  - [x] ProgressBar showcase with different colors

**Result:** Generic progress bars and scrollable containers available in `libs/ui/` for reuse across all UI. NeedBar simplified to thin wrapper. ✅

---

### ✅ Main Game UI: Interaction Components
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Sections 8, 14, 17)
**Dependencies:** ~~Main Game UI: Primitives Foundation~~ (complete)
**Status:** complete

**Goal:** Build dialog, tooltip, and context menu systems.

**Completed Tasks:**
- [x] Dialog Component
  - [x] Full-screen semi-transparent overlay
  - [x] Centered content panel with title bar
  - [x] Close via [X] button, Escape, or outside click
  - [x] Fade in/out animation
  - [x] Focus scope for keyboard capture
  - [x] Unit tests (18)
- [x] Tooltip System
  - [x] TooltipManager singleton for global coordination
  - [x] 0.5s hover delay (Theme.Tooltip.hoverDelay)
  - [x] Smart positioning (stay on screen)
  - [x] Fade in/out animation
  - [x] Content: title, description, hotkey
  - [x] Unit tests (19: 9 Tooltip + 10 TooltipManager)
- [x] Context Menu Component
  - [x] Right-click popup menu
  - [x] Menu items with enabled/disabled state
  - [x] Keyboard navigation (Up/Down/Enter/Escape)
  - [x] Click-outside-to-close
  - [x] Screen edge clamping
  - [x] Unit tests (17)

**Result:** All interaction components implemented with minimal APIs, theme-based styling, and comprehensive test coverage (54 new tests). Components use FocusableBase for keyboard handling. ✅

---

### ✅ Main Game UI: Information Systems
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Sections 4, 6)
**Dependencies:** Main Game UI: Complex Components, UI Architecture: ViewModel Pattern
**Status:** complete

**Goal:** Add resources panel and notifications to the game UI.

**Completed Tasks:**
- [x] Resources Panel
  - [x] Collapsed: [Storage ▼] button
  - [x] Expanded: Empty state message (stockpiles not yet implemented)
  - [x] Position below zoom controls in top-right
  - [x] Uses Component pattern with addChild for layering
- [x] Notifications System
  - [x] ToastStack integration in GameUI
  - [x] Position bottom-right, above gameplay bar
  - [x] Click-to-navigate callback support (onClick)
  - [x] pushNotification() API on GameUI
  - [x] Deleted old NotificationManager (replaced by ToastStack)
- [x] Z-Order Fix
  - [x] Remove explicit zIndex from TopBar/GameplayBar backgrounds
  - [x] Use insertion order for parent/child layering

**Deferred:**
- Resources Panel TreeView population (requires stockpile system)
- Pin-to-always-show functionality

**Result:** Notification toasts appear in bottom-right with click-to-navigate. Resources panel shows collapsible "Storage" button with empty state. Z-order rendering fixed across all bars. ✅

---

### Main Game UI: Minimap
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Section 3)
**Dependencies:** Main Game UI: Information Systems
**Status:** planned

**Goal:** Add minimap for world overview and navigation.

**Tasks:**
- [ ] Minimap Rendering
  - [ ] Render-to-texture world overview
  - [ ] Terrain colors by surface type
  - [ ] Building indicators
  - [ ] Colonist dots
  - [ ] Threat indicators (red)
- [ ] Minimap Interaction
  - [ ] Camera viewport rectangle overlay
  - [ ] Click to navigate camera
  - [ ] Minimap zoom controls (+/-)
- [ ] Position in top-right, above Resources Panel

**Notes:** Deferred from Information Systems epic due to GPU complexity. May require significant render-to-texture optimization work.

---

### Main Game UI: Management Screens
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Section 11)
**Dependencies:** Main Game UI: Interaction Components (Modal), UI Architecture: ViewModel Pattern
**Status:** planned

**Goal:** Full-screen management overlays.

**Tasks:**
- [ ] Work Priorities Screen (`W` key)
  - [ ] Grid: colonists × work types
  - [ ] Priority 1-4 or disabled (—)
  - [ ] Click to cycle, right-click to disable
- [ ] Schedule Screen (`S` key)
  - [ ] Hour-by-hour grid (0-23)
  - [ ] Block types: Sleep, Work, Recreation, Anything
  - [ ] Drag to paint time blocks
- [ ] Research Screen
  - [ ] Tech tree visualization with dependencies
  - [ ] Current research progress bar
- [ ] History/Log Screen (`H` key)
  - [ ] Scrollable event log
  - [ ] Timestamped entries
  - [ ] Filter by category

---


### Main Game UI: Polish & worldsim Features
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Sections 12, 14, 16)
**Dependencies:** Most other Main Game UI epics
**Status:** planned

**Goal:** Final polish and worldsim-unique visualization features.

**Tasks:**
- [ ] Camera & Selection
  - [ ] Location bookmarks (Shift+1-9 to set, 1-9 to jump)
  - [ ] Box selection, Shift/Ctrl-click multi-select
  - [ ] Backspace to return to previous location
- [ ] Tooltips Throughout
  - [ ] Add tooltips to all interactive elements
- [ ] Memory Visualization Mode (`M` key when colonist selected)
  - [ ] Overlay: bright = entities colonist knows, dim = unknown
- [ ] World-Space UI
  - [ ] Selection indicators (white outline)
  - [ ] Colonist labels toggle (`L` key)
  - [ ] Threat indicators (red pulsing circles)
  - [ ] Off-screen threat direction arrows

---

### Vision System: Occlusion & Discovery
**Spec/Documentation:** `/docs/technical/vision-architecture.md`
**Dependencies:** Geometry Foundations (libs/geometry); consumes Building & Construction's structure publication for occluders
**Status:** planned

**Goal:** Honest sight: GeometryIndex shared with navigation, visibility polygons with an outdoor fast path, discovery/witnessing/stale-memory reconciliation through visibility, windows pass sight while blocking movement, structures discoverable per segment. Must land with or before Navigation P4 (belief filtering is hollow without it). **Fog of war explicitly excluded** — separate later epic alongside the overlay system; the polygon data it needs comes free from this work.

**Tasks:**
- [ ] GeometryIndex (segment store + transparency flags + version counter, shared with nav obstacle publication)
- [ ] Visibility polygons (rotational sweep, outdoor fast path, per-observer caching)
- [ ] Rewire discovery, witnessing, and stale-memory reconciliation through visibility
- [ ] Structure-as-observable discovery + window transparency

---

### Navigation & Pathfinding
**Spec/Documentation:** `/docs/technical/pathfinding-architecture.md`
**Dependencies:** Geometry Foundations (shared integer-coordinate substrate, libs/geometry); consumes the construction obstacle/portal contract
**Status:** planned (spec in review)

**Goal:** Four-tier vector-native navigation: planet hex graph for cross-globe abstract parties, chunk connectivity components for reachability, dynamic CDT navmesh for exact local paths, collision circles + velocity-obstacle steering so agents take up space. Must land (through P4) before walls ship as player-facing gameplay; P6 unlocks raids.

**Tasks:**
- [ ] P1: Agents become physical (radius component, dynamic spatial hash, circle collision, separation)
- [ ] P2: Local navmesh, static world (per-chunk CDT from terrain contours, triangle A* + corridor width + funnel, waypoint following; chunk anchoring fix)
- [ ] P3: Regional layer (components, traversal-class reachability API, RRA* heuristic)
- [ ] P4: Dynamic world + belief (construction obstacles/portals, door permission costs, memory-filtered planning, discovery replans, search primitives; requires Vision System: Occlusion & Discovery)
- [ ] P5: Crowds (velocity-obstacle avoidance + mitigations, occupancy costs, door slot queues, regression rig)
- [ ] P6: Global tier + raids (hex-graph A*, abstract party records, attention bubbles, materialization handoffs, raider belief seeding + scouting)

---

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
