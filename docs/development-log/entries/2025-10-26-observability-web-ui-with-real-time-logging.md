# Observability Web UI with Real-Time Logging

**Date:** 2025-10-26

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


