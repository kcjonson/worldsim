# Developer Client (Web Application)

Created: 2025-10-24
Status: Active

## Overview

The developer client is an external TypeScript/Vite web application that connects to the [developer server](./developer-server.md) via HTTP Server-Sent Events (SSE). It provides real-time visualization of metrics, logs, UI state, and profiler data.

**Key Principle:** Runs in a separate browser window. Cannot crash or interfere with the running application.

## Technology Stack

- **Build Tool**: Vite (fast HMR, TypeScript support, lightweight)
- **Language**: TypeScript (type safety, modern features)
- **UI Framework**: Vanilla TypeScript (no React/Vue - keep it simple)
- **Charts**: Custom Canvas rendering (no external charting library)
- **Styling**: Plain CSS with dark theme
- **SSE Client**: Native browser `EventSource` API (built-in, no library needed)

**Why this stack:**
- **Vite**: Lightning-fast hot reload during development
- **Vanilla TS**: No framework overhead, full control, easy to understand
- **Canvas**: Custom charts with zero dependencies, optimized for real-time data
- **EventSource**: Built into all browsers, handles reconnection automatically

## Project Structure

```
developer-client/                    # Root folder (new name)
  src/
    main.ts                          # Entry point, app initialization

    components/
      MetricsChart.ts                # FPS/memory line chart (Canvas)
      LogViewer.ts                   # Real-time log display with filtering
      ProfilerView.ts                # Flame graph rendering
      UIHierarchyTree.ts             # UI element tree view
      HoverInspector.ts              # Visual stack + component hierarchy

    services/
      ServerConnection.ts            # SSE connection management
      DataStore.ts                   # Client-side data buffering

    styles/
      main.css                       # Dark theme, layout
      components.css                 # Component-specific styles

  public/
    index.html                       # Single-page app entry

  vite.config.ts                     # Vite configuration
  package.json                       # Dependencies
  tsconfig.json                      # TypeScript configuration
```

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
// src/main.ts

const serverUrl = 'http://localhost:8080'; // Or 8081 for ui-sandbox, 8082 for game client

const connection = new ServerConnection(serverUrl);

// Connect to metrics stream
connection.connect('metrics', {
    endpoint: '/stream/metrics',
    eventType: 'metric',
    handler: (data) => {
        metricsChart.addDataPoint(data.fps, data.frameTimeMs);
        memoryChart.addDataPoint(data.memoryMB);
    }
});

// Connect to logs stream
connection.connect('logs', {
    endpoint: '/stream/logs',
    eventType: 'log',
    handler: (data) => {
        logViewer.appendLog(data);
    }
});

// Connect to UI hierarchy stream
connection.connect('ui', {
    endpoint: '/stream/ui',
    eventType: 'ui_update',
    handler: (data) => {
        uiHierarchyTree.update(data.root);
    }
});

// Connect to hover inspection (when F3 is enabled in app)
connection.connect('hover', {
    endpoint: '/stream/hover',
    eventType: 'hover',
    handler: (data) => {
        hoverInspector.update(data.visualStack, data.componentHierarchy);
    }
});
```

## Custom Chart Rendering (Canvas)

### Metrics Line Chart

```typescript
// src/components/MetricsChart.ts

interface DataPoint {
    timestamp: number;
    value: number;
}

class MetricsChart {
    private canvas: HTMLCanvasElement;
    private ctx: CanvasRenderingContext2D;
    private dataPoints: DataPoint[] = [];
    private maxPoints = 300; // 30 seconds at 10 Hz
    private min: number = 0;
    private max: number = 100;

    constructor(canvas: HTMLCanvasElement, title: string, min: number, max: number) {
        this.canvas = canvas;
        this.ctx = canvas.getContext('2d')!;
        this.min = min;
        this.max = max;
    }

    addDataPoint(value: number): void {
        this.dataPoints.push({
            timestamp: Date.now(),
            value: value
        });

        // Keep only last N points
        if (this.dataPoints.length > this.maxPoints) {
            this.dataPoints.shift();
        }

        this.render();
    }

    private render(): void {
        const width = this.canvas.width;
        const height = this.canvas.height;

        // Clear canvas
        this.ctx.fillStyle = '#1a1a1a';
        this.ctx.fillRect(0, 0, width, height);

        // Draw grid
        this.drawGrid(width, height);

        // Draw data line
        if (this.dataPoints.length > 1) {
            this.drawLine(width, height);
        }

        // Draw current value
        if (this.dataPoints.length > 0) {
            const latest = this.dataPoints[this.dataPoints.length - 1];
            this.drawValue(latest.value);
        }
    }

    private drawGrid(width: number, height: number): void {
        this.ctx.strokeStyle = '#333';
        this.ctx.lineWidth = 1;

        // Horizontal lines
        for (let i = 0; i <= 10; i++) {
            const y = (i / 10) * height;
            this.ctx.beginPath();
            this.ctx.moveTo(0, y);
            this.ctx.lineTo(width, y);
            this.ctx.stroke();
        }

        // Vertical lines (time markers)
        for (let i = 0; i <= 10; i++) {
            const x = (i / 10) * width;
            this.ctx.beginPath();
            this.ctx.moveTo(x, 0);
            this.ctx.lineTo(x, height);
            this.ctx.stroke();
        }
    }

    private drawLine(width: number, height: number): void {
        this.ctx.beginPath();
        this.ctx.strokeStyle = '#00ff00';
        this.ctx.lineWidth = 2;

        this.dataPoints.forEach((point, i) => {
            const x = (i / this.dataPoints.length) * width;
            const normalizedValue = (point.value - this.min) / (this.max - this.min);
            const y = height - (normalizedValue * height);

            if (i === 0) {
                this.ctx.moveTo(x, y);
            } else {
                this.ctx.lineTo(x, y);
            }
        });

        this.ctx.stroke();
    }

    private drawValue(value: number): void {
        this.ctx.fillStyle = '#fff';
        this.ctx.font = '16px monospace';
        this.ctx.fillText(value.toFixed(1), 10, 20);
    }
}
```

### Log Viewer Component

```typescript
// src/components/LogViewer.ts

interface LogEntry {
    level: 'DEBUG' | 'INFO' | 'WARNING' | 'ERROR';
    category: string;
    message: string;
    timestamp: number;
    file?: string;
    line?: number;
}

class LogViewer {
    private container: HTMLElement;
    private logs: LogEntry[] = [];
    private filters = {
        level: 'DEBUG' as LogEntry['level'],
        category: 'All',
        search: ''
    };

    constructor(container: HTMLElement) {
        this.container = container;
        this.render();
    }

    appendLog(log: LogEntry): void {
        this.logs.push(log);

        // Keep last 1000 logs
        if (this.logs.length > 1000) {
            this.logs.shift();
        }

        // Auto-scroll to bottom if already at bottom
        const isScrolledToBottom =
            this.container.scrollHeight - this.container.clientHeight <=
            this.container.scrollTop + 1;

        this.renderLog(log);

        if (isScrolledToBottom) {
            this.container.scrollTop = this.container.scrollHeight;
        }
    }

    private renderLog(log: LogEntry): void {
        // Filter
        if (!this.shouldShowLog(log)) return;

        const logEl = document.createElement('div');
        logEl.className = `log-entry log-${log.level.toLowerCase()}`;

        const timestamp = new Date(log.timestamp).toLocaleTimeString();
        const location = log.file ? ` (${log.file}:${log.line})` : '';

        logEl.textContent = `[${timestamp}][${log.category}][${log.level}] ${log.message}${location}`;

        this.container.appendChild(logEl);
    }

    private shouldShowLog(log: LogEntry): boolean {
        // Level filter (show at this level or higher)
        const levels = ['DEBUG', 'INFO', 'WARNING', 'ERROR'];
        const minLevel = levels.indexOf(this.filters.level);
        const logLevel = levels.indexOf(log.level);
        if (logLevel < minLevel) return false;

        // Category filter
        if (this.filters.category !== 'All' && log.category !== this.filters.category) {
            return false;
        }

        // Search filter
        if (this.filters.search && !log.message.toLowerCase().includes(this.filters.search.toLowerCase())) {
            return false;
        }

        return true;
    }
}
```

## Build Integration

### Development Mode (Hot Reload)

```bash
cd developer-client
npm install
npm run dev
```

**Output:**
```
VITE v5.0.0  ready in 432 ms

➜  Local:   http://localhost:5173/
➜  Network: use --host to expose
➜  press h + enter to show help
```

Connect to developer server at `http://localhost:8080` (or 8081, 8082).

**Hot reload:** Changes to TypeScript/CSS instantly reflected in browser.

### Production Build (Bundled with Game)

```bash
npm run build
```

**Output:**
```
dist/
  index.html
  assets/
    index-abc123.js      # Bundled, minified JavaScript
    index-def456.css     # Bundled, minified CSS
```

Developer server serves these static files from `/` endpoint.

### CMake Integration

```cmake
# CMakeLists.txt (root)

if(CMAKE_BUILD_TYPE STREQUAL "Development")
    # Build web app during game build
    add_custom_target(developer-client ALL
        COMMAND npm install
        COMMAND npm run build
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/developer-client
        COMMENT "Building developer client web app"
    )

    # Copy built files to output directory
    add_custom_command(TARGET developer-client POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/developer-client/dist/
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
    "dev": "vite",
    "build": "tsc && vite build",
    "preview": "vite preview"
  },
  "devDependencies": {
    "typescript": "^5.3.0",
    "vite": "^5.0.0"
  }
}
```

### vite.config.ts

```typescript
import { defineConfig } from 'vite';

export default defineConfig({
  build: {
    outDir: 'dist',
    minify: 'terser',
    sourcemap: false,
    rollupOptions: {
      output: {
        manualChunks: undefined
      }
    }
  },
  server: {
    port: 5173,
    open: true
  }
});
```

## UI Components

### Hover Inspector

Displays two views when F3 is enabled in application:

```typescript
// src/components/HoverInspector.ts

interface VisualLayer {
    type: string;
    id: string;
    zIndex: number;
    bounds: { x: number; y: number; width: number; height: number };
}

interface ComponentNode {
    type: string;
    id: string;
}

class HoverInspector {
    private visualStackEl: HTMLElement;
    private componentHierarchyEl: HTMLElement;

    update(visualStack: VisualLayer[], componentHierarchy: ComponentNode[]): void {
        this.renderVisualStack(visualStack);
        this.renderComponentHierarchy(componentHierarchy);
    }

    private renderVisualStack(stack: VisualLayer[]): void {
        this.visualStackEl.innerHTML = '<h3>Visual Stack (z-index order)</h3>';

        // Render from top to bottom (highest z-index first)
        stack.sort((a, b) => b.zIndex - a.zIndex).forEach(layer => {
            const el = document.createElement('div');
            el.className = 'visual-layer';
            el.innerHTML = `
                <span class="layer-type">${layer.type}</span>
                <span class="layer-id">${layer.id}</span>
                <span class="layer-z">z: ${layer.zIndex}</span>
            `;
            this.visualStackEl.appendChild(el);
        });
    }

    private renderComponentHierarchy(hierarchy: ComponentNode[]): void {
        this.componentHierarchyEl.innerHTML = '<h3>Component Hierarchy</h3>';

        hierarchy.forEach((node, depth) => {
            const el = document.createElement('div');
            el.className = 'component-node';
            el.style.paddingLeft = `${depth * 20}px`;
            el.innerHTML = `
                <span class="node-type">${node.type}</span>
                <span class="node-id">${node.id}</span>
            `;
            this.componentHierarchyEl.appendChild(el);
        });
    }
}
```

## Styling (Dark Theme)

```css
/* src/styles/main.css */

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

.app-container {
    display: grid;
    grid-template-columns: 1fr 1fr;
    grid-template-rows: auto 1fr;
    height: 100vh;
    gap: 1px;
    background: var(--border);
}

.header {
    grid-column: 1 / -1;
    background: var(--bg-secondary);
    padding: 1rem;
    border-bottom: 1px solid var(--border);
}

.panel {
    background: var(--bg-primary);
    overflow: auto;
    padding: 1rem;
}

.log-entry {
    padding: 0.25rem 0;
    font-size: 12px;
    font-family: monospace;
}

.log-debug { color: var(--text-secondary); }
.log-info { color: var(--text-primary); }
.log-warning { color: var(--accent-yellow); }
.log-error { color: var(--accent-red); font-weight: bold; }

canvas {
    display: block;
    width: 100%;
    height: 300px;
    border: 1px solid var(--border);
}
```

## Performance Considerations

**Client-side performance:**
- Canvas rendering: ~2ms per chart update
- DOM updates: Throttled to 60 FPS max
- Memory: ~10 MB for chart data buffers
- SSE overhead: Minimal (browser handles everything)

**Best practices:**
- Use `requestAnimationFrame` for chart rendering
- Throttle DOM updates (don't update on every SSE event)
- Limit log history (keep last 1000 entries)
- Use CSS for animations (hardware accelerated)

## Related Documentation

- [Developer Server](./developer-server.md) - C++ server implementation
- [UI Inspection](./ui-inspection.md) - UI hierarchy and hover inspection
- [INDEX](./INDEX.md) - Observability system overview

## Implementation Status

- [x] Technology stack chosen
- [x] Architecture defined
- [x] SSE client design
- [ ] Project scaffolding (Vite + TypeScript)
- [ ] ServerConnection service
- [ ] MetricsChart component
- [ ] LogViewer component
- [ ] UIHierarchyTree component
- [ ] HoverInspector component
- [ ] Build integration with CMake

## Notes

**Auto-reconnection**: `EventSource` automatically reconnects when connection drops. No manual retry logic needed.

**Multiple servers**: Can connect to multiple applications simultaneously by opening multiple tabs (different ports).

**Debugging**: Use browser DevTools to debug the developer client itself (meta-debugging!).

**Deployment**: Developer client is bundled into game repository, served by developer server. No separate deployment needed.
