# Developer Client - TypeScript Web UI for Debug Server

**Date:** 2025-10-27

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


