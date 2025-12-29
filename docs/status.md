# Project Status

Last Updated: 2025-12-28 (Colonist Details Dialog implementation)

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

### Main Game UI: Colonist Details Dialog
**Spec/Documentation:** `/docs/design/main-game-ui-design.md` (Section 8), `.claude/plans/colonist-details-dialog.md`
**Dependencies:** ~~Main Game UI: Interaction Components~~ (complete), ~~Main Game UI: Primitives Foundation~~ (complete), ~~UI Architecture: ViewModel Pattern~~ (complete)
**Status:** in progress

**Goal:** Full colonist information display with 5 tabs, live updates while game runs.

**Tasks:**
- [x] Dialog Structure
  - [x] ColonistDetailsModel (extracts ECS data with change detection)
  - [x] ColonistDetailsDialog shell with Dialog + TabBar
  - [x] Wire to GameUI (showColonistDetails/hideColonistDetails)
  - [x] onDetails callback from EntityInfoView [Details] button
- [x] Bio Tab
  - [x] Name, age placeholder, traits placeholder, background placeholder
  - [x] Current mood with label
  - [x] Current task display
- [x] Health Tab
  - [x] All 8 needs as ProgressBars (Hunger, Thirst, Energy, Bladder, Social, Comfort, Fun, Hygiene)
  - [x] Mood summary
- [x] Social Tab
  - [x] Placeholder message (future implementation)
- [x] Gear Tab
  - [x] Inventory items from Inventory component
  - [x] Item type → display name mapping
- [x] Memory Tab
  - [x] TreeView with collapsible categories (Food, Water, Resources, Threats, Colonists)
  - [x] Entity counts per category
- [ ] Testing & Polish
  - [ ] Visual verification in game
  - [ ] Create PR

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
