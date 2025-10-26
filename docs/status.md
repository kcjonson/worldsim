# Project Status

Last Updated: 2025-10-26 (OpenGL + RmlUI Implementation Guide)

## Current Sprint/Phase
Initial project setup and architecture

## Active Tasks
- [x] Define project structure and documentation system
- [ ] Set up build system (CMake + vcpkg)
- [ ] Create library skeleton structure
- [ ] Implement basic application scaffolding

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

### Engine Patterns to Implement
- 2025-10-12 - **String hashing** (FNV-1a, compile-time) - Implement Now
- 2025-10-12 - **Structured logging** (categories + levels) - Implement Now
- 2025-10-12 - **Memory arenas** (linear allocators) - Implement Soon
- 2025-10-12 - **Resource handles** (32-bit IDs with generation) - Implement Soon
- 2025-10-12 - **Immediate mode debug rendering** - Implement Later

## Blockers & Issues
None currently

## Next Steps
1. Complete project foundation (CMake, vcpkg, VSCode config)
2. Implement core engine patterns:
   - String hashing system (foundation)
   - Structured logging system (foundation)
   - Memory arenas (foundation)
   - Resource handle system (renderer)
3. Create skeleton for all libraries with basic headers
4. Set up ui-sandbox application with CLI argument support
5. Implement UI inspector/testability infrastructure
6. Begin splash screen implementation for world-sim app

## Development Log

### 2025-10-26 - UI Framework Architecture - Complete Design

**Critical Gap Addressed:**

Comprehensive planning exists for vector graphics rendering and procedural tile systems, but **no technical design for UI framework** - a foundational requirement for ui-sandbox and complex game UI (inventory grids, skill trees, resource management).

**Production game context established:** This is not a learning project - need production-quality UI for complex management game with thousands of dynamic UI elements.

**User Context:**
- Web/React developer background (familiar with declarative UI)
- Previous C++ project used modern designated initializer syntax (`.position = value`)
- Hit scrolling container issues (performance/clipping in OpenGL)
- Wants crisp text rendering matching vector aesthetic

**Documentation Created** (`/docs/technical/ui-framework/`):

**Seven comprehensive design documents:**

1. **INDEX.md** - Navigation hub for UI framework documentation
   - Desired API style examples (CSS-like `.Args{}` syntax)
   - Integration with observability system
   - Common UI patterns (menus, forms, HUD)

2. **ui-architecture-fundamentals.md** - Foundational concepts for production games
   - Immediate vs retained mode explained in React/web terms
   - Scene graphs = React's virtual DOM
   - What production games actually use (Unity, Unreal, indies)
   - RmlUI as HTML/CSS for C++ games
   - Clear recommendation: Retained mode for complex game UI

3. **library-options.md** - Initial comparative analysis (6 libraries)
   - Later superseded by fundamentals doc with production focus

4. **integration-analysis.md** - Technical integration deep dive
   - Immediate vs retained mode in React terms
   - OpenGL rendering models (who draws what)
   - Transitive dependencies for each library
   - Rendering conflict analysis (NanoVG issues)
   - Backend implementation examples
   - Dependency table (RmlUI: FreeType, NanoGUI: NanoVG+Eigen)

5. **library-isolation-strategy.md** - Risk mitigation and architecture isolation
   - Interface pattern to isolate RmlUI from game code
   - What CAN be isolated (game logic, event handlers)
   - What CANNOT be isolated (.rml files, data binding syntax)
   - Hidden constraints (threading, memory management, font rendering)
   - Conflicts with project goals analysis
   - Migration cost assessment (1-2 weeks simple, 4-8 weeks complex)
   - Prototype-first recommendation

6. **rendering-boundaries.md** - Critical architectural boundaries
   - **The "two different APIs" problem identified**
   - RmlUI for screen-space panels only (menus, inventory, skill trees)
   - Custom rendering for game world (tiles, entities)
   - Custom rendering for world-space UI (health bars, tooltips)
   - **Solution: Unified Primitive Rendering API**
   - Complete render loop architecture
   - Real-world examples (RimWorld, Factorio, Unity, Unreal)

7. **primitive-rendering-api.md** - Foundation layer design
   - Unified API for all 2D drawing (DrawRect, DrawText, DrawTexture)
   - Used by RmlUI backend, game rendering, world-space UI
   - Immediate mode API, retained mode implementation (batching)
   - Scissor/clipping stack for scrollable containers
   - Transform stack for world-space rendering
   - Integration with existing renderer and vector graphics
   - Performance targets (<1ms per frame for all primitives)
   - Complete code examples (health bars, tooltips, minimap)

8. **rmlui-integration-architecture.md** - Complete integration design
   - Multi-layer architecture (game → interface → adapter → RmlUI → primitives)
   - IUISystem, IDocument, IElement abstract interfaces
   - RmlUI adapter hidden in .cpp files (game never sees RmlUI headers)
   - Renderer backend (RmlUI geometry → Primitives API)
   - Three usage patterns: XML, programmatic, hybrid
   - Complete game loop integration example
   - CMake configuration with private RmlUI linking
   - Testing strategy (unit tests with mock, integration tests with real)
   - Migration strategy (game code unchanged, rewrite adapter only)

**Key Architectural Decisions:**

**1. RmlUI for Screen-Space Complex UI**
- HTML/CSS-like markup for complex layouts (inventory grids, skill trees)
- Flexbox layout engine (handles nested containers automatically)
- Can be used programmatically (NO .rml files required)
- Isolated via interface layer (game code never depends on RmlUI)

**2. Unified Primitive Rendering API**
- **Solves "two different APIs for rectangles" problem**
- One DrawRect/DrawText/DrawTexture API used by everything:
  - RmlUI backend
  - Game world rendering
  - World-space UI (health bars)
  - Custom UI components
- Batching implementation (minimize draw calls)
- Integration with existing renderer

**3. Clear Rendering Boundaries**
- **RmlUI**: Screen-space panels only (menus, inventory, dialogs)
- **Custom/Primitives**: Game world + world-space UI + simple HUD
- **Vector Graphics**: Complex SVG entities (trees, decorations)
- No overlap or confusion about which system to use

**4. Isolation Strategy**
- Game code depends on IUISystem/IDocument/IElement interfaces
- RmlUI adapter hidden in private .cpp files
- CMake links RmlUI privately (not exposed to game)
- Migration cost: Rewrite adapter (~500 lines), game code unchanged

**Critical Insights:**

**Production Game Requirements Clarified:**
- Not a learning project or toy
- Complex management game UI (thousands of elements)
- Inventory grids, skill trees, resource management panels
- Performance critical (60 FPS with dynamic updates)

**RmlUI Programmatic API Discovery:**
- Can create all UI from C++ (NO .rml files required)
- Avoids markup file lock-in
- Enables `.Args{}` wrapper pattern
- Still get flexbox layout engine benefits

**Text Rendering:**
- RmlUI requires FreeType (can't avoid)
- Can write custom font backend using msdfgen for SDF fonts
- Integration point with vector graphics system

**Scrolling Containers:**
- OpenGL scissor test (hardware clipping)
- RmlUI handles automatically
- Primitives API exposes PushScissor/PopScissor for custom usage

**Next Steps:**

1. ✅ **Complete architecture documentation** (DONE)
2. **Prototype RmlUI integration** (1-2 weeks)
   - Implement primitive rendering API
   - Build RmlUI backend + isolation layer
   - Create one complex UI panel (inventory or skill tree)
   - Validate performance and integration
3. **Evaluate prototype results**
   - Does flexbox handle layouts?
   - Is performance acceptable?
   - Are constraints manageable?
4. **Make final decision**
   - Commit to RmlUI if prototype succeeds
   - Build custom if critical issues found

**Documentation Organization:**
- Created `/docs/technical/ui-framework/` subdirectory
- Updated `/docs/technical/INDEX.md` with UI Framework section
- All documents cross-referenced and navigable

### 2025-10-26 - OpenGL + RmlUI Implementation Guide - Production-Ready Rendering Layer

**Critical Missing Piece:**

Architecture was complete, but **no implementation guidance** for actually building the OpenGL rendering layer with RmlUI integration. User asked: "How should we set up our rendering layer according to RmlUI best practices? Do they have a guide for a production ready game for the OpenGL setup?"

**Research Conducted:**

Comprehensive research of RmlUI's official documentation, reference implementations, and production best practices:

1. **RmlUI Official Documentation** (mikke89.github.io/RmlUiDoc)
   - RenderInterface specification (`<RmlUi/Core/RenderInterface.h>`)
   - Integration guide and main loop patterns
   - Troubleshooting and common mistakes
   - Rendering conventions and coordinate system requirements

2. **GL3 Reference Backend Analysis** (`RmlUi_Renderer_GL3.cpp`)
   - Complete class structure and implementation patterns
   - Vertex buffer management (VAO/VBO/IBO)
   - Shader architecture (9 programs for various effects)
   - State backup/restore implementation
   - Scissor region handling with Y-flip
   - Transform dirty flag pattern

3. **Production Integration Patterns**
   - Main loop order (Input → Update → Render)
   - Initialization sequence (interfaces → initialize → context → fonts → documents)
   - State management requirements
   - Performance profiling approaches

**Documentation Created:**

**opengl-rmlui-implementation-guide.md** - Comprehensive production-ready implementation guide (~2000 lines)

**10 Major Sections:**

1. **OpenGL Rendering Architecture**
   - Complete render pipeline (world → world UI → screen UI)
   - Three-layer architecture (RmlUI → Backend → Primitives → OpenGL)
   - Batching strategy decision (where to batch, how it works)

2. **Coordinate System Conventions**
   - RmlUI vs OpenGL origin mismatch (top-left vs bottom-left)
   - Y-axis flipping for projection matrix
   - Scissor region Y-coordinate transformation
   - Color space (sRGB with premultiplied alpha)
   - Blend function: `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`
   - Face culling (CCW winding)

3. **RmlUI Backend Implementation**
   - Complete `RmlUIBackend` class implementing `Rml::RenderInterface`
   - Geometry compilation strategy (immediate GPU upload with VAO/VBO/IBO)
   - `CompileGeometry()` implementation with vertex attributes
   - `RenderGeometry()` options (direct OpenGL vs Primitive API)
   - Texture management (`LoadTexture()` and `GenerateTexture()`)
   - Scissor implementation with Y-flip
   - Transform support with dirty flags

4. **Primitive API OpenGL Implementation**
   - `BatchAccumulator` class design
   - Batching by texture/state/transform/scissor
   - Flush triggers (state changes, max batch size, frame end)
   - Vertex buffer strategy (dynamic upload, capacity management)
   - Draw call accumulation and rendering

5. **Integration Patterns**
   - **Critical main loop order** (violations cause crashes/glitches)
   - Initialization sequence (order matters!)
   - Shutdown sequence (backend must outlive RmlUI)
   - Input injection (RmlUI doesn't convert keys to text!)
   - Viewport handling on resize

6. **State Management** - **CRITICAL**
   - Why state backup/restore is mandatory
   - Complete `GLState` structure (20+ state variables)
   - `BackupGLState()` implementation
   - `RestoreGLState()` implementation
   - `BeginFrame()` / `EndFrame()` lifecycle

7. **Production Best Practices**
   - Always backup/restore state (non-negotiable)
   - Preserve render order (never batch/reorder RmlUI calls)
   - Handle viewport changes correctly
   - Use visual testing suite
   - Integrate RmlUI debugger
   - Profile early and often
   - Load fonts before documents
   - Handle interface lifetimes correctly
   - Implement high-resolution timer
   - Copy reference backend, don't link it

8. **Common Pitfalls** (10 documented issues)
   - Face culling blank screen (DirectX vs OpenGL winding)
   - Upside-down UI (forgot Y-flip)
   - Scissor regions incorrect (forgot Y-flip)
   - Crash on shutdown (destroyed backend too early)
   - Text not rendering (fonts not loaded)
   - Slow animations (low-resolution timer)
   - State corruption (no backup/restore)
   - Modifying elements after Update (causes crashes)
   - Incorrect initialization order
   - Premultiplied alpha confusion

9. **Performance Considerations**
   - Expected performance (reference vs batched)
   - Profiling strategy (separate Update vs Render time)
   - Optimization targets (< 2ms for complex UI)
   - Batching effectiveness measurement
   - Memory usage expectations

10. **Testing and Validation**
    - RmlUI visual test suite usage
    - Integration testing examples
    - Debug visualization
    - Logging and diagnostics

**Key Technical Discoveries:**

**RmlUI Reference Backend Does NOT Batch:**
- Each `RenderGeometry()` call = one draw call
- Prioritizes correctness and transform flexibility
- Simple, battle-tested approach
- **Our architecture batches at Primitive API layer below**

**Critical Coordinate System Details:**
```cpp
// Projection matrix MUST flip Y-axis
glm::ortho(0, width, height, 0, -1, 1);  // Note: bottom/top swapped

// Scissor regions need Y-flip
int flippedY = viewportHeight - region.p1.y;
glScissor(region.p0.x, flippedY, width, height);
```

**State Management is Non-Negotiable:**
- Must backup/restore 20+ OpenGL state variables
- Reference backend does comprehensive state preservation
- Failure causes subtle rendering corruption bugs
- `BeginFrame()` → backup + setup, `EndFrame()` → restore

**Main Loop Order is Critical:**
```cpp
// CORRECT order (violations cause crashes/glitches):
ProcessInput();           // 1. Input BEFORE update
UpdateUIDataBindings();   // 2. Modify elements
context->Update();        // 3. Update (resolves layout)
// 4. NEVER modify elements here!
context->Render();        // 5. Render
```

**Initialization Order Matters:**
```cpp
// CORRECT sequence:
Rml::SetRenderInterface(backend);   // 1. Set interfaces
Rml::SetSystemInterface(&system);
Rml::Initialise();                  // 2. Initialize
CreateContext();                    // 3. Create context
Rml::LoadFontFace("font.ttf");      // 4. Load fonts
context->LoadDocument("menu.rml");  // 5. Load documents
```

**Batching Strategy Decision:**
- **Reference backend**: No batching (immediate rendering)
- **Our architecture**: Batch at Primitive API layer
- RmlUI backend uploads geometry to GPU (like reference)
- But calls Primitive API instead of raw OpenGL
- Primitive API accumulates and batches draw calls
- **Result**: Battle-tested geometry compilation + performance batching

**Performance Targets:**
- Simple UI: < 0.5ms per frame
- Complex UI (inventory grid): < 2ms per frame
- Expected batching ratio: 5-10x (1000 calls → 100 draw calls)

**Production Patterns from Reference Backend:**
- `GL_STATIC_DRAW` for vertex/index data (RmlUI guarantees immutability)
- Comprehensive state backup (blend, depth, stencil, culling, scissor, viewport, bindings)
- Y-flip transformation for scissor regions
- Clamping scissor to viewport (prevents WebGL validation errors)
- Dirty flag pattern for transform matrices
- Per-program uniform location caching

**Documentation Updated:**
- Updated `INDEX.md` status to "Implementation Phase: Architecture complete"
- Added implementation guide to "Implementation Guides" section
- Updated "Next Steps" with concrete implementation tasks
- Added revision history entry

**Next Steps:**

Architecture and implementation guide complete. Ready to begin coding:

1. **Implement Primitive Rendering API** (`libs/renderer/`)
   - BatchAccumulator for geometry batching
   - OpenGL shader setup (vertex + fragment shaders)
   - State management and batching triggers

2. **Implement RmlUI Backend** (`libs/ui/src/rmlui/`)
   - RmlUIBackend class implementing Rml::RenderInterface
   - Geometry compilation (VAO/VBO/IBO)
   - Texture loading and management
   - State backup/restore (critical!)

3. **Integration Testing**
   - Load simple RML document (colored rectangles)
   - Test scissor regions (scrolling containers)
   - Test transforms (CSS transforms)
   - Validate against RmlUI visual test suite

### 2025-10-26 - Procedural Tile System Game Design Documentation

**Major Architectural Clarification:**

Comprehensive game design documentation for the procedural tile rendering system. **Critical discovery**: Tiles are procedurally generated in code (not SVG-based), with SVG assets serving as decorations and entities placed on top of the procedural ground.

**Core Innovation - Biome Influence Percentage System:**

Tiles don't have a single "type" but instead have percentage influences from multiple biomes:
```
Example: { meadow: 80%, forest: 20% }
- Ground: 80% grass appearance, 20% forest floor appearance
- Entities: 80% wildflower spawn probability, 20% pine tree spawn probability
- Movement speed: Interpolated between meadow and forest values
```

This creates **natural ecotones** (transition zones) hundreds of tiles deep between biomes, with gradual species mixing and visual blending. Every forest-meadow edge looks different, every coastline is unique.

**Game Design Documentation Created** (7 documents):

**Core Tile System** (`/docs/design/`):
1. `visual-style.md` - Overall aesthetic direction: hand-crafted feel, recognizable biomes, organic appearance
2. `biome-ground-covers.md` - Seven ground cover types (grass, forest floor, dirt, sand, rock, wetland, water)
3. `biome-influence-system.md` - Percentage-based biome blending, natural ecotone creation
4. `tile-transitions.md` - Visual appearance of biome transition zones (complete rewrite for percentage system)
5. `procedural-variation.md` - Creating unique tiles while maintaining biome recognizability

**SVG Asset Organization** (`/docs/design/features/vector-graphics/`):
6. `svg-decorations.md` - SVG assets as placed objects (flowers, trees, boulders)
7. `svg-texture-patterns.md` - SVG patterns as fills for code-drawn shapes (brick, wood, concrete)

**Documentation Index Updates**:
- Updated `/docs/design/INDEX.md` with "Visual Design & Procedural Tile System" section
- Updated vector graphics section with organized SVG documentation links

**Three Distinct SVG Use Cases Identified:**

1. **Decorations/Entities**: Placed objects on top of procedural tiles
   - Ground decorations: flowers, pebbles, grass tufts (5-10 per tile)
   - Standing entities: trees, bushes, boulders (0-3 per tile)
   - Placement follows biome influence percentages
   - Biome-appropriate spawning (meadow flowers only in meadow-influenced tiles)

2. **Texture Patterns**: Seamlessly tiling fills for code-drawn shapes
   - Building materials: brick, wood planks, concrete, thatch
   - Terrain features: cobblestone paths, rock formations
   - Any shape/size polygon filled with hand-crafted texture
   - Pattern must tile perfectly (edges match)

3. **Animated Vegetation**: Spline-based deformation (documented previously in animated-vegetation.md)
   - Grass swaying, tree movement, player interaction
   - Real-time Bezier curve manipulation

**Key Terminology Clarifications:**

- **Ground Covers**: Physical surface types (grass, sand, rock) - permanent, code-generated
- **Biomes**: Ecological zones that determine which ground covers appear and which decorations spawn
- **Tiles**: Individual game world cells with multiple biome influence percentages
- **Ecotones**: Natural transition zones between biomes (emergent from percentage system)
- **Decorations**: Small SVG objects (flowers, pebbles)
- **Entities**: Larger SVG objects (trees, boulders, creatures)
- **Texture Patterns**: Seamlessly tiling SVG patterns for filling shapes

**Visual Rendering Layers** (bottom to top):
1. Procedural ground covers (code-generated, biome-blended)
2. SVG texture pattern fills (code-drawn shapes with SVG fills)
3. Ground SVG decorations (flowers, pebbles)
4. Standing SVG entities (trees, structures)
5. Animated SVG elements (swaying grass, trees)

**Deterministic Variation System:**

- Each tile gets deterministic seed from world position (x, y)
- Same position always generates same appearance
- Color variation within biome palette (±10-20% lightness/saturation)
- Entity placement varies (rotation, scale, position)
- Balance: recognizable biomes with endless visual variety
- Multiplayer compatible (all clients see same world)

**Design Philosophy Established:**

- **Hand-crafted illusion**: Player should think "someone carefully designed this", not "obviously procedural"
- **Recognizable not readable**: Biomes must be instantly identifiable by players
- **Variation within coherence**: Every tile unique, but biome identity maintained
- **Performance in technical docs**: Game design docs focus on player experience, not implementation
- **Separation of concerns**: Design docs describe "what/why", technical docs describe "how"

**Iterative Refinement Process:**

Multiple rounds of user feedback refined the documentation:
- Changed "readable" → "recognizable" (it's a game, not text)
- Removed performance concerns from design docs
- Removed prototyping plans from design docs
- Changed "terrain types" → "tile types" → "ground covers" (terminology evolution)
- Removed snow as ground cover (now seasonal overlay system)
- Removed code examples from design docs (belongs in technical docs)
- Split SVG documentation by use case (decorations vs patterns vs animation)

**Open Questions Documented** (for future technical design):

1. Color variation ranges (how much can grass green vary?)
2. Entity density variation (fixed or varied per tile?)
3. Scale variation ranges (trees 80%-120% of base?)
4. Cross-tile decoration continuity (coordinate or independent?)
5. Regional vs per-tile variation (macro trends or purely local?)
6. Pattern library size (how many texture patterns needed?)
7. LOD transition smoothness (fade decorations gracefully?)

**Next Steps:**

1. Create technical design documents for tile system implementation
2. Define edge blending algorithms (geometric, alpha, marching squares)
3. Specify procedural variation implementation (noise functions, seed-based generation)
4. Document terrain layer composition system
5. Specify SVG-tile integration architecture
6. Prototype procedural tile rendering in ui-sandbox

### 2025-10-24 - Vector Graphics System Research & Documentation

**Vector Graphics Rendering Strategy Complete:**

Comprehensive research and documentation phase for vector graphics rendering system. Analyzed approaches from multiple game engines (Godot, Unity, Bevy, Phaser, LibGDX) and created detailed comparative analysis of all key components.

**Documentation Created** (18 documents):

**Technical Documentation** (`/docs/technical/vector-graphics/`):
1. `INDEX.md` - Navigation hub for all vector graphics documentation
2. `architecture.md` - Four-tier rendering system (static, semi-static, dynamic, GPU compute)
3. `tessellation-options.md` - Comparative analysis: libtess2 vs Earcut vs Poly2Tri vs custom ear clipping
4. `svg-parsing-options.md` - Comparative analysis: NanoSVG vs LunaSVG vs PlutoVG vs custom parser
5. `rendering-backend-options.md` - Comparative analysis: NanoVG vs Blend2D vs custom batched renderer vs Vello
6. `batching-strategies.md` - GPU batching techniques, streaming VBOs, texture atlasing
7. `animation-system.md` - Spline-based deformation for grass/trees, wind simulation, trampling
8. `collision-shapes.md` - Dual representation (render geometry vs physics shapes)
9. `lod-system.md` - Level of detail strategies for zoom-based rendering
10. `memory-management.md` - Memory architecture across all tiers (~350 MB budget)
11. `performance-targets.md` - Performance budgets, profiling methodology (60 FPS @ 10k entities)
12. `asset-pipeline.md` - Moved from root, updated with comprehensive cross-references

**Game Design Documentation** (`/docs/design/features/vector-graphics/`):
13. `README.md` - Asset creation workflow for artists, SVG guidelines, procedural variation
14. `animated-vegetation.md` - Grass swaying, tree movement, player interaction behavior
15. `environmental-interactions.md` - Trampling mechanics, harvesting, wind effects

**Index Updates**:
16. Updated `/docs/technical/INDEX.md` with vector graphics section
17. Updated `/docs/design/INDEX.md` with vector graphics features

**Key Architectural Decisions**:

- **Four-Tier System**: Static backgrounds (pre-rasterized) → Semi-static structures (cached meshes) → Dynamic entities (real-time tessellation) → GPU compute (future)
- **Desktop-First**: OpenGL 3.3+ target, leverage full desktop GPU capabilities
- **CPU Tessellation Primary**: Proven approach (Godot, Unity, Phaser pattern), defer GPU compute to Tier 4
- **Custom Batched Renderer Recommended**: Best fit for 10,000+ dynamic entities @ 60 FPS
- **Hybrid Parsing**: NanoSVG (already in project) + custom metadata parsing for game data
- **Spline-Based Animation**: Real-time Bezier curve deformation for organic movement
- **Dual Geometry**: Separate render (complex) vs collision (simplified) shapes

**Comparative Analysis Completed**:
- Each component analyzed with 3+ library options plus "no library" custom implementation
- Objective pros/cons for all options
- Decision criteria frameworks (not decisions)
- Performance estimates and complexity assessments

**Performance Targets Defined**:
- 60 FPS with 10,000+ animated entities
- <2ms tessellation budget
- <100 draw calls per frame
- ~350 MB memory budget

**Next Steps**:
1. Begin prototyping in ui-sandbox (Phase 1: Basic tessellation + rendering)
2. Test tessellation options with real SVG assets
3. Validate performance assumptions
4. Make library selection decisions based on prototype results
5. Implement core rendering pipeline

### 2025-10-24 - Networking & Multiplayer Architecture

**Client/Server Architecture Finalized:**
- Two-process design from day one: `world-sim` (client) + `world-sim-server` (headless)
- Server spawns only when needed (new game/load game), not during main menu
- Client manages server lifecycle: spawn, health monitoring, graceful shutdown
- Process management handles crash detection, zombie prevention, cross-platform spawning

**Network Protocol Defined:**
- HTTP (REST) for control plane: create game, load game, chunk requests, health checks
- WebSocket for real-time gameplay: 60 Hz entity updates, player input, world events
- Hybrid approach: Control vs. data plane separation
- JSON messages initially, binary protocol (MessagePack/Protobuf) for future optimization

**Colony Sim Synchronization Strategy:**
- Four object types: Terrain (rarely changes), Flora (event-based), Structures (deltas), Entities (60 Hz)
- Batch updates for mass events: Plant plagues, fires, meteors use efficient area mutations
- Chunk-based world streaming: Client requests chunks as viewport moves, aggressive LRU caching
- SVG assets local to client, server only sends metadata (position, rotation, variant seed)

**HTTP Debug Server Specification:**
- Separate debugging system on port 8080 (independent of game server on port 9000)
- Server-Sent Events (SSE) for real-time metrics streaming at configurable rates (10 Hz default)
- Web-based debug app (TypeScript + Vite) with custom Canvas chart rendering
- Lock-free ring buffers for metrics collection (game thread writes, server thread reads)
- Rate-limited streams prevent bandwidth issues: metrics 10 Hz, logs 20 Hz throttled, profiler 5 Hz

**Technical Documentation Created:**
- `/docs/design/features/debug-server/README.md` - Debug server game design doc
- `/docs/design/features/multiplayer/README.md` - Multiplayer architecture game design doc
- `/docs/technical/http-debug-server.md` - Debug server implementation design
- `/docs/technical/multiplayer-architecture.md` - Client/server protocol and synchronization
- `/docs/technical/process-management.md` - Cross-platform process lifecycle management

**Key Technical Decisions:**
- **cpp-httplib** chosen for both game server and debug server (header-only, SSE support, simple integration)
- Server-Sent Events preferred over WebSocket for debug server (one-way streaming, simpler, auto-reconnect)
- WebSocket required for game server (bidirectional, low-latency gameplay)
- Process spawning: fork/exec on Unix, CreateProcess on Windows
- Health monitoring: HTTP polling every 5s + PID checks for crash detection

**Library Organization Clarified:**
- `game-systems` is SERVER ONLY (colony simulation logic, must not depend on renderer)
- `renderer` and `ui` are CLIENT ONLY (never linked by server)
- `world` is SHARED (procedural generation algorithms)
- `engine/shared`, `engine/client`, `engine/server` split for clear separation

**Next Session Should:**
1. Begin implementing process management (client spawning server)
2. Set up cpp-httplib in vcpkg.json
3. Create basic HTTP server skeleton in `world-sim-server`
4. Implement health check endpoint
5. Test cross-platform process spawning (macOS, Windows, Linux)

### 2025-10-12 - Initial Planning Session

**Documentation System Created:**
- Established three-tier docs: status.md, technical/, design/
- Created workflows.md for common development tasks
- Streamlined CLAUDE.md from 312→124 lines (navigation only, no duplication)
- Technical docs use topic-based organization (no ADR numbering)

**Project Architecture Defined:**
- Monorepo with 6 libraries in dependency layers: foundation → renderer → ui/world/game-systems → engine
- Headers and implementation side-by-side (not separate include/src dirs)
- Custom ECS in engine (roll our own, not EnTT)
- Philosophy: Build core systems ourselves, only use external libs for platform/formats

**Major Architectural Decision: Vector-Based Assets**
- All game assets are SVG files with dynamic rasterization
- Procedural variation per tile (color, scale, rotation)
- Inter-tile blending for seamless appearance
- Aggressive caching strategy (hybrid: pre-rasterize common + LRU for variants)
- LOD system (16/32/64/128px based on zoom)
- Created comprehensive tech doc: vector-asset-pipeline.md

**C++ Standards Established:**
- Naming: PascalCase classes/functions, camelCase variables, m_ members, k constants
- `#pragma once` for headers (not traditional guards)
- User's clang-format config: tabs, 140 col limit, align declarations
- clang-tidy with naming enforcement + modernization checks
- Manual formatting (Shift+Alt+F), not on-save

**Essential Engine Patterns Documented:**
- String hashing (FNV-1a) - compile-time hash for fast lookups
- Structured logging - categories + levels, debug logs compile out
- Memory arenas - linear allocators for temp data (10-100x faster)
- Resource handles - 32-bit IDs with generation (safe, hot-reload friendly)
- Immediate mode debug draw - simple API for visualization

**UI Testability Strategy:**
- Scene graph JSON export for inspection
- HTTP debug server (debug builds only)
- Visual regression tests (screenshot comparison)
- Inspector overlay (F12 in debug builds)
- Event recording/playback

**World Generation:**
- Pluggable IWorldGenerator interface
- Initial Perlin noise generator (TEMPORARY - marked for replacement)
- Progress reporting during generation
- Integration with 3D preview and 2D sampling

**Game Development Conventions:**
- Use "scene" terminology (SplashScene), not "screen"
- Assets called "SVG files", not "images" or "textures"
- Test UI components in ui-sandbox before using in main game

**Project Structure Created:**
- All library directories with CMakeLists.txt
- VSCode config (launch.json, tasks.json, settings.json)
- .clang-format and .clang-tidy configs
- .gitignore configured
- vcpkg.json with dependencies (including nanosvg for SVG)
- Asset directories: assets/tiles/{terrain,vegetation,structures}
- README.md with full setup instructions

**Next Session Should:**
1. Test that CMake configures properly
2. Implement string hashing system
3. Implement logging system
4. Begin creating actual library implementations
