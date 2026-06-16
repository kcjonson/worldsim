# Developer Client

Web-based developer tools for world-sim. Connects to the developer server via SSE to display real-time metrics, logs, and UI inspection data.

## Architecture

- **Framework:** React 18 + TypeScript
- **Build Tool:** Vite (static SPA output)
- **Styling:** CSS Modules (co-located with components)
- **Connection:** Server-Sent Events (EventSource API)

## Key Features

- **Single-File Output:** Everything inlined into one ~150 KB HTML file using `vite-plugin-singlefile`
- **No Server Needed:** Works with `file://` protocol - just double-click to open!
- **Real-time Data:** Connects to developer server via SSE streams
- **CSS Modules:** Scoped styling with `.module.css` files co-located with components

## Building

The developer client is automatically built by CMake in Development/Debug mode:

```bash
# From project root
make
```

**That's it!** CMake will:
1. Detect if npm is available
2. Run `npm install` to get dependencies
3. Run `npm run build` to build the React app
4. Copy the output to `build/developer-client/`

No manual npm commands needed!

## Running

> **This is a static single-file build, NOT a live server.** There is no `dev`/serve script,
> and you should not run `vite` / `npm run dev` against it. Build the static HTML (`npm run build`,
> or CMake with `-DBUILD_DEVELOPER_CLIENT=ON`) and open the file directly. It talks to the running
> game over HTTP (the debug server on port 8081), so the *game* is the only thing that needs to be
> running, not a web server.

After building, open the static HTML file:

```bash
# macOS
open ../../build/developer-client/index.html

# Or just double-click:
# build/developer-client/index.html
```

The app opens in your browser and connects to `http://localhost:8081` by default.

## Tabs

- **Performance / Logs** — read-only, fed by SSE streams (`/stream/metrics`, `/stream/logs`).
- **Dev Tools** — drives the running game: one-shot `fetch()` to the dev verbs (`/api/dev/<verb>`)
  and the world-state readback (`/api/state`). Dev builds only. This is the only tab that *sends*
  commands; it still needs no web server of its own (CORS `*` lets the static `file://` page reach
  the debug server).

## Project Structure

```
apps/developer-client/
  src/
    components/          # React components with co-located .module.css files
    services/            # Business logic (ServerConnection, etc.)
    styles/              # Global styles (globals.css)
    App.tsx              # Root component
    App.module.css       # Root component styles
    main.tsx             # React entry point
  public/
    index.html           # HTML entry point
  vite.config.ts         # Vite configuration (base: "./")
  package.json           # Dependencies
  tsconfig.json          # TypeScript configuration
```

## Development

See [docs/technical/observability/developer-client.md](../../docs/technical/observability/developer-client.md) for full documentation.
