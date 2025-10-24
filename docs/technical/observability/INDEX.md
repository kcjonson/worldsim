# Observability System

Created: 2025-10-24
Status: Active

## Overview

The observability system provides real-time monitoring and inspection capabilities for development builds. All components use HTTP Server-Sent Events (SSE) to stream data from the running application to an external web-based developer client.

**Key Principle:** Zero in-game overhead. All visualization happens in an external browser window, not in the game viewport.

## Architecture

```
┌─────────────────────────────────────────────┐
│ Application Process                         │
│ (Game Client / Game Server / ui-sandbox)    │
│                                              │
│  Game/UI Thread (60 FPS)                    │
│    - Collect metrics                         │
│    - Write logs                              │
│    - Capture UI state                        │
│    - Write to lock-free ring buffers ─────┐ │
│                                             │ │
│  HTTP Server Thread (10-20 Hz)             │ │
│    - Read from ring buffers  ◄─────────────┘ │
│    - Stream via SSE                          │
│    - Serve static web app                    │
│                                              │
└──────────────────┬───────────────────────────┘
                   │ HTTP (localhost)
                   │ Port 8080/8081/8082
        ┌──────────▼─────────────┐
        │ Developer Client       │
        │ (Browser Window)       │
        │                        │
        │ - Metrics charts       │
        │ - Log viewer           │
        │ - UI hierarchy tree    │
        │ - Profiler             │
        └────────────────────────┘
```

## Components

### [Developer Server](./developer-server.md)
**C++ HTTP server embedded in application**
- SSE streaming architecture
- Lock-free ring buffers for data collection
- Threading model (game thread never blocks on network)
- Multiple stream endpoints (metrics, logs, UI, events)
- Compile-time exclusion in Release builds

**Ports:**
- 8080: Game server
- 8081: ui-sandbox
- 8082: Main game client

### [Developer Client](./developer-client.md)
**External TypeScript/Vite web application**
- Connects to developer server via SSE
- Real-time data visualization
- Metrics charts (Canvas rendering)
- Log viewer with filtering
- Build integration with CMake

### [UI Inspection](./ui-inspection.md)
**UI hierarchy and hover inspection**
- Real-time UI hierarchy streaming
- Mouse hover inspection (F3 toggle)
- Visual layer stack + component hierarchy
- UI event streaming
- Screenshot capture for visual regression testing

## Stream Endpoints

| Endpoint | Rate | Purpose | Data |
|----------|------|---------|------|
| `/stream/metrics` | 10 Hz | Game server metrics | FPS, memory, ECS, chunks |
| `/stream/logs` | 20 Hz | Log messages | Filtered by level/category |
| `/stream/ui` | 10 Hz | UI hierarchy | Scene graph as JSON |
| `/stream/hover` | 10 Hz | Mouse hover data | Layer stack + component hierarchy |
| `/stream/events` | 20 Hz | UI events | Clicks, keys, focus changes |
| `/stream/profiler` | 5 Hz | Performance data | Function timings, flame graph |

## REST Endpoints (Snapshots)

| Endpoint | Purpose |
|----------|---------|
| `/api/health` | Server status, uptime, connected clients |
| `/api/scene` | Current ECS state snapshot |
| `/api/ui/tree` | UI hierarchy snapshot |
| `/api/ui/screenshot` | PNG screenshot |
| `/api/resources` | Resource usage (textures, memory) |

## Build Configuration

**Development Build** (`-DCMAKE_BUILD_TYPE=Development`):
- `DEVELOPMENT_BUILD` flag set
- Developer server compiled in
- All streaming endpoints available
- External developer client served on localhost
- ~2 MB memory overhead
- ~0.3ms per frame overhead (worst case)

**Release Build** (`-DCMAKE_BUILD_TYPE=Release`):
- Developer server compiled out completely
- Zero code footprint
- Zero runtime overhead
- No ports opened

## Related Systems

### [Logging System](../logging-system.md)
Core logging implementation that outputs to:
- Console/stdout
- File (optional)
- Developer server ring buffer (Development builds)

### [Diagnostic Drawing](../diagnostic-drawing.md)
Manual in-viewport debugging tool:
- Draw lines, boxes, spheres in game viewport
- Temporary visualization during development
- Different from observability (renders IN game, not external)

## Quick Start

### Running with Observability

```bash
# Build in Development mode
cmake -DCMAKE_BUILD_TYPE=Development -B build
cmake --build build

# Run application (developer server auto-starts)
./build/apps/ui-sandbox/ui-sandbox

# Open developer client in browser
open http://localhost:8081

# Enable UI hover inspection (F3 in application)
# See hover data in developer client
```

### Viewing Different Applications

```bash
# Game server (port 8080)
./build/apps/game-server/world-sim-server
open http://localhost:8080

# ui-sandbox (port 8081)
./build/apps/ui-sandbox/ui-sandbox
open http://localhost:8081

# Main game client (port 8082)
./build/apps/world-sim/world-sim
open http://localhost:8082
```

## Implementation Status

### Developer Server
- [x] Architecture defined
- [x] SSE streaming design
- [x] Ring buffer specification
- [ ] Core HTTP server implementation
- [ ] Metrics collection
- [ ] Log streaming
- [ ] Profiler integration

### Developer Client
- [x] Architecture defined
- [x] Technology stack chosen (TypeScript/Vite)
- [ ] SSE client implementation
- [ ] Metrics chart rendering
- [ ] Log viewer UI
- [ ] Build integration

### UI Inspection
- [x] Strategy defined
- [x] Hover inspection design (F3 toggle)
- [ ] Scene graph JSON serialization
- [ ] UI state streaming
- [ ] Hover data collection
- [ ] Event streaming

## Design Principles

1. **Non-intrusive**: Game thread never blocks on network I/O
2. **Lock-free**: Ring buffers for data collection
3. **Rate-limited**: Streaming at 10-20 Hz prevents spam
4. **External**: All visualization in separate browser window
5. **Zero-cost**: Compiled out completely in Release builds
6. **Professional**: Industry-standard observability patterns

## Notes

**Thread Safety**: All ring buffers are lock-free. Game thread writes, HTTP thread reads. No mutexes in hot path.

**Performance**: With no clients connected, overhead is <0.1ms per frame (just ring buffer writes). With clients, <0.3ms per frame.

**Security**: Developer server only binds to `127.0.0.1` (localhost). Not accessible from network. No authentication needed (local development only).

**Multi-process**: Can run multiple applications simultaneously, each on different port. Developer client can connect to multiple processes.
