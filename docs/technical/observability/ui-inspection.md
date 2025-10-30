# UI Inspection

Created: 2025-10-12
Last Updated: 2025-10-24
Status: Active

## Context

In a previous iteration of this project, the UI became untestable by AI agents because there was no way to inspect or verify changes without manual visual inspection. This is a major problem for C++ applications where there's no built-in inspector like web browsers provide.

**Goal:** Make the C++ UI as inspectable and testable as a web application.

**Architecture Decision:** Follow the same HTTP debug server pattern established in [developer-server.md](./developer-server.md). All UI inspection happens in an external web application connected via HTTP with Server-Sent Events (SSE). **No in-game overlays** - they impact performance and can break the game.

## Application Scope

**Runs in:**
- Main game client (Development builds only)
- `ui-sandbox` application (Development builds only)

**Build Requirement:**
- Compiled in: `DEVELOPMENT_BUILD` flag set
- Compiled out: Release builds (zero code footprint)

**Runtime Control:**
- HTTP server: Auto-starts on application launch in Development builds
- Hover inspection: Toggled via **F3 key** (disabled by default for performance)

## Decision

Implement testability through HTTP server with SSE streaming, following the architecture pattern from [developer-server.md](./developer-server.md):

### 1. Scene Graph Serialization

Export the complete UI hierarchy to JSON at any point in time.

**What's captured:**
```json
{
  "type": "Container",
  "id": "main_menu",
  "visible": true,
  "bounds": {"x": 0, "y": 0, "width": 1920, "height": 1080},
  "children": [
    {
      "type": "Button",
      "id": "create_world_btn",
      "text": "Create World",
      "enabled": true,
      "visible": true,
      "bounds": {"x": 860, "y": 400, "width": 200, "height": 60}
    }
  ]
}
```

**API:**
```cpp
std::string ui::SceneGraph::toJSON();
ui::Element* ui::SceneGraph::findById(const std::string& id);
```

### 2. HTTP Debug Server with SSE Streaming

Embed HTTP server (using cpp-httplib) in the UI application following the same threading model as [developer-server.md](./developer-server.md).

**Threading Model:**
- **Game/UI Thread**: Updates UI at 60 FPS, writes UI state to lock-free ring buffers
- **HTTP Server Thread**: Reads from ring buffers at 10 Hz, streams via SSE to external web app
- **Critical**: UI thread never blocks on network I/O

**SSE Stream Endpoints:**
- `GET /stream/ui` - Real-time UI hierarchy updates (10 Hz)
- `GET /stream/hover` - Mouse hover inspection data (10 Hz, F3 to enable)
- `GET /stream/events` - UI events (clicks, hovers, focus changes) (20 Hz)

**REST Endpoints (Snapshots):**
- `GET /api/ui/tree` - Current UI hierarchy as JSON
- `GET /api/ui/element/:id` - Specific element properties
- `GET /api/ui/screenshot` - PNG screenshot (uses same system as visual regression testing)
- `POST /api/ui/click` - Simulate click at coordinates or element ID
- `POST /api/ui/type` - Simulate keyboard input

**SSE Event Format:**
```
event: ui_update
data: {"type": "hierarchy", "root": {...}, "timestamp": 1729800000}

event: hover
data: {"visualStack": [...], "componentHierarchy": [...], "position": {"x": 450, "y": 200}}

event: ui_event
data: {"type": "click", "elementId": "create_world_btn", "timestamp": 1729800001}
```

**External Web App:**
- Separate browser window connects to `http://localhost:8081` (ui-sandbox) or `http://localhost:8082` (main game)
- Displays live UI hierarchy tree
- Shows hover inspection with visual layer stack and component hierarchy
- No in-game rendering - all visualization in external app

This allows AI agents (like Claude) to inspect and interact with the UI programmatically.

### 3. Visual Regression Testing

Compare screenshots against golden images using perceptual hashing or pixel-perfect comparison.

**Process:**
1. Capture "golden" screenshots of UI states
2. Store in `tests/golden/`
3. During tests, capture new screenshots
4. Compare using image diff tools (e.g., libpng + custom diff)
5. Fail test if difference exceeds threshold

**API:**
```cpp
testing::captureGolden("main_menu");
testing::compareWithGolden("main_menu", threshold=0.01);
```

### 4. Mouse Hover Inspection (F3 Toggle)

Track what UI elements are under the mouse cursor and stream data to external debug app.

**Two Data Views:**

1. **Visual Layer Stack** (z-index order, what you see):
```json
{
  "visualStack": [
    {"type": "Rectangle", "id": "background", "zIndex": 0, "bounds": {...}},
    {"type": "Rectangle", "id": "button_bg", "zIndex": 10, "bounds": {...}},
    {"type": "Text", "id": "button_label", "zIndex": 11, "bounds": {...}}
  ]
}
```

2. **Component Hierarchy** (logical structure):
```json
{
  "componentHierarchy": [
    {"type": "World", "id": "main_world"},
    {"type": "Scene", "id": "main_menu"},
    {"type": "Container", "id": "button_container"},
    {"type": "Button", "id": "create_world_btn"},
    {"type": "Rectangle", "id": "button_bg"}
  ]
}
```

**Performance Control:**
- **F3 key toggles** hover data collection on/off
- Disabled by default (collecting hover data has overhead)
- When enabled: collects data every frame, streams at 10 Hz via SSE
- When disabled: zero overhead

**Implementation:**
- UI thread: Hit-test under mouse cursor, write to ring buffer (fast)
- HTTP thread: Read latest from buffer, stream via `/stream/hover` endpoint
- External app: Displays both views in real-time as mouse moves

### 5. UI Event Streaming

Stream UI interactions to external debug app for monitoring and testing.

**Events captured:**
- Clicks (element ID, position, timestamp)
- Key presses (key, modifiers, focused element)
- Mouse moves (position, elements under cursor)
- Focus changes (from/to elements)
- UI state changes (element visibility, enabled state)

**Streaming:**
- Events written to ring buffer by UI thread
- HTTP thread reads and streams via `/stream/events` (20 Hz, throttled)
- External app displays event log with filtering/search

**Use Cases:**
- Monitor UI interactions during testing
- Record event sequences for regression tests (future enhancement)
- Debug event handling issues
- Verify AI agent interactions

## Implementation Details

### Threading Model & Ring Buffers

Follow the same lock-free architecture as [developer-server.md](./developer-server.md):

```cpp
namespace ui {

// Lock-free ring buffer for UI state updates
template<typename T, size_t N>
class LockFreeRingBuffer {
    std::array<T, N> m_buffer;
    std::atomic<size_t> m_writeIndex{0};
    std::atomic<size_t> m_readIndex{0};

public:
    void Write(const T& item);  // UI thread, never blocks
    bool ReadLatest(T& item);   // HTTP thread
};

// Global ring buffers
inline LockFreeRingBuffer<UIStateSnapshot, 1000> g_uiStateBuffer;
inline LockFreeRingBuffer<HoverData, 100> g_hoverBuffer;
inline LockFreeRingBuffer<UIEvent, 1000> g_eventBuffer;

} // namespace ui
```

### Core Classes

```cpp
namespace ui {

class DebugServer {
public:
    void Start(int port);  // 8081 for ui-sandbox, 8082 for main game
    void Stop();
    bool IsRunning() const;

private:
    httplib::Server m_server;
    std::thread m_serverThread;
    std::atomic<bool> m_running{false};

    void HandleUIStream(httplib::DataSink& sink);       // 10 Hz
    void HandleHoverStream(httplib::DataSink& sink);    // 10 Hz
    void HandleEventStream(httplib::DataSink& sink);    // 20 Hz
};

class Inspector {
public:
    // Snapshot current UI state to ring buffer (called by UI thread)
    static void CaptureUIState();

    // Capture hover data if F3 enabled (called by UI thread)
    static void CaptureHoverData(int mouseX, int mouseY);

    // Log UI event to ring buffer (called by UI thread)
    static void LogEvent(const UIEvent& event);

    // Export full hierarchy to JSON (for REST endpoint)
    static std::string ExportToJSON();

    // F3 toggle state
    static bool IsHoverInspectionEnabled();
    static void SetHoverInspectionEnabled(bool enabled);
};

class Element {
public:
    virtual std::string toJSON() const;
    std::string getId() const;
    Bounds getBounds() const;
    bool isVisible() const;
    int getZIndex() const;
    // ... other properties
};

} // namespace ui
```

### Integration with ui-sandbox

The `ui-sandbox` application runs HTTP debug server on port 8081:

```bash
# Run ui-sandbox (HTTP server auto-starts in Development builds)
./ui-sandbox --component button

# Open developer client in browser
open http://localhost:8081

# Press F3 in ui-sandbox to enable hover inspection
```

**Automated Testing:**
```bash
# Connect via REST API for automated tests
curl http://localhost:8081/api/ui/tree

# Click a button
curl -X POST http://localhost:8081/api/ui/click -d '{"id":"test_button"}'

# Take screenshot for visual regression
curl http://localhost:8081/api/ui/screenshot > test.png
```

**For main game client:**
```bash
# Run main game (HTTP server auto-starts in Development builds)
./world-sim

# Open developer client in browser
open http://localhost:8082

# Press F3 in game to enable hover inspection
```

### Developer Client (External Web App)

Same architecture as developer server - TypeScript/Vite app served by HTTP server:

**Features:**
- Live UI hierarchy tree view (updates at 10 Hz via SSE)
- Hover inspection panel (visual stack + component hierarchy)
- Event log viewer with filtering
- Element properties inspector
- Screenshot capture

**Development:**
```bash
cd debug-app
npm run dev
# Hot reload, connects to game/ui-sandbox on localhost
```

**Production (bundled with game):**
```bash
npm run build
# Outputs to debug-app/dist/
# HTTP server serves static files from /
```

## Performance Considerations

**Overhead Budget (Development builds only):**

**When F3 disabled (default):**
- UI state snapshots: ~0.1ms per frame (60 Hz writes to ring buffer)
- HTTP server idle: ~0.01ms
- **Total: ~0.1ms per frame** ✓

**When F3 enabled (hover inspection):**
- UI state snapshots: ~0.1ms
- Hover hit-testing: ~0.2ms per frame
- HTTP SSE streaming: async on server thread (doesn't block UI thread)
- **Total: ~0.3ms per frame** ✓

**Memory Usage:**
- Ring buffers: ~1 MB (UI state + hover data + events)
- HTTP server: ~350 KB (cpp-httplib + connections)
- **Total: ~1.5 MB**

**Release Builds:**
- Entire system compiled out (`#ifdef DEVELOPMENT_BUILD`)
- Zero code footprint
- Zero overhead

## Trade-offs

**Pros:**
- AI agents can test UI changes automatically
- No in-game overlays (no performance impact on game rendering)
- External app can't crash or break the game
- Faster iteration cycle (no manual visual verification)
- Regression tests catch UI bugs
- Can inspect UI while game is running

**Cons:**
- Additional complexity in UI library (~500 lines for HTTP server integration)
- HTTP server dependency (cpp-httplib, already used by game server)
- Screenshot tests can be brittle
- Performance overhead in Development builds (acceptable)
- Separate window for debugging (not integrated in game)

**Decision:** The testability benefits far outweigh the complexity cost. Following the proven developer-server.md architecture ensures consistency.

## Build Configuration

**Development Build** (`-DCMAKE_BUILD_TYPE=Development`):
- `DEVELOPMENT_BUILD` flag set
- HTTP debug server compiled in
- UI inspection enabled
- Hover tracking available (F3 toggle)
- ~1.5 MB memory overhead
- ~0.1-0.3ms per frame overhead

**Release Build** (`-DCMAKE_BUILD_TYPE=Release`):
- `DEVELOPMENT_BUILD` not defined
- Entire system compiled out via `#ifdef`
- Zero code footprint in binary
- Zero runtime overhead
- No ports opened, no network code

**CMake Integration:**
```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Development")
    target_compile_definitions(ui PRIVATE DEVELOPMENT_BUILD)
    target_link_libraries(ui PRIVATE httplib::httplib)
endif()
```

## Alternatives Considered

### Option: In-Game Inspector Overlay (F12)
**Rejected** - Performance overhead, can break rendering, clutters viewport. External app is cleaner separation.

### Option: Manual Testing Only
**Rejected** - This is what failed in the previous iteration. Not acceptable.

### Option: Unit Tests Only (No Visual Testing)
**Rejected** - UI bugs are often visual. Need screenshot comparison and live inspection.

### Option: External Testing Framework (Selenium-style)
**Rejected** - External tools can't access internal C++ state easily. Embedding HTTP server is more flexible.

## Cross-Reference: Debug Systems

| System | Purpose | Access Method | In-Game UI | Availability |
|--------|---------|---------------|------------|--------------|
| **UI Inspection** (this doc) | Inspect UI hierarchy, hover data | HTTP SSE `/stream/ui`, `/stream/hover` | No (external app) | Development builds |
| [Logging](../logging-system.md) | Diagnostic messages | Console/file + SSE `/stream/logs` | No | All builds (Error only in Release) |
| [Diagnostic Drawing](../diagnostic-drawing.md) | Manual visual debugging | `DebugDraw::*()` in code | Yes (viewport) | Development builds |
| [Developer Server](./developer-server.md) | Application monitoring | SSE (metrics, profiler) | No (external) | Development builds |

**Key Distinctions:**
- **UI Inspection** (F3 toggle): Inspect what's under mouse cursor, stream UI state
- **Diagnostic Drawing**: Developer draws temporary lines/boxes in viewport during manual debugging
- **Logging**: Text diagnostic output (console, file, HTTP stream)
- **Developer Server**: Monitor application state (ECS, chunks, performance, logs)

## Related Documentation

- **Architecture**: [Developer Server](./developer-server.md) - SSE streaming, ring buffers
- **Architecture**: [Developer Client](./developer-client.md) - External web application
- **Related Systems**: [Logging System](../logging-system.md) - Integrates with log streaming
- **Related Systems**: [Diagnostic Drawing](../diagnostic-drawing.md) - In-viewport debugging
- [INDEX](./INDEX.md) - Observability system overview
- **Design Doc**: [UI Sandbox Application](/docs/design/features/ui-framework/ui-sandbox.md) (if exists)
- **Code**: `libs/ui/include/ui/inspector/` (once implemented)

## Notes

**Implementation Priority:**
1. **Phase 1**: Scene graph JSON serialization + HTTP server with SSE
2. **Phase 2**: Mouse hover inspection (F3 toggle) + visual/component hierarchy
3. **Phase 3**: External web debug app (TypeScript/Vite)
4. **Phase 4**: Visual regression testing

Start with basic HTTP server and SSE streaming - this provides the most value with least complexity. Build external web app progressively.
