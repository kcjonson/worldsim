# Project Status

Last Updated: 2025-10-29 (Ready for PR3: Style System)

## Current Sprint/Phase
Colonysim UI Integration - Porting production-quality UI systems to worldsim

## Active Tasks
- [x] Define project structure and documentation system
- [x] Set up build system (CMake + vcpkg)
- [x] Create library skeleton structure
- [x] Implement basic application scaffolbing
- [x] Begin vector graphics system implementation (star validation complete, grass validation in progress)
- [ ] **Colonysim UI Integration** - 9 PRs planned (see detailed breakdown below)
  - [x] PR 1: Compatibility Analysis & Planning ❌ SKIPPED (determined unnecessary)
  - [x] PR 2: Font Rendering System ✅ COMPLETE
  - [ ] PR 3: Style System ⏳ NEXT
  - [ ] PR 4: Rendering Integration Decision
  - [ ] PR 5: Layer System
  - [ ] PR 6: Shape System
  - [ ] PR 7: Button Component
  - [ ] PR 8: TextInput Component
  - [ ] PR 9: Polish & Integration

## Recent Decisions

### Documentation & Organization
- 2025-10-12 - Tech docs instead of ADRs, no numbering (topic-based organization)
- 2025-10-12 - Created workflows.md for common tasks (separate from CLAUDE.md)
- 2025-10-12 - CLAUDE.md streamlined to ~124 lines (was 312) - navigation guide only

### C++ Standards & Tools
- 2025-10-12 - **Naming**: PascalCase for classes/functions, camelCase for variables, m_ prefix for members, k prefix for constants
- 2025-10-12 - **Header guards**: `#pragma once` (not traditional guards)
- 2025-10-12 - **File organization**: Headers (.h) and implementation (.cpp) side-by-side in same directory
- 2025-10-12 - **Linting**: clang-format (manual formatting) + clang-tidy (automatic analysis)
- 2025-10-12 - Using user's existing .clang-format config (tabs, 140 column limit)

### Architecture Decisions
- 2025-10-12 - **Vector-based assets (SVG)**: All game assets use SVG format with dynamic rasterization
- 2025-10-12 - Using nested library structure with clear dependency hierarchy (6 core libraries)
- 2025-10-12 - Using "scenes" terminology (game dev standard), not "screens"
- 2025-10-12 - UI testability as a core requirement with inspector system
- 2025-10-12 - **Custom ECS** implementation in engine (not external library like EnTT)
- 2025-10-12 - **Roll our own implementations** for core systems (no external libs except platform/format support)
- 2025-10-24 - **Client/Server Architecture from Day One**: Two-process design (world-sim + world-sim-server) even for single-player
- 2025-10-24 - **Server spawns on-demand**: Only starts when player begins/loads game, not during main menu
- 2025-10-24 - **HTTP + WebSocket protocol**: HTTP for control plane (create/load game), WebSocket for real-time gameplay (60 Hz)
- 2025-10-24 - **HTTP Debug Server**: Separate debugging system (port 8080) using Server-Sent Events for real-time metrics
- 2025-10-24 - **World object taxonomy**: Terrain (base), Flora (vegetation), Structures (buildings/ruins), Entities (dynamic)
- 2025-10-24 - **Batch updates for mass events**: Area mutations (plagues, fires) use efficient batch messaging instead of individual events
- 2025-10-24 - **cpp-httplib** for networking: Header-only library for both game server and debug server
- 2025-10-26 - **Procedural Tile Rendering**: Tiles are code-generated (not SVG-based), with procedural ground covers (grass, sand, rock, water, etc.)
- 2025-10-26 - **Biome Influence Percentage System**: Tiles have multiple biome influences (e.g., `{ meadow: 80%, forest: 20% }`), creating natural ecotones hundreds of tiles deep
- 2025-10-26 - **SVG Asset Categorization**: Three distinct uses: (1) Decorations/Entities (placed objects like flowers, trees), (2) Texture Patterns (fills for code-drawn shapes like brick walls), (3) Animated Vegetation (spline deformation)
- 2025-10-26 - **Deterministic Procedural Variation**: Same world position always generates same appearance (seed-based), ensuring consistency and multiplayer compatibility
- 2025-10-26 - **Ground Covers vs Biomes**: Ground covers are physical surface types (permanent), biomes determine which covers appear and spawn decorations
- 2025-10-26 - **Seasonal Overlays**: Snow is not a ground cover but a seasonal overlay system (0-100% coverage on top of existing ground)
- 2025-10-26 - **1:1 Pixel Mapping for UI**: Primitive rendering uses framebuffer dimensions for pixel-perfect rendering - `Rect(50, 50, 200, 100)` is always exactly 200×100 pixels, matching RmlUI/ImGui industry standards
- 2025-10-26 - **Logging Macro Naming**: Use unprefixed global macros (`LOG_ERROR` not `WSIM_LOG_ERROR`) for brevity and developer experience, accepting potential library conflict risk
- 2025-10-27 - **Client-Side History Aggregation (Developer Client)**: Server streams current values only (stateless), client maintains all history with configurable retention (metrics: time-based, logs: count-based), localStorage persistence with automatic cleanup

### Engine Patterns to Implement
- 2025-10-27 - **Resource handles** (32-bit IDs with generation) - ✅ **IMPLEMENTED** (PR #4)
- 2025-10-27 - **Memory arenas** (linear allocators) - ✅ **IMPLEMENTED** (PR #3)
- 2025-10-27 - **String hashing** (FNV-1a, compile-time) - ✅ **IMPLEMENTED**
- 2025-10-26 - **Structured logging** (categories + levels) - ✅ **IMPLEMENTED**
- 2025-10-12 - **Immediate mode debug rendering** - Implement Later

### Colonysim UI Integration
- 2025-10-29 - **Comprehensive analysis of colonysim codebase** - Identified valuable UI systems for integration
- 2025-10-29 - **Singleton architecture confirmed** - Keep singletons for core rendering systems (industry best practice, best performance)
- 2025-10-29 - **9-PR integration plan created** - Structured approach: analysis → foundations → rendering → layers → shapes → components → polish
- 2025-10-29 - **Rendering integration strategy** - TBD after compatibility analysis (Option A: Adopt VectorGraphics, Option B: Adapt to Primitives)
- 2025-10-29 - **Systems to port identified**: Font rendering (~350 lines), Layer/Container (~350 lines), Styles (~200 lines), Shapes (~400 lines), Components (~1,400 lines)

## Blockers & Issues
None currently

## Next Steps

### Current Focus: Colonysim UI Integration
Port valuable UI systems from colonysim project to worldsim. See detailed task breakdown below.

### Future Work
1. ✅ ~~Complete project foundation (CMake, vcpkg, VSCode config)~~ - DONE
2. ✅ ~~Implement core engine patterns~~ - DONE (string hashing, logging, memory arenas, resource handles)
3. ✅ ~~Begin vector graphics validation plan~~ - Star phases (0-3) complete, grass blade phases (4-7) in progress
   - See `/docs/technical/vector-graphics/validation-plan.md` for detailed progression
4. ✅ ~~Implement primitive rendering API with batching~~ - DONE (basic implementation, needs enhancement)
5. Prototype SVG asset loading and tessellation
6. Implement UI inspector/testability infrastructure
7. Begin splash screen implementation for world-sim app

## Colonysim UI Integration Plan

**Goal**: Port production-quality UI systems from `/Volumes/Code/colonysim` to worldsim
- Font rendering (FreeType-based, ~350 lines)
- Layer/Container hierarchy (scene graph, ~350 lines)
- Style system (composition-based, ~200 lines)
- Shape system (Rectangle/Circle/Line/Polygon/Text, ~400 lines)
- UI components (Button + TextInput, ~1,400 lines)

**Total Estimated Lines**: ~2,700 lines of production C++20 code
**Estimated Timeline**: 24-33 hours over multiple PRs

### PR 1: Compatibility Analysis & Planning ❌ SKIPPED
**Status**: Determined unnecessary - PR 2 proved direct porting is viable
**Rationale**: The successful completion of PR 2 (Font Rendering System) demonstrated that colonysim code can be ported directly to worldsim with minimal adaptations (GLAD→GLEW, logging system updates). Both projects use compatible coordinate systems, OpenGL 3.3+, and similar architectural patterns. A formal compatibility analysis became unnecessary after this empirical validation.
**Architectural Decision Made**: Direct porting with minor adaptations (no major rendering backend replacement needed)

### PR 2: Font Rendering System ✅ COMPLETE
**Branch**: `feature/font-rendering-system` (merged as PR #10)
**Status**: Successfully ported and merged
**Completed Tasks**:
- [x] Add FreeType to vcpkg.json dependencies
- [x] Port FontRenderer.h/cpp (~350 lines) to `libs/ui/font/`
- [x] Port text.vert/frag shaders to `shaders/`
- [x] Copy Roboto-Regular.ttf to `fonts/`
- [x] Port Shader wrapper class to `libs/renderer/shader/`
- [x] Create font rendering demo scene in ui-sandbox
- [x] Test: Render "Hello World" at various sizes and colors
**Deliverable**: Working text rendering in ui-sandbox ✅
**Time Taken**: ~3 hours

### PR 3: Style System ⏳ NEXT
**Branch**: `feature/colonysim-styles` (to be created)
**Objective**: Port composition-based style system from colonysim
**Tasks**:
- [ ] Port Base style class to `libs/renderer/styles/`
- [ ] Port Border style class
- [ ] Port concrete style classes (Rectangle, Circle, Line, Text, Polygon)
- [ ] Port all style parameter structs
- [ ] Create style demo scene showing various styles
- [ ] Test: Create shapes with different styles (colors, borders, rounded corners)
**Deliverable**: Complete style system with demo
**Estimated Time**: 2-3 hours

### PR 4: Rendering Integration Decision
**Branch**: `feature/colonysim-rendering-integration`
**Objective**: Integrate or replace BatchRenderer with VectorGraphics
**Decision Point**: Based on PR 1 findings
**Option A - Adopt VectorGraphics**:
- [ ] Port VectorGraphics.h/cpp to `libs/renderer/vector/`
- [ ] Replace BatchRenderer with VectorGraphics
- [ ] Adapt Primitives API to call VectorGraphics
- [ ] Test: Verify all existing scenes still work
- [ ] Test: Verify scissor/batching improvements
**Option B - Adapt to Primitives**:
- [ ] Keep BatchRenderer as-is
- [ ] Adapt colonysim Layer/Shape code to use Primitives API
- [ ] Add missing features to Primitives (scissor, text)
**Deliverable**: Unified rendering backend
**Estimated Time**: 4-6 hours

### PR 5: Layer System
**Branch**: `feature/colonysim-layers`
**Objective**: Port scene graph hierarchy system
**Tasks**:
- [ ] Port CoordinateSystem utilities to `libs/renderer/coordinate/`
- [ ] Port Layer.h/cpp to `libs/engine/ui/`
- [ ] Implement parent-child hierarchy management
- [ ] Implement z-index ordering with dirty flags
- [ ] Implement WorldSpace/ScreenSpace projection types
- [ ] Implement update/input propagation
- [ ] Create layer hierarchy demo (nested layers, z-ordering)
- [ ] Test: Nested layers render in correct order
- [ ] Test: Input events propagate correctly
**Deliverable**: Working scene graph system
**Estimated Time**: 3-4 hours

### PR 6: Shape System
**Branch**: `feature/colonysim-shapes`
**Objective**: Port basic drawing primitives that extend Layer
**Dependencies**: PR 3 (Styles), PR 4 (Rendering), PR 5 (Layers)
**Tasks**:
- [ ] Port Shape base class to `libs/engine/ui/shapes/`
- [ ] Port Rectangle shape with rounded corners/borders
- [ ] Port Circle shape
- [ ] Port Line shape
- [ ] Port Polygon shape
- [ ] Port Text shape (uses FontRenderer from PR 2)
- [ ] Create shapes demo showing all types
- [ ] Test: All shape types render correctly
- [ ] Test: Shapes nested in layers work
**Deliverable**: Complete shape system
**Estimated Time**: 4-5 hours

### PR 7: Button Component
**Branch**: `feature/colonysim-button`
**Objective**: Port interactive button component
**Dependencies**: PR 5 (Layers), PR 6 (Shapes)
**Tasks**:
- [ ] Port Button.h/cpp to `libs/engine/ui/components/`
- [ ] Implement state machine (normal/hover/pressed)
- [ ] Implement type-based styling (Primary/Secondary)
- [ ] Implement onClick callbacks
- [ ] Create button demo with multiple button types
- [ ] Test: Button responds to mouse hover
- [ ] Test: Button responds to mouse click
- [ ] Test: onClick callback fires correctly
- [ ] Test: Disabled state works
**Deliverable**: Working button component
**Estimated Time**: 3-4 hours

### PR 8: TextInput Component
**Branch**: `feature/colonysim-textinput`
**Objective**: Port text input field component
**Dependencies**: PR 5 (Layers), PR 6 (Shapes)
**Tasks**:
- [ ] Port Form/Text.h/cpp to `libs/engine/ui/components/`
- [ ] Implement focus management (global focus tracking)
- [ ] Implement cursor positioning and blinking animation
- [ ] Implement text scrolling for overflow
- [ ] Implement placeholder support
- [ ] Implement onChange callbacks
- [ ] Create text input demo
- [ ] Test: Text input accepts keyboard input
- [ ] Test: Cursor moves with arrow keys
- [ ] Test: Focus switching between multiple inputs
- [ ] Test: Text scrolls when overflow
- [ ] Test: onChange callback fires correctly
**Deliverable**: Working text input component
**Estimated Time**: 4-5 hours

### PR 9: Polish & Integration
**Branch**: `feature/colonysim-polish`
**Objective**: Optimize, document, and finalize integration
**Tasks**:
- [ ] Integrate memory arenas for temporary allocations
- [ ] Profile draw calls using debug server
- [ ] Optimize batching if needed
- [ ] Complete transform stack implementation (if not done)
- [ ] Abstract input handling (remove direct GLFW dependencies)
- [ ] Create comprehensive UI demo scene (buttons, inputs, nested layers)
- [ ] Update CLAUDE.md with UI component usage examples
- [ ] Update `/docs/technical/INDEX.md` with UI system docs
- [ ] Write development log entry in status.md
- [ ] Performance validation: <100 draw calls, 60 FPS
**Deliverable**: Production-ready UI system
**Estimated Time**: 3-4 hours

## Architecture Decision: Singletons vs DI

**Decision**: Keep singletons for rendering (industry best practice for game engines)

**Rationale**:
- **Performance**: Singletons = zero indirection, one static pointer dereference
- **Industry Standard**: Unreal, Unity, id Tech all use singletons for core systems
- **Cache Friendly**: Fixed memory location, better CPU prediction
- **No Parameter Overhead**: Don't need to pass renderer to every function
- **Best Practice**: Core systems (Renderer, Audio, Physics, Input) use singletons
- **Flexibility Where Needed**: Gameplay systems can use DI or ECS patterns

Colonysim's singleton architecture is correct for game engine performance.

## Architecture Decision: Rendering Integration Strategy

**Decision**: TBD after PR 1 compatibility analysis

**Option A (Recommended)**: Adopt VectorGraphics
- Use colonysim's VectorGraphics as implementation behind worldsim's Primitives API
- Get mature batching + text rendering + scissor support
- Keep worldsim's clean API (scenes already use it)
- Minimal refactoring of existing worldsim code

**Option B**: Adapt to Primitives API
- Keep worldsim's BatchRenderer
- Port colonysim code to call Primitives API
- More work, but keeps worldsim's architecture pure

**Evaluation Criteria**:
- Coordinate system compatibility
- State management complexity
- Performance characteristics
- Code maintainability

