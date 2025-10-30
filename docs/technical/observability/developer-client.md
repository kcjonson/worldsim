# Developer Client - Technical Design

Created: 2025-10-24
Last Updated: 2025-10-27
Status: Active

## Overview

The developer client is an external TypeScript/Vite web application that connects to the [developer server](./developer-server.md) via HTTP Server-Sent Events (SSE). It provides real-time visualization of metrics, logs, UI state, and profiler data.

**Key Principles:**
- **External**: Runs in a separate browser window - cannot crash or interfere with the application
- **Zero server required**: Built as a single self-contained HTML file that works with `file://` protocol
- **Auto-reconnecting**: Gracefully handles application restarts during development
- **Development-only**: Only built in Development/Debug builds, skipped in Release

## Architecture

```
┌─────────────────────────────────────────────┐
│  Developer Client (Browser - Single File)   │
│                                              │
│  ┌────────────────────────────────────────┐ │
│  │  App Component (Root)                  │ │
│  │  - Connection state management         │ │
│  │  - Multiple SSE stream coordination    │ │
│  └───────────┬────────────────────────────┘ │
│              │                                │
│  ┌───────────▼──────────┐  ┌──────────────┐ │
│  │ MetricsChart         │  │ LogViewer    │ │
│  │ - SVG rendering      │  │ - Filtering  │ │
│  │ - Time series data   │  │ - Auto-scroll│ │
│  └──────────────────────┘  └──────────────┘ │
│                                              │
│  ┌────────────────────────────────────────┐ │
│  │  ServerConnection Service              │ │
│  │  - EventSource wrapper                 │ │
│  │  - Per-stream state tracking           │ │
│  │  - Auto-reconnect on disconnect        │ │
│  └──────────────┬─────────────────────────┘ │
└─────────────────┼──────────────────────────┘
                  │ SSE Streams (EventSource)
                  │
┌─────────────────▼──────────────────────────┐
│  Developer Server (C++ - Port 8080/8081)  │
│  /stream/metrics, /stream/logs, etc.      │
└───────────────────────────────────────────┘
```

**Data Flow:**
1. Developer server streams JSON events via SSE at 10-20 Hz
2. ServerConnection service manages EventSource connections
3. React components subscribe to data streams via callbacks
4. State updates trigger React re-renders for live visualization
5. On disconnect, EventSource auto-reconnects (built-in browser behavior)

## Data Buffering & History Management

### Design Decision: Client-Side Aggregation

**The server does NOT aggregate or store history** - it only streams current values.

**The client maintains all history** for visualization and analysis.

**Rationale:**
- **Server stays lightweight**: No memory overhead, no buffer management, purely stateless streaming
- **Client controls retention**: Change history window without restarting application
- **Multiple clients can differ**: ui-sandbox can show 30s, game server can show 10 minutes
- **Browser has memory**: Development machines have plenty of RAM for tooling
- **Flexibility**: Can add derived metrics (moving averages, percentiles) client-side without C++ changes

**Tradeoffs:**
- Client memory usage increases with retention window (acceptable for dev tool)
- History lost if browser crashes (acceptable - not production monitoring)
- Each client maintains own history (no shared view - acceptable for single developer)

### Configurable Retention Policies

**Metrics (Time-Based):**
- Retention window options: 30s, 60s, 5min, 10min
- At 10 Hz sampling: 300 / 600 / 3000 / 6000 data points respectively
- User selects via dropdown in UI
- Setting persisted in localStorage

**Logs (Count-Based):**
- Entry count options: 500, 1000, 2000, 5000
- Oldest entries discarded when limit exceeded
- User selects via dropdown in UI
- Setting persisted in localStorage

**Why different strategies:**
- Metrics are dense time-series (10 Hz continuous) - time windows make sense
- Logs are sparse events (bursty) - count-based prevents unbounded growth during log spam

### localStorage Persistence Strategy

**Purpose:** Preserve history and configuration across page reloads during development workflow.

**What's persisted:**
```typescript
interface PersistedState {
  metrics: {
    history: Array<{timestamp: number, fps: number, frameTimeMs: number, ...}>,
    retentionWindow: 30 | 60 | 300 | 600  // seconds
  },
  logs: {
    entries: LogEntry[],
    maxEntries: 500 | 1000 | 2000 | 5000
  },
  preferences: {
    logLevelFilter: 'DEBUG' | 'INFO' | 'WARNING' | 'ERROR',
    serverUrl: string
  }
}
```

**Persistence lifecycle:**
1. **On mount**: Read from localStorage, restore history and preferences
2. **During operation**: History maintained in React state (not synced continuously to localStorage)
3. **On unmount/unload**: Write current state to localStorage
4. **On new data**: Old data beyond retention window discarded before persisting

**Cache management and cleanup:**

**Automatic cleanup:**
- **Age-based trimming**: On restore, discard metric samples older than retention window
- **Count-based trimming**: On restore, discard log entries beyond max count
- **Size monitoring**: If serialized JSON exceeds ~5 MB, trim to 50% of limits

**Manual cleanup:**
- UI "Clear History" button: Wipes all persisted data, resets to defaults
- Triggered automatically if localStorage.setItem() throws QuotaExceededError

**Error handling:**
- localStorage disabled (privacy mode): Graceful degradation - app works, just doesn't persist
- localStorage full: Log warning, clear old data, retry
- Corrupt data: JSON.parse fails → clear localStorage, start fresh

**Storage quota:**
- Typical browser limit: 5-10 MB per origin
- Expected usage: 0.5-2 MB typical, < 5 MB maximum
- Well within limits for development tool

### Time-Series Graphing

**Metrics to visualize:**
- **FPS (primary)**: Line graph, target zone highlighting (e.g., 55-65 FPS = green)
- **Frame time**: Line graph with min/max band showing variance
- **Memory**: Line graph with units (MB)
- **Draw calls**: Line graph, lower is better
- **Vertices/Triangles**: Dual-axis or separate graphs

**Graph design:**
- **Rolling time window**: X-axis shows last N seconds (e.g., "60s ago" to "now")
- **Auto-scrolling**: New data appears on right, old data scrolls off left
- **Auto-scaling Y-axis**: Dynamically adjusts to data range with padding
- **Grid lines**: Horizontal lines for Y-axis values, vertical lines for time intervals
- **Legend**: Color-coded labels for each metric series
- **Current value overlay**: Large text showing latest value

**Multi-series support:**
- Can display multiple metrics on one chart (e.g., FPS + frame time)
- Each series has distinct color
- Toggle visibility per series

**SVG rendering approach:**
- Declarative rendering with React
- Grid lines as SVG `<line>` elements
- Metric data as SVG `<polyline>` or `<path>`
- Text overlays as SVG `<text>` elements
- Re-render on data updates (React handles DOM updates)

## Technology Stack

### Core Decisions

**TypeScript + React + Vite:**
- **React**: Component model maps well to real-time data visualization with state updates
- **TypeScript**: Type safety for SSE message contracts (metrics/logs match C++ structs)
- **Vite**: Fast build, outputs static files that work without a web server

**CSS Modules:**
- Component-scoped styling prevents global namespace pollution
- Co-location (Component.tsx + Component.module.css) keeps styles near usage
- No runtime overhead (resolved at build time)

**EventSource API (Native Browser):**
- Server-Sent Events built into all modern browsers
- **Automatic reconnection** - critical for development workflow (restart app → client reconnects)
- No library dependencies needed
- Simpler than WebSocket for one-way streaming

**Custom SVG Rendering:**
- Metrics charts use SVG elements directly (not a charting library)
- Lightweight, declarative, React-friendly
- Scalable without pixelation, inspectable in DevTools
- Sufficient performance for 10 Hz data updates

### Why Single-File Build?

**Problem:** ES modules (`type="module"`) don't work with `file://` protocol due to CORS restrictions.

**Solution:** `vite-plugin-singlefile` inlines all JavaScript and CSS into a single HTML file.

**Benefits:**
- Double-click `index.html` to open - no `python -m http.server` needed
- No external dependencies - completely self-contained
- Works offline, no CORS issues
- Still get all Vite optimizations (minification, tree-shaking)

**Tradeoff:** Larger file size (~150-600 KB) but negligible for development tooling.

## Project Structure

```
apps/developer-client/
  src/
    main.tsx                         # Entry point - ReactDOM.render()
    App.tsx                          # Root component - manages all SSE connections
    App.module.css                   # Root layout and theme

    components/                      # (Planned) Future components
      MetricsChart.tsx               # SVG-based time-series chart
      LogViewer.tsx                  # Log display with level/category filtering
      ProfilerView.tsx               # Flame graph for profiler data
      UIHierarchyTree.tsx            # Tree view of UI scene graph
      HoverInspector.tsx             # Visual layer stack + component hierarchy

    services/
      ServerConnection.ts            # EventSource wrapper with state tracking
      DataStore.ts                   # (Planned) Client-side buffering

    styles/
      globals.css                    # CSS variables, dark theme, reset

  vite.config.ts                     # Build config with vite-plugin-singlefile
  package.json                       # React 18, TypeScript, Vite dependencies
  tsconfig.json                      # Strict mode, React JSX
  index.html                         # Vite entry point
```

**Key Files:**
- **ServerConnection.ts**: Wraps browser EventSource API, manages multiple stream types, tracks connection state per stream
- **App.tsx**: Coordinates all SSE streams, manages global state (metrics, logs, etc.), provides data to child components
- **vite.config.ts**: Configures single-file build with `viteSingleFile()` plugin
- **CSS Modules**: Each component has co-located `.module.css` for scoped styles

## Key Design Patterns

### SSE Connection Management

The ServerConnection service wraps the browser's EventSource API to manage multiple concurrent SSE streams with state tracking:

**Core pattern:**
```typescript
// EventSource wrapper manages per-stream state
class ServerConnection {
  private streams: Map<StreamType, EventSource>;

  connect(streamType: 'metrics' | 'logs' | 'ui', config: {
    endpoint: string,
    eventType: string,
    handler: (data: any) => void,
    onConnect?: () => void,
    onDisconnect?: () => void
  })
}
```

**Key behaviors:**
- One EventSource per stream type (metrics, logs, UI, hover, etc.)
- Track connection state per stream (for UI indicators)
- EventSource **auto-reconnects** on error - no manual retry logic needed
- JSON parsing happens in the service, components get typed data
- Cleanup on unmount prevents memory leaks

### React State Management

App component coordinates all streams and distributes data to child components:

**Pattern:**
```typescript
function App() {
  const [metrics, setMetrics] = useState(initialMetrics);
  const [logs, setLogs] = useState<LogEntry[]>([]);

  useEffect(() => {
    const connection = new ServerConnection(serverUrl);

    // Subscribe to metrics stream
    connection.connect('metrics', {
      endpoint: '/stream/metrics',
      handler: (data) => setMetrics(data)  // React re-render
    });

    // Subscribe to logs stream with buffering
    connection.connect('logs', {
      endpoint: '/stream/logs',
      handler: (log) => setLogs(prev => [...prev, log].slice(-1000))
    });

    return () => connection.disconnectAll();  // Cleanup
  }, []);

  return <MetricsChart metrics={metrics} />;  // Props down
}
```

**Design decisions:**
- State lives in App component (single source of truth)
- Children receive data via props (unidirectional data flow)
- Log buffer limited to 1000 entries (prevents browser memory issues)
- useEffect cleanup prevents EventSource leaks on unmount

## Component Responsibilities

### MetricsChart
**Purpose:** Real-time visualization of performance metrics with configurable history and localStorage persistence

**Data Management:**
- **Circular buffer**: Fixed-size rolling window of metric samples
- **Configurable retention**: 30s / 60s / 5min / 10min (300 / 600 / 3000 / 6000 samples at 10 Hz)
- **localStorage integration**:
  - On mount: Restore history from localStorage, discard stale samples
  - During operation: Maintain in React state
  - On unmount: Persist current history to localStorage
- **Efficient updates**: Circular buffer avoids array shifting (O(1) insert)

**Visualization:**
- SVG elements for declarative rendering (no charting library)
- Rolling time-series graph (X-axis = time, auto-scrolling)
- Auto-scaling Y-axis based on min/max values in current window
- Grid lines as SVG elements for readability
- Current value overlay (text showing latest metric)
- Multi-series support (can show FPS + frame time on same graph)

**Implementation approach:**
- useEffect for data buffering (handles SSE updates, maintains circular buffer)
- React re-render triggers SVG updates
- Dropdown UI to change retention window
- "Clear History" button to reset buffer

**Key decisions:**
- SVG over Canvas: Declarative, React-friendly, inspectable, scalable, sufficient for 10 Hz updates
- Circular buffer over array: O(1) insert, fixed memory
- localStorage on mount/unmount only: Avoid blocking I/O during operation
- Auto-scaling: Graph adapts to data range (e.g., FPS 30-60 vs 0-100)

### LogViewer
**Purpose:** Display real-time log stream with filtering, search, and localStorage persistence

**Data Management:**
- **Array-based storage**: Logs are sequential events (not circular - order matters)
- **Configurable count limit**: 500 / 1000 / 2000 / 5000 entries
- **localStorage integration**:
  - On mount: Restore log entries from localStorage
  - During operation: Append new logs to array
  - On unmount: Persist entries to localStorage (trimmed to limit)
- **Automatic trimming**: When limit exceeded, discard oldest entries

**Display Features:**
- Filter by log level (Debug+ / Info+ / Warning+ / Error only)
- Text search across messages (case-insensitive)
- Auto-scroll when scrolled to bottom (preserves position otherwise)
- Color-coded by level (DEBUG gray, INFO white, WARNING yellow, ERROR red)
- Shows file:line for warnings/errors
- Entry count display in header

**Implementation approach:**
- Single useEffect for auto-scroll detection
- Filter on render (Array.filter - fast enough for < 10k logs)
- Dropdown UI to change count limit
- Search input with debounced filtering

**Key decisions:**
- Array (not circular buffer): Logs are historical record, order crucial
- Count-based limit: Prevents unbounded growth during log spam
- Filter on render: Simple, no premature optimization
- Auto-scroll detection: Only scroll if user is at bottom (preserves manual scrolling)
- localStorage persistence: Survive page reload during long debugging sessions

### UIHierarchyTree (Planned)
**Purpose:** Display scene graph structure from UI inspection stream

**Approach:**
- Recursive tree component with expand/collapse
- Indent by depth, color-code by component type
- Click to highlight in application (future: bidirectional)

### HoverInspector (Planned)
**Purpose:** Show visual stack and component hierarchy for hovered element

**Two views:**
1. **Visual Stack**: Z-index ordered list of rendered layers
2. **Component Hierarchy**: Parent → child component chain from root

**Activated:** When F3 is pressed in application, `/stream/hover` becomes active

## Build Integration

**CMake integration:**
- Developer client builds automatically during `make` in Development/Debug builds
- CMake custom target runs `npm install` && `npm run build`
- Output copied from `apps/developer-client/dist/` to `build/developer-client/`
- Applications (`world-sim-server`, `ui-sandbox`) depend on `developer-client` target
- Skipped entirely in Release builds

**Build output:**
- Single self-contained HTML file (~150-600 KB depending on features)
- All JavaScript and CSS inlined via `vite-plugin-singlefile`
- Works with `file://` protocol - no server needed
- Inline source maps for debugging (optional)

**Vite configuration highlights:**
```typescript
// vite.config.ts
export default defineConfig({
  plugins: [react(), viteSingleFile()],  // Inline everything
  build: {
    cssCodeSplit: false,              // Required for single-file
    assetsInlineLimit: 100000000,     // Inline all assets
    sourcemap: 'inline'               // Debugging without .map files
  }
});
```

**Usage:**
```bash
# Build (automatic with make)
cd build && make

# Run application (developer server starts)
./apps/ui-sandbox/ui-sandbox

# Open developer client (no server needed)
open build/developer-client/index.html
```

## Styling Approach

**CSS Variables for theming:**
- Dark theme with monospace font (developer tool aesthetic)
- Global variables in `globals.css`: `--bg-primary`, `--text-primary`, `--accent-green`, etc.
- Consistent color palette across all components

**CSS Modules for component styles:**
- Each component has co-located `.module.css` file
- Vite scopes class names automatically (prevents conflicts)
- Pattern: `import styles from './Component.module.css'`
- Usage: `className={styles.container}`

**Why this approach:**
- Global theme variables provide consistency
- CSS Modules prevent class name collisions
- Co-location keeps component code together
- No runtime overhead (build-time scoping)

## Performance & Storage Considerations

### Memory Usage Calculations

**Metrics history:**
- Single metric sample: ~64 bytes (timestamp, fps, frameTime, drawCalls, vertices, triangles, etc.)
- Retention windows at 10 Hz:
  - 30s: 300 samples × 64 bytes = ~19 KB
  - 60s: 600 samples × 64 bytes = ~38 KB
  - 5min: 3000 samples × 64 bytes = ~188 KB
  - 10min: 6000 samples × 64 bytes = ~375 KB
- **Total for all metrics**: < 400 KB maximum

**Logs:**
- Single log entry: ~256 bytes (level, category, message, timestamp, file, line)
- Count limits:
  - 500 entries: ~125 KB
  - 1000 entries: ~250 KB
  - 2000 entries: ~500 KB
  - 5000 entries: ~1.25 MB
- **Total for logs**: < 1.5 MB maximum

**Overall memory footprint:**
- In-memory (React state): < 2 MB typical, < 5 MB maximum
- localStorage (persisted): Same as in-memory (JSON serialization overhead ~10%)
- **Well within browser limits** (~5-10 MB localStorage quota)

### localStorage Performance

**Read/Write frequency:**
- **Read**: Once on mount (~10-50ms for 2 MB JSON.parse)
- **Write**: Once on unmount/unload (~10-50ms for 2 MB JSON.stringify)
- **Not during operation**: No localStorage I/O in hot path

**Serialization strategy:**
- Use JSON.stringify/parse (built-in, fast enough)
- No compression needed (data is small)
- Atomic write: Single localStorage.setItem call

**Error handling:**
- localStorage disabled: App works without persistence (graceful degradation)
- QuotaExceededError: Trim to 50% of limits, retry
- JSON.parse failure: Clear localStorage, start fresh with empty state

### SVG Rendering Performance

**Target performance:**
- Chart update rate: 10 Hz (matches SSE data rate)
- SVG re-render via React: Negligible overhead for simple graphs
- No performance impact on application being monitored

**Optimization strategies:**
- **React handles updates**: Only re-renders when data changes
- **Circular buffer**: O(1) insert, no array shifting
- **Simple SVG primitives**: Lines, polylines, text (no complex paths)
- **Fixed viewBox**: SVG scales automatically without recalculation

**Performance characteristics:**
- 600 data points (60s window): Renders instantly (< 1ms)
- 6000 data points (10min window): Still performant (< 5ms)
- SVG DOM is lightweight for time-series graphs (< 100 elements total)

### Circular Buffer Data Structure

**Why circular buffer for metrics:**
```
Fixed-size array with head/tail pointers:
- Insert: O(1) - write to head, advance pointer
- Read: O(n) - iterate from tail to head
- Memory: Fixed - no allocations during operation
- No shifting: Unlike array, oldest data just gets overwritten
```

**Example with capacity 5:**
```
Initial: [_, _, _, _, _] head=0, tail=0
After 3: [A, B, C, _, _] head=3, tail=0
After 7: [F, G, C, D, E] head=2, tail=2  (wraps around, C overwritten)
```

**Why array for logs:**
```
Logs are historical record - order matters, can't overwrite:
- Append: O(1) - push to end
- Trim: O(n) - slice when over limit
- Simpler: No head/tail pointer management
- Search: O(n) - but needed for text search anyway
```

### Storage Quota Management

**Browser localStorage limits:**
- Chrome/Edge: ~10 MB
- Firefox: ~10 MB
- Safari: ~5 MB
- **Design target**: < 5 MB (works on all browsers)

**Automatic size management:**
```typescript
// Conceptual approach
function persistState(state: PersistedState) {
  const json = JSON.stringify(state);

  // Check size before persisting
  if (json.length > 5 * 1024 * 1024) {  // 5 MB
    // Trim to 50% of limits
    state.metrics.history = state.metrics.history.slice(-300);  // Keep last 30s
    state.logs.entries = state.logs.entries.slice(-500);        // Keep last 500
  }

  try {
    localStorage.setItem('developer-client', json);
  } catch (e) {
    if (e.name === 'QuotaExceededError') {
      // Clear and retry with minimal state
      localStorage.clear();
      localStorage.setItem('developer-client', JSON.stringify({...state, metrics: {history: []}, logs: {entries: []}}));
    }
  }
}
```

**Manual cleanup:**
- "Clear History" button: Wipes all persisted data
- "Reset to Defaults" button: Clears localStorage, resets retention settings
- Automatic on quota error: Trim aggressively and retry

## Key Takeaways

**Why external web app?**
- Zero in-game performance overhead
- Cannot crash or freeze the application
- Browser DevTools for debugging
- Modern web tech (React, TypeScript) for rapid UI development

**Why single-file build?**
- No web server needed during development
- Works with `file://` protocol
- Portable - can email the HTML file
- Still get Vite optimizations (minification, tree-shaking)

**Why EventSource over WebSocket?**
- Built-in auto-reconnect (critical for dev workflow)
- Simpler protocol (one-way streaming)
- No handshake complexity
- Sufficient for monitoring use case

**Why client-side history aggregation?**
- Server stays stateless and lightweight (just streams current values)
- Client controls retention without restarting application
- Multiple clients can have different policies
- Browser has sufficient memory for dev tooling
- Enables client-side analytics (moving averages, percentiles, etc.)

**Why localStorage persistence?**
- Preserves history across page reloads (common during development)
- Survives application restarts while debugging
- Remembers user preferences (retention windows, filters)
- Graceful degradation if disabled (app still works)
- Well within browser storage limits (< 5 MB)

**Why circular buffer for metrics?**
- O(1) insert performance (no array shifting)
- Fixed memory usage (no unbounded growth)
- Perfect fit for rolling time windows
- Simple implementation (head/tail pointers)

**Why array for logs?**
- Logs are historical record (order matters, can't overwrite)
- Text search requires full scan anyway (O(n) unavoidable)
- Simpler implementation (no pointer management)
- Trim when limit exceeded (acceptable O(n) operation)

**Why SVG for charts?**
- Declarative rendering (React-friendly)
- Scalable without pixelation
- Inspectable in browser DevTools
- Sufficient performance for 10 Hz updates
- Simpler than Canvas for simple line graphs

**Why CSS Modules?**
- Prevents class name conflicts
- Co-location with components
- No runtime overhead
- Type-safe with proper TypeScript config

## Related Documentation

- [Developer Server](./developer-server.md) - C++ SSE streaming implementation
- [UI Inspection](./ui-inspection.md) - UI hierarchy and hover data format
- [Observability INDEX](./INDEX.md) - Complete observability system overview
- [Logging System](../logging-system.md) - Log format and streaming design
