# Developer Client (Web Application)

Created: 2025-10-24
Status: Active

## Overview

The developer client is an external TypeScript/Vite web application that connects to the [developer server](./developer-server.md) via HTTP Server-Sent Events (SSE). It provides real-time visualization of metrics, logs, UI state, and profiler data.

**Key Principle:** Runs in a separate browser window. Cannot crash or interfere with the running application.

## Technology Stack

- **Build Tool**: Vite (static bundle generation, TypeScript support)
- **Language**: TypeScript (type safety, modern features)
- **UI Framework**: React (component reusability, strong ecosystem)
- **Styling**: CSS Modules (co-located with components, scoped styles)
- **Charts**: Custom Canvas rendering or React-based visualization
- **SSE Client**: Native browser `EventSource` API (built-in, no library needed)

**Why this stack:**
- **Vite**: Fast build tool that outputs static files with relative paths - can be opened directly in browser
- **React**: Component model fits well with real-time data visualization and updates
- **CSS Modules**: Scoped styling prevents conflicts, co-located with components for maintainability
- **EventSource**: Built into all browsers, handles reconnection automatically

**Build Output:** Static HTML/JS/CSS bundle in `build/developer-client/` that can be opened by clicking `index.html` - no server required.

## Project Structure

```
apps/developer-client/               # Root folder for app
  index.html                         # HTML entry point (Vite requires this at root)

  src/
    main.tsx                         # Entry point, React root render
    App.tsx                          # Root React component
    App.module.css                   # Root component styles
    vite-env.d.ts                    # Vite type declarations (CSS Modules)

    components/
      MetricsChart.tsx               # FPS/memory line chart component
      MetricsChart.module.css        # MetricsChart styles (co-located)

      LogViewer.tsx                  # Real-time log display with filtering
      LogViewer.module.css           # LogViewer styles (co-located)

      ProfilerView.tsx               # Flame graph rendering
      ProfilerView.module.css        # ProfilerView styles (co-located)

      UIHierarchyTree.tsx            # UI element tree view
      UIHierarchyTree.module.css     # UIHierarchyTree styles (co-located)

      HoverInspector.tsx             # Visual stack + component hierarchy
      HoverInspector.module.css      # HoverInspector styles (co-located)

    services/
      ServerConnection.ts            # SSE connection management
      DataStore.ts                   # Client-side data buffering

    styles/
      globals.css                    # Global styles, CSS variables, dark theme

  public/                            # Static assets (copied as-is to dist/)

  vite.config.ts                     # Vite configuration (with base: "./")
  package.json                       # Dependencies (includes React)
  tsconfig.json                      # TypeScript configuration
```

**CSS Modules Naming:** Each component has a co-located `.module.css` file with the same base name. Vite automatically scopes these styles to prevent conflicts.

## SSE Connection Management

### Server Connection Service

```typescript
// src/services/ServerConnection.ts

type StreamType = 'metrics' | 'logs' | 'ui' | 'hover' | 'events' | 'profiler';

interface StreamConfig {
    endpoint: string;
    eventType: string;
    handler: (data: any) => void;
}

class ServerConnection {
    private serverUrl: string;
    private streams: Map<StreamType, EventSource> = new Map();
    private reconnectAttempts: Map<StreamType, number> = new Map();

    constructor(serverUrl: string) {
        this.serverUrl = serverUrl;
    }

    connect(streamType: StreamType, config: StreamConfig): void {
        const url = `${this.serverUrl}${config.endpoint}`;
        const source = new EventSource(url);

        source.addEventListener(config.eventType, (event) => {
            const data = JSON.parse(event.data);
            config.handler(data);
        });

        source.addEventListener('open', () => {
            console.log(`[${streamType}] Connected`);
            this.reconnectAttempts.set(streamType, 0);
        });

        source.addEventListener('error', () => {
            const attempts = this.reconnectAttempts.get(streamType) || 0;
            console.log(`[${streamType}] Disconnected (attempt ${attempts + 1})`);
            this.reconnectAttempts.set(streamType, attempts + 1);
            // EventSource automatically reconnects!
        });

        this.streams.set(streamType, source);
    }

    disconnect(streamType: StreamType): void {
        const source = this.streams.get(streamType);
        if (source) {
            source.close();
            this.streams.delete(streamType);
        }
    }

    disconnectAll(): void {
        this.streams.forEach(source => source.close());
        this.streams.clear();
    }
}
```

### Usage in Main Application

```typescript
// src/main.tsx
import React from 'react';
import ReactDOM from 'react-dom/client';
import App from './App';
import './styles/globals.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
);
```

```typescript
// src/App.tsx
import { useEffect, useState } from 'react';
import { ServerConnection } from './services/ServerConnection';
import MetricsChart from './components/MetricsChart';
import LogViewer from './components/LogViewer';
import UIHierarchyTree from './components/UIHierarchyTree';
import HoverInspector from './components/HoverInspector';
import styles from './App.module.css';

const serverUrl = 'http://localhost:8081'; // Or 8080, 8082 for other apps

function App() {
  const [metrics, setMetrics] = useState({ fps: 0, frameTimeMs: 0, memoryMB: 0 });
  const [logs, setLogs] = useState<LogEntry[]>([]);
  const [uiHierarchy, setUiHierarchy] = useState(null);
  const [hoverData, setHoverData] = useState(null);

  useEffect(() => {
    const connection = new ServerConnection(serverUrl);

    // Connect to metrics stream
    connection.connect('metrics', {
      endpoint: '/stream/metrics',
      eventType: 'metric',
      handler: (data) => setMetrics(data),
    });

    // Connect to logs stream
    connection.connect('logs', {
      endpoint: '/stream/logs',
      eventType: 'log',
      handler: (data) => setLogs(prev => [...prev, data].slice(-1000)),
    });

    // Connect to UI hierarchy stream
    connection.connect('ui', {
      endpoint: '/stream/ui',
      eventType: 'ui_update',
      handler: (data) => setUiHierarchy(data.root),
    });

    // Connect to hover inspection (when F3 is enabled in app)
    connection.connect('hover', {
      endpoint: '/stream/hover',
      eventType: 'hover',
      handler: (data) => setHoverData(data),
    });

    return () => connection.disconnectAll();
  }, []);

  return (
    <div className={styles.appContainer}>
      <header className={styles.header}>
        <h1>Developer Client - {serverUrl}</h1>
      </header>
      <div className={styles.leftPanel}>
        <MetricsChart metrics={metrics} />
        <UIHierarchyTree hierarchy={uiHierarchy} />
      </div>
      <div className={styles.rightPanel}>
        <LogViewer logs={logs} />
        {hoverData && <HoverInspector data={hoverData} />}
      </div>
    </div>
  );
}

export default App;
```

## React Components

### Metrics Line Chart

```typescript
// src/components/MetricsChart.tsx
import { useEffect, useRef, useState } from 'react';
import styles from './MetricsChart.module.css';

interface MetricsChartProps {
  metrics: {
    fps: number;
    frameTimeMs: number;
    memoryMB: number;
  };
}

interface DataPoint {
  timestamp: number;
  value: number;
}

const MetricsChart: React.FC<MetricsChartProps> = ({ metrics }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [dataPoints, setDataPoints] = useState<DataPoint[]>([]);
  const maxPoints = 300; // 30 seconds at 10 Hz

  // Add new data points when metrics change
  useEffect(() => {
    setDataPoints(prev => {
      const newPoint = { timestamp: Date.now(), value: metrics.fps };
      const updated = [...prev, newPoint];
      return updated.slice(-maxPoints); // Keep only last N points
    });
  }, [metrics]);

  // Render chart when data changes
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const width = canvas.width;
    const height = canvas.height;
    const min = 0;
    const max = 100;

    // Clear canvas
    ctx.fillStyle = '#1a1a1a';
    ctx.fillRect(0, 0, width, height);

    // Draw grid
    ctx.strokeStyle = '#333';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 10; i++) {
      const y = (i / 10) * height;
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(width, y);
      ctx.stroke();
    }

    // Draw data line
    if (dataPoints.length > 1) {
      ctx.beginPath();
      ctx.strokeStyle = '#00ff00';
      ctx.lineWidth = 2;

      dataPoints.forEach((point, i) => {
        const x = (i / dataPoints.length) * width;
        const normalizedValue = (point.value - min) / (max - min);
        const y = height - (normalizedValue * height);

        if (i === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      });

      ctx.stroke();
    }

    // Draw current value
    if (dataPoints.length > 0) {
      const latest = dataPoints[dataPoints.length - 1];
      ctx.fillStyle = '#fff';
      ctx.font = '16px monospace';
      ctx.fillText(latest.value.toFixed(1), 10, 20);
    }
  }, [dataPoints]);

  return (
    <div className={styles.container}>
      <h2 className={styles.title}>FPS</h2>
      <canvas ref={canvasRef} width={600} height={300} className={styles.canvas} />
      <div className={styles.stats}>
        <span>Frame Time: {metrics.frameTimeMs.toFixed(2)}ms</span>
        <span>Memory: {metrics.memoryMB.toFixed(1)}MB</span>
      </div>
    </div>
  );
};

export default MetricsChart;
```

```css
/* src/components/MetricsChart.module.css */
.container {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.title {
  font-size: 1.2rem;
  font-weight: bold;
  margin: 0;
}

.canvas {
  display: block;
  width: 100%;
  border: 1px solid var(--border);
  background: #1a1a1a;
}

.stats {
  display: flex;
  gap: 1rem;
  font-size: 0.9rem;
  color: var(--text-secondary);
}
```

### Log Viewer Component

```typescript
// src/components/LogViewer.tsx
import { useEffect, useRef, useState } from 'react';
import styles from './LogViewer.module.css';

interface LogEntry {
  level: 'DEBUG' | 'INFO' | 'WARNING' | 'ERROR';
  category: string;
  message: string;
  timestamp: number;
  file?: string;
  line?: number;
}

interface LogViewerProps {
  logs: LogEntry[];
}

const LogViewer: React.FC<LogViewerProps> = ({ logs }) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const [filters, setFilters] = useState({
    level: 'DEBUG' as LogEntry['level'],
    category: 'All',
    search: '',
  });

  // Auto-scroll to bottom when new logs arrive
  useEffect(() => {
    const container = containerRef.current;
    if (!container) return;

    const isScrolledToBottom =
      container.scrollHeight - container.clientHeight <= container.scrollTop + 1;

    if (isScrolledToBottom) {
      container.scrollTop = container.scrollHeight;
    }
  }, [logs]);

  const shouldShowLog = (log: LogEntry): boolean => {
    const levels = ['DEBUG', 'INFO', 'WARNING', 'ERROR'];
    const minLevel = levels.indexOf(filters.level);
    const logLevel = levels.indexOf(log.level);

    if (logLevel < minLevel) return false;
    if (filters.category !== 'All' && log.category !== filters.category) return false;
    if (filters.search && !log.message.toLowerCase().includes(filters.search.toLowerCase())) {
      return false;
    }

    return true;
  };

  const filteredLogs = logs.filter(shouldShowLog);

  return (
    <div className={styles.container}>
      <div className={styles.filters}>
        <select
          value={filters.level}
          onChange={e => setFilters(prev => ({ ...prev, level: e.target.value as LogEntry['level'] }))}
          className={styles.select}
        >
          <option value="DEBUG">Debug+</option>
          <option value="INFO">Info+</option>
          <option value="WARNING">Warning+</option>
          <option value="ERROR">Error</option>
        </select>
        <input
          type="text"
          placeholder="Search..."
          value={filters.search}
          onChange={e => setFilters(prev => ({ ...prev, search: e.target.value }))}
          className={styles.searchInput}
        />
      </div>
      <div ref={containerRef} className={styles.logContainer}>
        {filteredLogs.map((log, i) => {
          const timestamp = new Date(log.timestamp).toLocaleTimeString();
          const location = log.file ? ` (${log.file}:${log.line})` : '';

          return (
            <div key={i} className={`${styles.logEntry} ${styles[`log${log.level}`]}`}>
              [{timestamp}][{log.category}][{log.level}] {log.message}{location}
            </div>
          );
        })}
      </div>
    </div>
  );
};

export default LogViewer;
```

```css
/* src/components/LogViewer.module.css */
.container {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.filters {
  display: flex;
  gap: 0.5rem;
  padding: 0.5rem;
  background: var(--bg-secondary);
  border-bottom: 1px solid var(--border);
}

.select,
.searchInput {
  padding: 0.25rem 0.5rem;
  background: var(--bg-tertiary);
  color: var(--text-primary);
  border: 1px solid var(--border);
  border-radius: 4px;
  font-family: monospace;
}

.searchInput {
  flex: 1;
}

.logContainer {
  flex: 1;
  overflow-y: auto;
  padding: 0.5rem;
  font-family: monospace;
  font-size: 12px;
}

.logEntry {
  padding: 0.25rem 0;
  white-space: pre-wrap;
  word-break: break-word;
}

.logDEBUG {
  color: var(--text-secondary);
}

.logINFO {
  color: var(--text-primary);
}

.logWARNING {
  color: var(--accent-yellow);
}

.logERROR {
  color: var(--accent-red);
  font-weight: bold;
}
```

## Build Integration

### Building the Application

The developer client is built as a static web application using Vite. The build is **fully integrated into CMake** and happens automatically when you run `make`.

**Build process:**
```bash
# From project root
make
```

CMake will automatically:
1. Check if npm is available
2. Run `npm install` to install dependencies
3. Run `npm run build` to build the React app with Vite
4. Copy the output from `apps/developer-client/dist/` to `build/developer-client/`

**No manual npm commands are needed!** Everything is managed through the standard build process.

**Output:**
```
dist/
  index.html                       # Single self-contained HTML file (~150 KB)
                                   # All JS and CSS inlined - no external files!
```

**Single-file architecture:**
- Uses `vite-plugin-singlefile` to inline all JavaScript and CSS into `index.html`
- **No external assets** - everything in one file that works with `file://` protocol
- Solves CORS issues that prevent ES modules from loading via `file://`
- The built `index.html` can be opened directly by double-clicking - no server needed
- CMake copies the built file to `build/developer-client/` during the build process

**Launching the application:**
- Navigate to `build/developer-client/` and double-click `index.html`
- Or use: `open build/developer-client/index.html` (macOS) or equivalent on other platforms
- The app will open in your default browser and connect to the developer server at the configured URL

### CMake Integration

```cmake
# CMakeLists.txt (root)

if(CMAKE_BUILD_TYPE STREQUAL "Development")
    # Build web app during game build
    add_custom_target(developer-client ALL
        COMMAND npm install
        COMMAND npm run build
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/apps/developer-client
        COMMENT "Building developer client web app"
    )

    # Copy built files to output directory
    add_custom_command(TARGET developer-client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/apps/developer-client/dist/
            ${CMAKE_BINARY_DIR}/developer-client/
        COMMENT "Copying developer client to build directory"
    )

    # Make applications depend on developer-client
    add_dependencies(world-sim-server developer-client)
    add_dependencies(ui-sandbox developer-client)
endif()
```

### package.json

```json
{
  "name": "developer-client",
  "version": "1.0.0",
  "type": "module",
  "scripts": {
    "build": "tsc && vite build"
  },
  "dependencies": {
    "react": "^18.2.0",
    "react-dom": "^18.2.0"
  },
  "devDependencies": {
    "@types/react": "^18.2.0",
    "@types/react-dom": "^18.2.0",
    "@vitejs/plugin-react": "^4.2.0",
    "typescript": "^5.3.0",
    "vite": "^5.0.0",
    "vite-plugin-singlefile": "^2.0.0"
  }
}
```

**Note:** Only the `build` script is included. No dev server or preview scripts. The `vite-plugin-singlefile` dependency enables single-file HTML output.

### vite.config.ts

```typescript
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';

export default defineConfig({
  plugins: [
    react(),
    viteSingleFile()  // Inline all JS/CSS into a single HTML file (works with file://)
  ],
  build: {
    outDir: 'dist',
    minify: 'esbuild',
    sourcemap: false,
    cssCodeSplit: false,  // Required for vite-plugin-singlefile
    assetsInlineLimit: 100000000  // Inline everything
  }
});
```

**Key configuration:**
- `plugins: [react(), viteSingleFile()]` - Enables React and inlines all assets into HTML
- `viteSingleFile()` - **Critical plugin** - Inlines all JavaScript and CSS into a single HTML file
- `cssCodeSplit: false` - Required for single-file build
- `assetsInlineLimit: 100000000` - Inline everything (no external files)
- **Solves file:// CORS issues** - ES modules don't work with `file://` protocol unless inlined

### Hover Inspector

Displays two views when F3 is enabled in application:

```typescript
// src/components/HoverInspector.tsx
import styles from './HoverInspector.module.css';

interface VisualLayer {
  type: string;
  id: string;
  zIndex: number;
  bounds: { x: number; y: number; width: number; height: number };
}

interface ComponentNode {
  type: string;
  id: string;
  depth: number;
}

interface HoverInspectorProps {
  data: {
    visualStack: VisualLayer[];
    componentHierarchy: ComponentNode[];
  };
}

const HoverInspector: React.FC<HoverInspectorProps> = ({ data }) => {
  const { visualStack, componentHierarchy } = data;

  // Sort visual stack by z-index (highest first)
  const sortedStack = [...visualStack].sort((a, b) => b.zIndex - a.zIndex);

  return (
    <div className={styles.container}>
      <div className={styles.section}>
        <h3>Visual Stack (z-index order)</h3>
        <div className={styles.stackList}>
          {sortedStack.map((layer, i) => (
            <div key={i} className={styles.visualLayer}>
              <span className={styles.layerType}>{layer.type}</span>
              <span className={styles.layerId}>{layer.id}</span>
              <span className={styles.layerZ}>z: {layer.zIndex}</span>
            </div>
          ))}
        </div>
      </div>

      <div className={styles.section}>
        <h3>Component Hierarchy</h3>
        <div className={styles.hierarchyList}>
          {componentHierarchy.map((node, i) => (
            <div
              key={i}
              className={styles.componentNode}
              style={{ paddingLeft: `${node.depth * 20}px` }}
            >
              <span className={styles.nodeType}>{node.type}</span>
              <span className={styles.nodeId}>{node.id}</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
};

export default HoverInspector;
```

```css
/* src/components/HoverInspector.module.css */
.container {
  display: flex;
  flex-direction: column;
  gap: 1rem;
  padding: 1rem;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
}

.section h3 {
  font-size: 1rem;
  margin-bottom: 0.5rem;
  color: var(--text-primary);
}

.stackList,
.hierarchyList {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

.visualLayer,
.componentNode {
  display: flex;
  gap: 0.5rem;
  padding: 0.25rem;
  background: var(--bg-tertiary);
  border-radius: 4px;
  font-family: monospace;
  font-size: 0.85rem;
}

.layerType,
.nodeType {
  color: var(--accent-green);
  font-weight: bold;
}

.layerId,
.nodeId {
  color: var(--text-secondary);
}

.layerZ {
  margin-left: auto;
  color: var(--accent-yellow);
}
```

## Styling

### Global Styles

Global styles define CSS variables and base styles, imported in `main.tsx`:

```css
/* src/styles/globals.css */
:root {
  --bg-primary: #1a1a1a;
  --bg-secondary: #2a2a2a;
  --bg-tertiary: #3a3a3a;
  --text-primary: #ffffff;
  --text-secondary: #aaaaaa;
  --border: #444444;
  --accent-green: #00ff00;
  --accent-yellow: #ffff00;
  --accent-red: #ff0000;
}

* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}

body {
  font-family: 'Consolas', 'Monaco', monospace;
  background: var(--bg-primary);
  color: var(--text-primary);
  overflow: hidden;
}

#root {
  height: 100vh;
}
```

### CSS Modules

Each component has a co-located `.module.css` file. Vite automatically scopes class names to prevent conflicts:

```typescript
// Component imports its styles
import styles from './MyComponent.module.css';

// Use scoped class names
<div className={styles.container}>
  <h2 className={styles.title}>Hello</h2>
</div>
```

**CSS Modules naming convention:**
- Component file: `MetricsChart.tsx`
- CSS Module file: `MetricsChart.module.css` (must use `.module.css` extension)
- Import in component: `import styles from './MetricsChart.module.css';`
- Usage: `className={styles.someClass}`

**Why CSS Modules:**
- Scoped styles prevent naming conflicts
- Co-location makes components self-contained
- Type-safe with TypeScript (when using `typescript-plugin-css-modules`)
- No runtime overhead - CSS Modules are resolved at build time

## Static Build Architecture

The developer client uses Vite with `vite-plugin-singlefile` to create a completely self-contained single HTML file:

1. **Build process:** CMake runs `npm install` and `npm run build` automatically
2. **Single-file output:** Everything (HTML + JavaScript + CSS) inlined into one 150 KB file
3. **No external dependencies:** No assets folder, no external scripts - just `index.html`
4. **Works with file:// protocol:** Solves CORS restrictions that prevent ES modules from loading
5. **CMake integration:** Everything triggered by `make` - no manual npm commands
6. **Output location:** Single file copied to `build/developer-client/index.html`

**Why single-file:**
- ES modules (`type="module"`) don't work with `file://` protocol due to CORS
- Browser blocks loading external scripts from local files
- `vite-plugin-singlefile` inlines everything to bypass this restriction
- Result: Double-click `index.html` and it just works!

**How it works:**
1. Vite builds React app with all optimizations
2. `vite-plugin-singlefile` inlines all JavaScript and CSS into HTML
3. CMake copies single `index.html` to `build/developer-client/`
4. User opens file in browser - everything loads instantly
5. Only builds in Development/Debug mode (skipped in Release builds)

**Workflow:**
```bash
# Build everything (C++ code + developer client)
make

# Open the developer client (just double-click or use open)
open build/developer-client/index.html  # macOS
# or double-click index.html in file explorer
```

**Requirements:**
- Node.js and npm must be installed
- If npm is not found, CMake will skip the developer-client build with a warning

## Performance Considerations

**Client-side performance:**
- Canvas rendering: ~2ms per chart update
- React re-renders: Optimized with proper state management and memoization
- Memory: ~10 MB for chart data buffers
- SSE overhead: Minimal (browser handles everything)

**Best practices:**
- Use `useRef` for canvas operations to avoid re-renders
- Use `useMemo` and `useCallback` to prevent unnecessary re-renders
- Throttle state updates (don't update on every SSE event)
- Limit log history (keep last 1000 entries)
- Use CSS for animations (hardware accelerated)

## Related Documentation

- [Developer Server](./developer-server.md) - C++ server implementation
- [UI Inspection](./ui-inspection.md) - UI hierarchy and hover inspection
- [INDEX](./INDEX.md) - Observability system overview

## Implementation Status

- [x] Technology stack chosen (React + Vite + CSS Modules)
- [x] Architecture defined (static SPA with relative paths)
- [x] SSE client design
- [ ] Project scaffolding (Vite + React + TypeScript)
- [ ] CSS Modules setup with co-located styles
- [ ] ServerConnection service implementation
- [ ] App.tsx with SSE connection management
- [ ] MetricsChart React component
- [ ] LogViewer React component
- [ ] UIHierarchyTree React component
- [ ] HoverInspector React component
- [ ] Build integration with CMake
- [ ] Static build testing (open index.html directly)

## Notes

**Static file architecture**: The built application can be opened directly in a browser without any server. This is achieved through Vite's `base: "./"` configuration, which makes all asset paths relative.

**Auto-reconnection**: `EventSource` automatically reconnects when connection drops. No manual retry logic needed.

**Multiple servers**: Can connect to multiple applications simultaneously by opening multiple tabs (different ports). Each tab can point to a different developer server.

**Debugging**: Use browser DevTools to debug the developer client itself (meta-debugging!).

**Build workflow**: Simply run `make` from the project root. CMake handles everything - npm dependencies, Vite build, and copying files to `build/developer-client/`.

**CSS Modules requirement**: Files must use the `.module.css` extension for CSS Modules to work. Regular `.css` files are treated as global styles.
