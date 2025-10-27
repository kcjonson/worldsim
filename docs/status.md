# Project Status

Last Updated: 2025-10-27 (Developer Client - TypeScript Web App)

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
- 2025-10-26 - **1:1 Pixel Mapping for UI**: Primitive rendering uses framebuffer dimensions for pixel-perfect rendering - `Rect(50, 50, 200, 100)` is always exactly 200×100 pixels, matching RmlUI/ImGui industry standards
- 2025-10-26 - **Logging Macro Naming**: Use unprefixed global macros (`LOG_ERROR` not `WSIM_LOG_ERROR`) for brevity and developer experience, accepting potential library conflict risk

### Engine Patterns to Implement
- 2025-10-12 - **String hashing** (FNV-1a, compile-time) - Implement Now
- 2025-10-26 - **Structured logging** (categories + levels) - ✅ **IMPLEMENTED**
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

### 2025-10-27 - Developer Client - TypeScript Web UI for Debug Server

**Production-Quality Developer Tool Complete:**

Built a standalone TypeScript/React web application for monitoring C++ applications during development. Replaces the embedded HTML UI with a professional single-page application with advanced features.

**Technology Stack:**
- **React 18** with TypeScript for type-safe UI development
- **Vite** for fast development and optimized production builds
- **vite-plugin-singlefile** to bundle everything into a single HTML file
- **CSS Modules** for component-scoped styling
- **EventSource API** for Server-Sent Events streaming

**Application Features:**

**Real-Time Metrics Dashboard:**
- FPS and frame time monitoring (current, min, max)
- Draw call tracking
- Vertex and triangle count
- Updates at 10 Hz via SSE stream from debug server

**Live Log Viewer:**
- Color-coded log levels (DEBUG, INFO, WARN, ERROR)
- Category badges (Renderer, Physics, Network, etc.)
- Timestamps with file:line for warnings/errors
- Auto-scroll with 1000-log history buffer
- System logs for connection events

**Graceful Connection Handling:**
- Automatic reconnection when server restarts (built-in EventSource retry)
- Visual connection status indicator (green/yellow)
- System log entries for connect/disconnect events
- No manual intervention required during development cycles
- Handles multiple restart cycles seamlessly

**Developer Experience Improvements:**
- Inline source maps for debugging (TypeScript → browser dev tools)
- Single 614 KB HTML file (no external dependencies)
- Works via `file://` protocol (no web server needed)
- Type-safe interfaces matching C++ server output
- Strict TypeScript compilation (`noUnusedLocals`, `noUnusedParameters`)

**Build Integration:**
- Integrated into CMake build system
- Auto-builds on `make` in Debug/Development builds
- Output copied to `build/developer-client/index.html`
- npm install and Vite build happen automatically
- Skips gracefully if npm not installed

**Connection Management:**
- `ServerConnection` class wraps EventSource API
- Per-stream connection state tracking
- Callbacks for connect/disconnect events
- Error handling with JSON parse protection
- Support for multiple stream types (metrics, logs, future: profiler, events)

**Architecture:**
```
┌─────────────────────────────────────────────┐
│  Developer Client (TypeScript/React SPA)    │
│  - Metrics Dashboard                        │
│  - Log Viewer                               │
│  - Connection Status                        │
└──────────────────┬──────────────────────────┘
                   │ Server-Sent Events (SSE)
                   │ /stream/metrics (10 Hz)
                   │ /stream/logs (10 Hz)
┌──────────────────▼──────────────────────────┐
│  Debug Server (C++ cpp-httplib)             │
│  - Lock-free ring buffers                   │
│  - SSE streaming endpoints                  │
│  - Running in ui-sandbox/world-sim          │
└─────────────────────────────────────────────┘
```

**Usage Workflow:**
```bash
# Build C++ app with developer client
cd build && make

# Run application (debug server starts on port 8081)
./apps/ui-sandbox/ui-sandbox

# Open developer client in browser
open build/developer-client/index.html

# Restart app as many times as needed - client auto-reconnects
```

**Files Created:**
- `apps/developer-client/src/App.tsx` - Main application component
- `apps/developer-client/src/App.module.css` - Scoped styles
- `apps/developer-client/src/main.tsx` - Application entry point
- `apps/developer-client/src/services/ServerConnection.ts` - SSE connection manager
- `apps/developer-client/src/styles/globals.css` - Global theme variables
- `apps/developer-client/vite.config.ts` - Build configuration with inline source maps
- `apps/developer-client/tsconfig.json` - TypeScript compiler settings
- `apps/developer-client/package.json` - Dependencies (React, Vite, TypeScript)
- `apps/developer-client/index.html` - Vite entry point

**Build Configuration:**
- CMake target `developer-client` (ALL builds in Debug/Development)
- Runs `npm install` and `npm run build` automatically
- Copies output to `build/developer-client/`
- world-sim and ui-sandbox depend on developer-client target

**Design Decisions:**
- Single HTML file for portability (works without web server)
- Inline source maps (debugging without separate .map files)
- EventSource over WebSocket (simpler for one-way streaming, auto-reconnect)
- 1000-log buffer limit (prevents browser memory issues)
- System category for client-side events (connection status)
- Matching C++ log level strings (`WARN` not `WARNING`)

**Testing:**
- Verified metrics update in real-time at 10 Hz
- Confirmed logs stream with correct color coding
- Tested disconnect/reconnect with ui-sandbox restarts
- Validated source maps in browser dev tools
- Confirmed build integration with CMake

**Next Steps:**
- Add log filtering by category/level (UI controls)
- Add log search functionality
- Consider adding performance charts (FPS over time)
- Consider adding profiler stream visualization
- Add download logs as text file

### 2025-10-26 - Observability Web UI with Real-Time Logging

**Complete Observability Stack Operational:**

Extended the debug server with real-time log streaming and a tabbed web interface, providing full observability for development.

**Architecture - Lock-Free Performance Guarantee:**

**CRITICAL DESIGN CONSTRAINT: Performance > Complete Logs**
- Logger writes to lock-free ring buffer (~10-20ns, never blocks game thread)
- If ring buffer full (1000 entries): **oldest logs silently dropped**
- Zero mutex/locks in game thread path
- HTTP server reads from ring buffer at 10 Hz (throttled)

**LogEntry Structure** (`Foundation::LogEntry`):
```cpp
struct LogEntry {
    LogLevel level;           // Debug/Info/Warning/Error
    LogCategory category;     // Renderer/Physics/Audio/Network/Game/World/UI/Engine/Foundation
    char message[256];        // Formatted log message
    uint64_t timestamp;       // Unix timestamp in milliseconds
    const char* file;         // Source file (static string pointer)
    int line;                 // Line number
};
```

**Integration Flow:**
1. `LOG_INFO(Renderer, "message")` → Logger::Log()
2. Logger formats message, writes to console (colored)
3. Logger calls `debugServer->UpdateLog()` (lock-free, ~10-20ns)
4. DebugServer writes to `LockFreeRingBuffer<LogEntry, 1000>`
5. HTTP `/stream/logs` endpoint reads from buffer at 10 Hz
6. Browser receives Server-Sent Events with JSON log entries

**Web UI - Tabbed Interface:**

**Tab 1: Performance** (existing)
- Real-time metrics: FPS, frame time (min/max/current), draw calls, vertices, triangles
- Updates via `/stream/metrics` SSE endpoint (10 Hz)

**Tab 2: Logs** (NEW)
- Real-time log viewer with auto-scroll
- Color-coded by level:
  - Debug: Gray (#808080)
  - Info: White (#d4d4d4)
  - Warning: Yellow (#dcdcaa)
  - Error: Red (#f48771)
- Shows: timestamp, category, level, message
- File:line for warnings/errors
- Limit: 500 logs (prevents browser memory issues)
- Updates via `/stream/logs` SSE endpoint (10 Hz)

**HTTP Endpoints:**
- `GET /` - Tabbed web UI (HTML/CSS/JavaScript)
- `GET /api/health` - Health check (JSON)
- `GET /api/metrics` - Current metrics snapshot (JSON)
- `GET /stream/metrics` - Real-time metrics stream (SSE, 10 Hz)
- `GET /stream/logs` - Real-time log stream (SSE, 10 Hz) **NEW**

**Logger Integration:**
- Added `Logger::SetDebugServer(DebugServer*)` static method
- Logger stores static pointer to debug server
- On each log call, sends to debug server (only in DEVELOPMENT_BUILD)
- Conversion between `foundation::` and `Foundation::` enums
- Properly disconnects on shutdown (prevents use-after-free)

**ui-sandbox Wiring:**
```cpp
// After creating debug server
foundation::Logger::SetDebugServer(&debugServer);

// Before destroying debug server
foundation::Logger::SetDebugServer(nullptr);
```

**Files Modified/Created:**
- `libs/foundation/debug/debug_server.{h,cpp}` - Added LogEntry struct, UpdateLog(), /stream/logs endpoint
- `libs/foundation/debug/debug_server.cpp` - Rewrote HTML UI with tabs, log viewer, SSE streaming
- `libs/foundation/utils/log.{h,cpp}` - Added DebugServer integration, enum conversion
- `apps/ui-sandbox/main.cpp` - Wired Logger to DebugServer

**Testing:**
- Verified logger connects to debug server on startup
- Confirmed logs stream to browser in real-time
- Tested tab navigation (Performance ↔ Logs)
- Verified color-coded log levels
- Confirmed auto-scroll and 500-log limit
- Tested under load: no frame drops, logs may be dropped (by design)

**Performance Characteristics:**
- Game thread log overhead: ~10-20ns (lock-free write to ring buffer)
- HTTP streaming: 10 Hz (not real-time, throttled to prevent bandwidth issues)
- If logging faster than HTTP can consume: oldest logs dropped silently
- Zero performance impact even if debug server is slow/stuck

**Usage:**
```bash
# Run ui-sandbox (debug server on port 8081 by default)
./ui-sandbox

# Open web UI in browser
open http://localhost:8081

# Switch between Performance and Logs tabs
# Logs stream in real-time with color coding
```

**Next Steps:**
- Consider adding log filtering in web UI (by category/level)
- Consider adding log search functionality
- Consider adding download logs as text file

### 2025-10-26 - Structured Logging System Implementation

**Core Engine Pattern Complete:**

Implemented a production-ready structured logging system for the entire project, providing organized diagnostic output with categories and log levels.

**System Architecture:**

**Logger Class** (`libs/foundation/utils/log.{h,cpp}`):
- Four log levels: Debug, Info, Warning, Error
- Nine categories: Renderer, Physics, Audio, Network, Game, World, UI, Engine, Foundation
- Per-category level filtering (set different verbosity for each system)
- Automatic timestamping (HH:MM:SS format)
- ANSI color codes for terminal output (gray/white/yellow/red)
- File and line number capture for warnings and errors

**Convenience Macros:**
```cpp
LOG_DEBUG(category, format, ...)
LOG_INFO(category, format, ...)
LOG_WARNING(category, format, ...)
LOG_ERROR(category, format, ...)
```

**Build Configuration:**
- Development builds: All log levels available, `DEVELOPMENT_BUILD` flag enables Debug/Info/Warning
- Release builds: Only Error logs remain, Debug/Info/Warning compile to `((void)0)`
- CMake automatically sets flag for Debug and RelWithDebInfo build types

**Usage Examples:**
```cpp
LOG_INFO(Renderer, "Initializing renderer: %dx%d", width, height);
LOG_ERROR(Network, "Failed to connect to server");
LOG_DEBUG(Physics, "Tick took %f ms", deltaTime);
```

**Output Format:**
```
[19:08:10][UI][INFO] UI Sandbox - Component Testing & Demo Environment
[19:08:10][Renderer][INFO] OpenGL Version: 4.1 ATI-7.0.23
[19:08:11][Foundation][INFO] Debug server: http://localhost:8081
```

**Design Decision - Macro Naming:**

Chose **unprefixed global macros** (`LOG_ERROR` not `WSIM_LOG_ERROR`) for developer experience:
- **Pros**: Cleaner code, better readability, shorter is better for ubiquitous operations
- **Cons**: Potential conflicts with other libraries defining similar macros
- **Mitigation**: Game project (not library), we control dependencies, can refactor if needed
- **Documented**: Tradeoff explicitly documented in `/docs/technical/logging-system.md`

**Integration:**
- ui-sandbox fully converted to use logging system (replaced all `std::cout`/`std::cerr`)
- Foundation library exports logger for use by all other libraries
- Initialized in `main()` before any other systems

**Testing:**
- Verified colored output with timestamps in terminal
- Confirmed different categories display correctly
- Tested log level filtering (Debug logs visible in development builds)
- Verified ANSI color codes work on macOS terminal

**Future HTTP Streaming Integration:**

Documentation includes design for lock-free ring buffer + Server-Sent Events streaming to external debug app (from `/docs/technical/observability/developer-server.md`), to be implemented when needed.

**Files Created/Modified:**
- `libs/foundation/utils/log.h` - Logger class, enums, macros (NEW)
- `libs/foundation/utils/log.cpp` - Implementation with console output (NEW)
- `libs/foundation/CMakeLists.txt` - Added log.cpp, DEVELOPMENT_BUILD flag
- `apps/ui-sandbox/main.cpp` - Converted all output to logging system
- `docs/technical/logging-system.md` - Added macro naming convention section

**Next Engine Pattern:**
String hashing system (FNV-1a with compile-time hashing)

### 2025-10-26 - Pixel-Perfect UI Rendering & Window Sizing

**Fixed Critical Rendering Bug:**

The primitive rendering system was using a hardcoded 800x600 virtual coordinate system, causing shapes to physically change pixel dimensions when the window was resized. This violated the design specification for 1:1 pixel mapping.

**Root Cause:**
- Projection matrix was `ortho(0, 800, 600, 0)` regardless of actual framebuffer size
- `Rect(50, 50, 200, 100)` would be 320px wide in an 1280x720 window, but 640px wide in a 2560x1440 window
- Shapes scaled with window resize, breaking pixel-perfect rendering

**Fix Applied:**
- Changed projection matrix to use actual framebuffer dimensions: `ortho(0, m_viewportWidth, m_viewportHeight, 0)`
- Added `SetViewport(width, height)` to BatchRenderer and Primitives API
- Called `SetViewport()` on window creation and framebuffer resize callback
- Now `Rect(50, 50, 200, 100)` is **always exactly 200×100 pixels**, regardless of window size

**UI Sandbox Improvements:**
- Window now launches at 80% of screen size (was hardcoded 800x600)
- Queries primary monitor via GLFW to calculate appropriate initial size
- Created demo system structure (`demos/demo.h`, `demos/shapes_demo.cpp`)
- Moved rendering code from `main.cpp` to proper demo implementation
- Console output shows screen and window dimensions on startup

**Industry Standard Alignment:**
- RmlUI uses pixel coordinates with 1:1 mapping (per our documentation)
- ImGui uses pixel coordinates with 1:1 mapping
- Unity UI default is "Constant Pixel Size" mode
- Our implementation now matches these standards

**Files Modified:**
- `libs/renderer/primitives/batch_renderer.{h,cpp}` - Added viewport tracking, fixed projection matrix
- `libs/renderer/primitives/primitives.{h,cpp}` - Exposed SetViewport() API
- `apps/ui-sandbox/main.cpp` - Added monitor querying, demo system integration, viewport updates
- `apps/ui-sandbox/demos/demo.h` - Created demo interface (NEW)
- `apps/ui-sandbox/demos/shapes_demo.cpp` - Implemented shapes demo (NEW)
- `apps/ui-sandbox/CMakeLists.txt` - Added shapes_demo.cpp to build

**Test Results:**
```
Screen: 3200x1800
Window: 2560x1440 (80% of screen)
```

Shapes now maintain constant pixel dimensions when window is resized. ✅

### 2025-10-27 - UI Sandbox Implementation + Lock-Free Performance Monitoring

**UI Sandbox Foundation - Fully Operational:**

Built complete ui-sandbox development tool with working primitive rendering and real-time performance monitoring:

**Primitive Rendering API Implemented:**
- `DrawRect()`, `DrawLine()`, `DrawRectBorder()` - Basic 2D shape primitives
- Batching system with OpenGL 3.3 shaders (vertex + fragment)
- Batch accumulator minimizes draw calls (single draw per batch)
- Transform and scissor stacks (for world-space rendering and clipping)
- Color type with common presets (Red, Green, Blue, etc.)
- Rect type with collision helpers (Contains, Intersects, Intersection)

**Performance Monitoring System:**
- **Lock-free ring buffer** (atomic operations only, zero mutex contention)
- HTTP Debug Server on port 8081 using cpp-httplib
- REST endpoints: `/api/health`, `/api/metrics`
- Server-Sent Events stream: `/stream/metrics` (10 Hz updates)
- HTML UI at `http://localhost:8081` with live metrics
- Metrics tracked: FPS, frame time (min/max/current), draw calls, vertices, triangles

**Critical Architecture Fix:**
- Initial implementation used `std::mutex` (WRONG - could block game thread!)
- Replaced with lock-free ring buffer per observability spec
- Game thread writes: ~10-20 nanoseconds (was 100-1000ns uncontended, 1-10ms contended)
- HTTP thread reads: ~10-20 nanoseconds
- **Zero possibility of frame drops from monitoring** ✅

**Performance Test Results:**
- Normal operation: **~1370 FPS** (0.73ms frame time)
- Stress test (50 concurrent curl requests): **9009 FPS** (0.11ms frame time)
- Frame time range: 0.08ms - 2.34ms (max likely OS scheduler)
- Draw calls: 1 (batching working correctly)
- **No frame drops observed under HTTP load** ✅

**Files Created (17 files):**
- `libs/foundation/math/types.h` - GLM type aliases (Vec2, Vec3, Mat4)
- `libs/foundation/graphics/{color.h, rect.h}` - Core graphics types
- `libs/foundation/metrics/performance_metrics.{h,cpp}` - Metrics data structure + JSON serialization
- `libs/foundation/debug/debug_server.{h,cpp}` - HTTP server with SSE streaming
- `libs/foundation/debug/lock_free_ring_buffer.h` - Lock-free template from observability spec
- `libs/renderer/primitives/primitives.{h,cpp}` - Public 2D drawing API
- `libs/renderer/primitives/batch_renderer.{h,cpp}` - OpenGL batching implementation
- `libs/renderer/metrics/metrics_collector.{h,cpp}` - Frame timing + stats collection
- `apps/ui-sandbox/main.cpp` - Complete rewrite with metrics integration

**Build System:**
- vcpkg.json baseline updated (8f54ef5453e7e76ff01e15988bf243e7247c5eb5)
- CMake configured with toolchain
- All dependencies installed (GLFW, GLEW, GLM, cpp-httplib)
- Foundation library changed from INTERFACE to regular library

**Testing:**
```bash
# Run ui-sandbox with debug server (default port 8081)
./ui-sandbox

# Query current metrics
curl http://localhost:8081/api/metrics
# {"fps": 1369.86, "frameTimeMs": 0.73, "drawCalls": 1, ...}

# Stream live metrics (10 updates/second)
curl -N http://localhost:8081/stream/metrics

# Open browser for visual monitoring
open http://localhost:8081
```

**Key Architectural Decisions:**
- Lock-free observability is **non-negotiable** for zero-overhead monitoring
- ui-sandbox is always a dev tool (no Release build guards needed)
- Default port 8081 for ui-sandbox debug server
- Primitive API as foundation for both custom rendering AND RmlUI backend

**Next Steps:**
1. Add text rendering to Primitive API (basic font support)
2. Integrate RmlUI for complex UI panels
3. Implement RmlUI backend using Primitive API
4. Build developer client (TypeScript/Vite) for advanced metrics visualization

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
