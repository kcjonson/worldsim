# HTTP Debug Server

## Overview

The game engine includes an embedded HTTP server that streams real-time metrics, debug information, and telemetry data to external debugging applications. This approach eliminates performance overhead from in-game debug overlays while providing rich debugging capabilities.

## Problem Statement

Traditional in-game debug panels have significant drawbacks:
- Opening debug overlays causes performance drops, masking actual performance issues
- Limited screen real estate for displaying comprehensive metrics
- Debug UI competes for rendering resources with the game itself
- Difficult to maintain complex debug interfaces in C++

## Solution

An HTTP server runs within the game engine (debug builds only) that:
1. Streams metrics and debug data in real-time using Server-Sent Events (SSE)
2. Provides REST endpoints for one-time queries and snapshots
3. Serves a web-based debug application (TypeScript/HTML) for visualization
4. Runs in a separate process/browser, eliminating performance impact on the game

## Architecture: Hybrid Streaming + REST

### Server-Sent Events (SSE) - Recommended for Streaming Data

**Why SSE over WebSockets:**
- **Simpler protocol**: One-way server→client is perfect for metrics streaming
- **Lower overhead**: Built on HTTP/1.1, less complex than WebSocket handshake
- **Auto-reconnection**: Browser EventSource API handles reconnects automatically
- **Easier implementation**: Simpler C++ server code, less state management
- **Sufficient for 95% of use cases**: Debug data flows primarily server→client

**When to use WebSockets instead:**
Future features requiring bidirectional communication:
- Live component editing from web app
- Command injection (pause, step frame, trigger events)
- Interactive debugging sessions

**Recommendation**: Start with SSE, add WebSocket endpoints later if bidirectional features are needed.

### Hybrid Endpoint Design

#### SSE Streams (Real-time)
```
GET /stream/metrics         → FPS, frame time, memory (60Hz max)
GET /stream/logs            → New log entries as they occur
GET /stream/profiler        → Frame profiling data (configurable rate)
GET /stream/renderer        → Draw calls, texture swaps, etc.
```

#### REST Endpoints (On-demand snapshots)
```
GET /api/scene              → Full scene hierarchy JSON dump
GET /api/resources          → All loaded resources and memory
GET /api/config             → Current engine configuration
POST /api/screenshot        → Trigger screenshot capture
```

## Requirements

### Functional Requirements

#### HTTP Server (C++ in Engine)
- **REQ-DS-001**: Server runs on configurable port (default: 8080) in debug builds only
- **REQ-DS-002**: Server is completely compiled out in release builds (zero overhead)
- **REQ-DS-003**: Implements SSE streaming for real-time data:
  - `/stream/metrics` - Performance metrics (FPS, frame time, memory, CPU usage)
  - `/stream/logs` - Log entries with category, level, timestamp, message
  - `/stream/profiler` - Frame timing, function costs, call counts
  - `/stream/renderer` - Draw calls, state changes, buffer usage
- **REQ-DS-004**: Provides REST endpoints for snapshots:
  - `GET /api/scene` - Current scene state and entity hierarchy
  - `GET /api/resources` - Loaded resources and memory consumption
  - `GET /api/config` - Engine configuration values
  - `GET /api/health` - Server health check and uptime
- **REQ-DS-005**: Supports CORS for local development
- **REQ-DS-006**: SSE streams update at configurable rates (default: 10Hz, max: 60Hz)
- **REQ-DS-007**: Server can be toggled on/off at runtime via config or console command
- **REQ-DS-008**: Streams buffer recent data (last 100 entries) for late-connecting clients

#### Web Debug Application (TypeScript)
- **REQ-DS-009**: Standalone web application served from `/` (static files)
- **REQ-DS-010**: Connects to SSE streams using EventSource API
- **REQ-DS-011**: Real-time metrics dashboard with custom-drawn charts (Canvas/SVG)
- **REQ-DS-012**: Scene hierarchy inspector (fetched via REST, updated on-demand)
- **REQ-DS-013**: Log viewer with real-time streaming, filtering, search, color-coding
- **REQ-DS-014**: Performance profiler visualization (flame graphs, timing charts)
- **REQ-DS-015**: Renderer statistics display with visual representations
- **REQ-DS-016**: Auto-reconnection when game restarts (EventSource built-in)
- **REQ-DS-017**: Dark theme for comfortable viewing during development
- **REQ-DS-018**: Pause/resume stream consumption (client-side buffering)

### Non-Functional Requirements

#### Performance
- **REQ-DS-NF-001**: HTTP server overhead < 0.1ms per frame when no clients connected
- **REQ-DS-NF-002**: SSE event generation overhead < 0.3ms per frame per active stream
- **REQ-DS-NF-003**: Metric collection overhead < 0.5ms per frame total
- **REQ-DS-NF-004**: Large snapshots (>100KB) compressed with gzip
- **REQ-DS-NF-005**: No blocking operations in game thread (async I/O for network)
- **REQ-DS-NF-006**: Circular buffers for stream data to prevent memory growth

#### Development
- **REQ-DS-NF-007**: Web app uses modern TypeScript with hot-reload during development
- **REQ-DS-NF-008**: Web app builds to static files bundled with the game
- **REQ-DS-NF-009**: HTTP server uses minimal dependencies (prefer header-only libraries)

#### Security
- **REQ-DS-NF-010**: Server only binds to localhost (127.0.0.1) by default
- **REQ-DS-NF-011**: No authentication required (debug builds are local-only)
- **REQ-DS-NF-012**: Clear warnings if server is accidentally left enabled in production

## User Workflows

### Basic Usage
1. Developer launches game in debug build
2. HTTP server starts automatically and logs: `Debug server running on http://localhost:8080`
3. Developer opens browser to `http://localhost:8080`
4. Debug dashboard loads, connects to SSE streams
5. Real-time metrics begin streaming automatically
6. Developer can navigate between different debug views

### Debugging Performance Issues
1. Developer notices frame drops in game
2. Opens performance profiler in debug app
3. Sees live frame timing graph spike
4. Clicks spike to load detailed profiler snapshot
5. Flame graph shows slow function via `/stream/profiler` data
6. Correlates with log entries from same timestamp

### Inspecting Scene State
1. Developer wants to verify entity setup
2. Opens scene inspector in debug app
3. Fetches current scene via `GET /api/scene`
4. Browses entity hierarchy (like browser DOM inspector)
5. Expands entity to see components and properties
6. Refreshes scene view to see updates after game changes

## Technical Considerations

### SSE Protocol Details

#### Event Format
```
event: metric
data: {"fps": 60.2, "frameTime": 16.6, "memoryMB": 245.3, "timestamp": 1729800000}

event: log
data: {"level": "WARNING", "category": "renderer", "message": "Texture cache full", "timestamp": 1729800001}

event: profiler
data: {"frame": 12345, "functions": [{"name": "RenderScene", "timeUs": 8234, "calls": 1}, ...]}
```

#### Connection Handling
- Client connects → server sends last 100 buffered events (catch-up)
- Server sends heartbeat every 15 seconds (`:keepalive\n\n`)
- Client disconnect → server cleans up stream state
- Server supports multiple simultaneous clients

### HTTP Server Library Options

Ranked by suitability for SSE + minimal dependencies:

1. **cpp-httplib** (Recommended)
   - Header-only, single file (~6000 lines)
   - Native SSE support via chunked transfer encoding
   - Simple API: `svr.Get("/stream/metrics", [](const Request&, Response& res) { res.set_chunked_content_provider(...); })`
   - MIT license, actively maintained

2. **Crow**
   - Header-only, Boost-optional
   - Express.js-like API
   - SSE possible via streaming responses

3. **Beast (Boost.Asio)**
   - More complex, requires Boost
   - Maximum control over async I/O
   - Overkill for this use case

**Recommendation**: Use **cpp-httplib** for simplicity and SSE support.

### Web Framework Stack

- **Build Tool**: Vite (fast, HMR, TypeScript)
- **UI Framework**: React or Vanilla TypeScript (decision TBD)
- **Charts**: Custom canvas/SVG rendering (no external library)
- **Styling**: Plain CSS (no framework)
- **SSE Client**: Native EventSource API (browser built-in)

#### Example EventSource Usage
```typescript
const metricsSource = new EventSource('http://localhost:8080/stream/metrics');
metricsSource.addEventListener('metric', (event) => {
  const data = JSON.parse(event.data);
  updateMetricsChart(data);
});
metricsSource.onerror = () => {
  // Auto-reconnects, just log it
  console.log('Disconnected, reconnecting...');
};
```

### Integration Points

#### Game Engine Side
- **Logging System**: Ring buffer of last 1000 log entries, push to SSE when new entry arrives
- **Profiler System**: Collect frame timing data, push summary every N frames
- **Metrics Collector**: Sample FPS/memory every frame, emit SSE event at configured rate (10Hz default)
- **Scene Manager**: Provide `ExportSceneJSON()` method for REST endpoint
- **Renderer**: Track statistics (draw calls, state changes), push deltas via SSE

#### Thread Safety
- Game thread writes to lock-free ring buffers
- HTTP server thread reads from ring buffers for SSE streams
- No locks on hot path (game thread never blocks)

## Data Flow Example

```
Game Loop (60 FPS)                    HTTP Server Thread              Web Browser
     |                                        |                            |
     |-- Collect metrics ------------------>  |                            |
     |-- Write to ring buffer                 |                            |
     |                                        |                            |
     |                                        |-- Check buffer (10Hz)      |
     |                                        |-- Format SSE event         |
     |                                        |-- Send to client --------> |
     |                                        |                            |-- Update chart
     |                                        |                            |
     |-- Log WARNING ------------------->     |                            |
     |                                        |-- Immediate SSE send ----> |
     |                                        |                            |-- Append to log view
```

## Future Enhancements

- **WebSocket endpoint**: Add `/ws` for bidirectional features (command injection, live editing)
- **Remote debugging**: Optional network binding with token-based authentication
- **Command injection**: Send commands to game (pause, step frame, trigger events)
- **Live editing**: Modify component values in real-time from inspector
- **Recording/playback**: Capture metric timelines, export as JSON, replay later
- **Multiple game instances**: Dashboard connects to multiple games simultaneously
- **Performance budgets**: Set thresholds, visual alerts when exceeded

## Related Documentation

- [Technical Design: HTTP Debug Server](../../../technical/http-debug-server.md)
- [Technical Design: Logging System](../../../technical/logging-system.md)
- [UI Testability Strategy](../../../technical/ui-testability-strategy.md)
