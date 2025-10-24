# HTTP Debug Server - Technical Design

## Overview

The HTTP Debug Server is an embedded web server that runs within the game server process (debug builds only) and streams real-time metrics, logs, and debug information to a standalone web application. This design eliminates the performance overhead of in-game debug overlays.

**Critical**: The debug server is completely separate from the game client/server architecture. It observes the game server but doesn't affect gameplay.

## Architecture

### Component Structure

```
┌─────────────────────────────────────────────────────┐
│ Game Server Process (world-sim-server)              │
│                                                      │
│  ┌────────────────────────────────────────────┐    │
│  │ Main Thread (Game Logic) - 60 TPS          │    │
│  │  - Update ECS                               │    │
│  │  - Broadcast to game clients (60 Hz)       │    │
│  │  - Collect metrics → Ring Buffer           │    │
│  │  - Write logs → Ring Buffer                │    │
│  └────────────────┬───────────────────────────┘    │
│                   │ Lock-free                       │
│  ┌────────────────▼───────────────────────────┐    │
│  │ HTTP Server Thread - 10 Hz read            │    │
│  │  - Read from ring buffers                  │    │
│  │  - Rate-limited SSE streaming              │    │
│  │  - Serve static web app files              │    │
│  └────────────────────────────────────────────┘    │
│                                                      │
└──────────────────────────────────────────────────────┘
                    │
    ┌───────────────┴────────────────┐
    │                                 │
    │  Browser (localhost:8080)      │  ← Separate from game!
    │  Debug Web App (TypeScript)    │
    │  - Metrics charts (Canvas)     │
    │  - Log viewer                  │
    │  - Profiler visualization      │
    └────────────────────────────────┘
```

### Threading Model

**Game Thread (Main):**
- Runs game logic at 60 TPS
- Collects metrics, writes to lock-free ring buffers
- Logs events, writes to log ring buffer
- **Never blocks on network I/O**
- Overhead: <0.05ms per frame

**HTTP Server Thread:**
- Handles incoming HTTP requests
- Reads from ring buffers at configurable rate (default: 10 Hz)
- Sends SSE events to connected clients
- **No locks on hot path**
- Runs independently of game loop

**Key Principle**: Game thread writes fast (60 Hz), server thread reads slow (10 Hz). Completely decoupled.

## Server-Sent Events (SSE) Implementation

### Why SSE Over WebSocket?

- **One-way streaming**: Debug data flows server→client (95% of use case)
- **Simpler than WebSocket**: Built on HTTP, no handshake complexity
- **Auto-reconnection**: Browser `EventSource` API handles reconnects automatically
- **Lower overhead**: Less protocol complexity than WebSocket
- **Easier C++ implementation**: Chunked transfer encoding is straightforward

### SSE Stream Endpoints

```cpp
// cpp-httplib implementation with rate limiting
svr.Get("/stream/metrics", [](const httplib::Request& req, httplib::Response& res) {
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("Access-Control-Allow-Origin", "*");

    res.set_chunked_content_provider(
        "text/event-stream",
        [](size_t offset, httplib::DataSink& sink) {
            StreamMetricsWithRateLimit(sink);
            return true;
        }
    );
});

void StreamMetricsWithRateLimit(httplib::DataSink& sink) {
    const int updateRateHz = 10; // Configurable: 1-60 Hz
    const auto updateInterval = std::chrono::milliseconds(1000 / updateRateHz);

    auto lastUpdate = std::chrono::steady_clock::now();

    while (g_debugServer.IsRunning() && sink.is_writable()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - lastUpdate;

        // Only send if enough time has passed (rate limiting)
        if (elapsed >= updateInterval) {
            MetricsSample sample;
            if (g_metricsBuffer.ReadLatest(sample)) {
                std::string event = "event: metric\n";
                event += "data: " + sample.ToJSON() + "\n\n";
                sink.write(event.c_str(), event.size());
            }
            lastUpdate = now;
        }

        // Sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

### Stream Rate Configuration

Different streams have different needs:

| Stream | Rate | Interval | Rationale |
|--------|------|----------|-----------|
| `/stream/metrics` | 10 Hz | 100ms | Smooth graphs, sub-second latency |
| `/stream/logs` | 20 Hz (throttled) | 50ms | New logs push ASAP, but prevent spam |
| `/stream/profiler` | 5 Hz | 200ms | Large payloads, high frequency unnecessary |
| `/stream/renderer` | 10 Hz | 100ms | Similar to metrics |

**Latency Analysis:**
- Game writes metrics every 16.6ms (60 FPS)
- Server reads latest every 100ms
- Worst-case latency: ~100ms (well under 1 second requirement) ✓
- Typical latency: ~50ms average

### Event Format

All SSE events follow this structure:

```
event: <event_type>
data: <json_payload>

```

**Examples:**

```
event: metric
data: {"fps": 60.2, "frameTimeMs": 16.6, "memoryMB": 245, "timestamp": 1729800000}

event: log
data: {"level": "WARNING", "category": "renderer", "message": "Texture cache full", "timestamp": 1729800001}

event: profiler
data: {"frame": 12345, "functions": [{"name": "RenderScene", "timeUs": 8234, "calls": 1}]}
```

### Connection Management

**Client connects:**
1. Server creates new SSE session
2. Server sends last 100 buffered events (catch-up)
3. Server adds client to active sessions list
4. Server starts rate-limited streaming of new events

**Heartbeat:**
- Server sends `:keepalive\n\n` every 15 seconds
- Prevents proxy/firewall timeouts
- Client detects broken connections

**Client disconnects:**
- Server detects broken socket (write fails)
- Server removes client from active sessions
- Server cleans up session state
- **Game continues running unaffected**

## Data Collection & Ring Buffers

### Lock-Free Ring Buffer Design

```cpp
template<typename T, size_t N>
class LockFreeRingBuffer {
    std::array<T, N> m_buffer;
    std::atomic<size_t> m_writeIndex{0};
    std::atomic<size_t> m_readIndex{0};

public:
    // Game thread writes (never blocks)
    void Write(const T& item) {
        size_t writeIdx = m_writeIndex.load(std::memory_order_relaxed);
        m_buffer[writeIdx % N] = item;
        m_writeIndex.store(writeIdx + 1, std::memory_order_release);
    }

    // Server thread reads oldest unread item (for logs)
    bool Read(T& item) {
        size_t readIdx = m_readIndex.load(std::memory_order_relaxed);
        size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);

        if (readIdx == writeIdx) return false; // Empty

        item = m_buffer[readIdx % N];
        m_readIndex.store(readIdx + 1, std::memory_order_release);
        return true;
    }

    // Server thread reads latest item, discards intermediate (for metrics)
    bool ReadLatest(T& item) {
        size_t writeIdx = m_writeIndex.load(std::memory_order_acquire);
        if (writeIdx == 0) return false;

        item = m_buffer[(writeIdx - 1) % N];
        m_readIndex.store(writeIdx, std::memory_order_release);
        return true;
    }
};
```

**Benefits:**
- No mutex, no blocking
- Game thread never waits for network
- Fixed memory usage (no allocations)
- Old data overwritten if consumer is slow (game keeps running)

### Metrics Collection

**What to collect:**
- **Frame timing**: FPS, frame time (ms), min/max/avg over last second
- **Memory**: Heap usage, arena usage, texture memory
- **Renderer**: Draw calls, state changes, triangles rendered
- **ECS**: Entity count, system execution times
- **World**: Loaded chunks, generation queue depth

**Collection frequency:**
- Game samples every frame (60 Hz)
- Server reads latest sample every 100ms (10 Hz)
- Server discards intermediate samples (we don't need all 60 samples)

```cpp
void GameServer::Tick(float dt) {
    // ... game logic ...

    #ifdef DEBUG_BUILD
    MetricsSample sample;
    sample.fps = 1.0f / dt;
    sample.frameTimeMs = dt * 1000.0f;
    sample.memoryMB = GetMemoryUsage() / (1024 * 1024);
    sample.timestamp = GetCurrentTimestamp();

    g_metricsBuffer.Write(sample); // Lock-free, never blocks!
    #endif
}
```

### Log Buffering with Throttling

Logs are special - we want important logs immediately, but must prevent spam:

```cpp
void DebugServer::StreamLogs(httplib::DataSink& sink) {
    const int maxLogsPerSecond = 20;
    const auto minInterval = std::chrono::milliseconds(1000 / maxLogsPerSecond);

    auto lastSend = std::chrono::steady_clock::now();
    std::vector<LogEntry> buffered;

    while (m_running && sink.is_writable()) {
        // Read all available logs from ring buffer
        LogEntry entry;
        while (g_logBuffer.Read(entry)) {
            buffered.push_back(entry);
        }

        auto now = std::chrono::steady_clock::now();
        if (!buffered.empty() && (now - lastSend) >= minInterval) {
            if (buffered.size() > 10) {
                // Too many logs - send batch summary
                std::string event = "event: log_batch\n";
                event += "data: {\"count\": " + std::to_string(buffered.size()) +
                         ", \"logs\": [...]}\n\n";
                sink.write(event.c_str(), event.size());
            } else {
                // Send individual log events
                for (const auto& log : buffered) {
                    std::string event = "event: log\n";
                    event += "data: " + log.ToJSON() + "\n\n";
                    sink.write(event.c_str(), event.size());
                }
            }

            buffered.clear();
            lastSend = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

**Ring buffer of log entries:**
- Buffer last 1000 log entries
- Each entry: {timestamp, level, category, message}
- Game thread writes (never blocks)
- Server thread reads sequentially (don't skip logs)
- Rate limit: max 20 logs/second to prevent UI spam

## Web Debug Application (TypeScript)

### Technology Stack

- **Build Tool**: Vite (fast HMR, TypeScript support)
- **UI**: React or Vanilla TypeScript (TBD based on complexity)
- **Charts**: Custom Canvas rendering (no external library)
- **Styling**: Plain CSS (no framework)
- **SSE Client**: Native browser `EventSource` API

### Project Structure

```
debug-app/
  src/
    main.ts              ← Entry point
    components/
      MetricsChart.ts    ← FPS/memory line chart (Canvas)
      LogViewer.ts       ← Real-time log display
      ProfilerView.ts    ← Flame graph rendering
      SceneInspector.ts  ← Entity hierarchy tree
    services/
      EventSource.ts     ← SSE connection management
      DataStore.ts       ← Client-side data buffering
    styles/
      main.css           ← Dark theme
  index.html
  vite.config.ts
  package.json
```

### SSE Connection (TypeScript)

```typescript
class DebugClient {
    private metricsSource: EventSource;
    private logsSource: EventSource;

    connect(serverUrl: string) {
        // Connect to metrics stream (10 Hz updates)
        this.metricsSource = new EventSource(`${serverUrl}/stream/metrics`);

        this.metricsSource.addEventListener('metric', (event) => {
            const data = JSON.parse(event.data);
            this.updateMetricsChart(data);
        });

        this.metricsSource.onerror = () => {
            console.log('Disconnected, will auto-reconnect...');
            // EventSource automatically reconnects!
        };

        // Connect to log stream (20 Hz updates)
        this.logsSource = new EventSource(`${serverUrl}/stream/logs`);
        this.logsSource.addEventListener('log', (event) => {
            const log = JSON.parse(event.data);
            this.appendLogEntry(log);
        });
    }

    disconnect() {
        this.metricsSource?.close();
        this.logsSource?.close();
    }
}
```

### Custom Chart Rendering (Canvas)

No external charting library - draw directly to Canvas:

```typescript
class MetricsChart {
    private ctx: CanvasRenderingContext2D;
    private dataPoints: number[] = [];
    private maxPoints = 300; // 30 seconds at 10 Hz

    addDataPoint(value: number) {
        this.dataPoints.push(value);
        if (this.dataPoints.length > this.maxPoints) {
            this.dataPoints.shift(); // Keep last 300 points
        }
        this.render();
    }

    render() {
        const width = this.canvas.width;
        const height = this.canvas.height;

        // Clear canvas
        this.ctx.fillStyle = '#1a1a1a';
        this.ctx.fillRect(0, 0, width, height);

        // Draw grid
        this.ctx.strokeStyle = '#333';
        this.ctx.lineWidth = 1;
        for (let i = 0; i < 10; i++) {
            const y = (i / 10) * height;
            this.ctx.beginPath();
            this.ctx.moveTo(0, y);
            this.ctx.lineTo(width, y);
            this.ctx.stroke();
        }

        // Draw line chart
        this.ctx.beginPath();
        this.ctx.strokeStyle = '#00ff00';
        this.ctx.lineWidth = 2;

        this.dataPoints.forEach((value, i) => {
            const x = (i / this.dataPoints.length) * width;
            const y = height - (value / 100) * height; // Scale to 0-100
            if (i === 0) this.ctx.moveTo(x, y);
            else this.ctx.lineTo(x, y);
        });
        this.ctx.stroke();
    }
}
```

### Build Integration

**Development mode** (hot reload):
```bash
cd debug-app
npm run dev
# Opens at http://localhost:5173
# Connects to game server debug endpoint at localhost:8080
```

**Production build** (bundled with game):
```bash
npm run build
# Outputs to debug-app/dist/
# Game server serves these static files from / endpoint
```

**CMake integration:**
```cmake
# Build web app during game build (debug only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_custom_target(debug-app ALL
        COMMAND npm install
        COMMAND npm run build
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/debug-app
        COMMENT "Building debug web app"
    )

    # Copy built files to output directory
    add_custom_command(TARGET debug-app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/debug-app/dist/
        ${CMAKE_BINARY_DIR}/debug-app/
    )
endif()
```

## HTTP Server Implementation (cpp-httplib)

### Library Choice: cpp-httplib

**Why cpp-httplib:**
- Header-only (single `httplib.h` file, ~6000 lines)
- MIT license, actively maintained
- Native SSE support via chunked transfer encoding
- Simple API, minimal dependencies
- Good performance for debug builds

**Integration:**
```cmake
# vcpkg.json
{
  "dependencies": ["cpp-httplib"]
}

# CMakeLists.txt (foundation library)
find_package(httplib CONFIG REQUIRED)
target_link_libraries(foundation PRIVATE httplib::httplib)
```

### Server Initialization

```cpp
class DebugServer {
    httplib::Server m_server;
    std::thread m_serverThread;
    std::atomic<bool> m_running{false};
    int m_port;

public:
    void Start(int port = 8080) {
        m_port = port;

        // Serve static files (web app)
        m_server.set_mount_point("/", "./debug-app");

        // SSE streams with rate limiting
        m_server.Get("/stream/metrics", HandleMetricsStream);
        m_server.Get("/stream/logs", HandleLogsStream);
        m_server.Get("/stream/profiler", HandleProfilerStream);
        m_server.Get("/stream/renderer", HandleRendererStream);

        // REST endpoints (snapshots)
        m_server.Get("/api/scene", HandleSceneSnapshot);
        m_server.Get("/api/resources", HandleResourcesSnapshot);
        m_server.Get("/api/config", HandleConfigSnapshot);
        m_server.Get("/api/health", [](const auto&, auto& res) {
            json health = {
                {"status", "ok"},
                {"uptime", GetUptime()},
                {"clientsConnected", GetActiveSSEClients()}
            };
            res.set_content(health.dump(), "application/json");
        });

        // Start server on background thread (localhost only!)
        m_running = true;
        m_serverThread = std::thread([this]() {
            LOG_INFO("Debug", "Debug server starting on http://127.0.0.1:%d", m_port);
            m_server.listen("127.0.0.1", m_port);
        });
    }

    void Stop() {
        m_running = false;
        m_server.stop();
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }
        LOG_INFO("Debug", "Debug server stopped");
    }
};
```

### Compile-Time Exclusion (Release Builds)

```cpp
// foundation/DebugServer.h
#ifdef DEBUG_BUILD
class DebugServer {
    // ... full implementation ...
    void Start(int port);
    void Stop();
};
#else
// No-op stub for release builds (entire class compiled out)
class DebugServer {
public:
    void Start(int port) {} // Empty, inlined
    void Stop() {}          // Empty, inlined
};
#endif
```

**CMake configuration:**
```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(foundation PRIVATE DEBUG_BUILD)
endif()
```

**Result**: Zero code footprint in release builds. Debug server doesn't exist in shipped binaries.

## Configuration

### JSON Configuration

```json
{
  "debugServer": {
    "enabled": true,
    "port": 8080,
    "streamRates": {
      "metrics": 10,
      "logs": 20,
      "profiler": 5,
      "renderer": 10
    },
    "ringBufferSizes": {
      "metrics": 1000,
      "logs": 1000,
      "profiler": 100
    }
  }
}
```

### Runtime Toggle

```cpp
// Can be toggled via console command (debug builds)
void ConsoleCommand_ToggleDebugServer(const char* args) {
    if (g_debugServer.IsRunning()) {
        g_debugServer.Stop();
        LOG_INFO("Console", "Debug server stopped");
    } else {
        g_debugServer.Start(8080);
        LOG_INFO("Console", "Debug server started on http://localhost:8080");
    }
}
```

## Performance Considerations

### Overhead Budget

**When no clients connected:**
- Ring buffer writes: ~0.05ms per frame
- Server thread idle: ~0.01ms (just checking accept queue)
- **Total: <0.1ms per frame** ✓

**When 1 client connected:**
- Ring buffer writes: ~0.05ms
- SSE event formatting: ~0.2ms (10 Hz, amortized over 6 frames)
- Network I/O: Async on server thread, doesn't block game
- **Total: <0.3ms per frame** ✓

**When 5 clients connected:**
- SSE event formatting for 5 clients: ~0.5ms total
- Still acceptable for debug builds

### Memory Usage

**Ring buffers:**
- Metrics: 1000 samples × 64 bytes = 64 KB
- Logs: 1000 entries × 256 bytes = 256 KB
- Profiler: 100 frames × 10 KB = 1 MB
- **Total: ~1.3 MB**

**HTTP server:**
- cpp-httplib base: ~100 KB
- Per-connection overhead: ~50 KB
- **5 clients: ~350 KB**

**Total debug server overhead: ~2 MB** (negligible for debug builds)

## Separation from Game Client/Server

**Critical distinction:**

| System | Purpose | Port | Protocol | Rate |
|--------|---------|------|----------|------|
| Game Server → Game Client | Gameplay | 9000 | WebSocket | 60 Hz |
| Debug Server → Debug Web App | Monitoring | 8080 | SSE | 10 Hz |

**They are completely independent:**
- Debug server observes game server (reads metrics)
- Debug server doesn't affect gameplay
- Can disable debug server with zero impact on game
- Debug server crash doesn't affect game
- Different threads, different ports, different protocols

## Security Considerations

**Localhost-only binding:**
```cpp
m_server.listen("127.0.0.1", port); // NOT "0.0.0.0"!
```

**No authentication:**
- Debug builds are local development only
- Only accessible from same machine
- Developer convenience (no login required)

**CORS enabled** (for local dev):
```cpp
res.set_header("Access-Control-Allow-Origin", "*");
```

**Production safety:**
- Entire debug server compiled out in release builds
- Zero code footprint in shipped binaries
- No accidental exposure

## Future Enhancements

- **WebSocket endpoint**: For bidirectional features (command injection, live editing)
- **Command injection**: Pause game, step frame, trigger events from web app
- **Live component editing**: Modify ECS component values in real-time
- **Recording/playback**: Capture metric timelines, export as JSON, replay later
- **Performance budgets**: Set thresholds, visual alerts when exceeded
- **Multiple game instances**: Dashboard connects to multiple games simultaneously

## Related Documentation

- [Game Design Doc: HTTP Debug Server](/docs/design/features/debug-server/README.md)
- [Logging System](./logging-system.md)
- [Multiplayer Architecture](./multiplayer-architecture.md) - Separate game server protocol
