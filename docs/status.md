# Project Status

Last Updated: 2025-10-24

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
