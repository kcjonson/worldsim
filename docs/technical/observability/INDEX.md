# Observability System

Created: 2025-10-24
Last Updated: 2026-11-11
Status: Active

## Overview

Each app runs an HTTP dev server on localhost that answers snapshots, takes controls and synthetic input, and streams metrics and logs to the browser-based [developer client](./developer-client.md). The server is salal's `app::DevServer` (salal `libs/app/DevServer`); salal's `docs/app-api.md` ("Dev server") is its reference. This folder covers what worldsim adds and the client.

## How it works

```
Main thread, each frame                         HTTP threads (cpp-httplib)
  step 3, before input:                           accept a request
    run parked controls and /api/dev routes  <--  park it, wait for the main thread
    take injected input                           (503 after requestTimeout, 10 s)
  ... update, render ...
  step 10, after render:
    publish the frame's metrics snapshot     -->  /api/metrics, /api/health read it
    take parked screenshots, answer parked
    /api/state, /api/ui/tree, /api/ui/lint
  any thread that logs:
    write to a 1000-entry log ring           -->  /stream/logs reads it
```

A request that needs the game waits for the main thread to serve it at one of two fixed points in the frame, so it always sees a consistent frame and never races the game. The main thread never waits for the network. [Main Loop Timing](../main-loop-timing.md) places both points in the frame.

The log ring is a fixed array behind a mutex held for one copy (salal `RingBuffer.h`), written by whichever thread logs. Each `/stream/logs` connection reads it from its own cursor, starting a ring's worth back. The metrics snapshot is published under a mutex once a frame.

## Components

### [Developer Server](./developer-server.md)
What worldsim registers on salal's dev server: the game's dev routes, controls, and state views. The server itself is documented in salal.

**Ports** (each app's default):
- 8081: world-sim
- 8090: ui-sandbox
- 8070: asset-manager
- 8080, 8082: planned for the game server and the networked client

### [Developer Client](./developer-client.md)
External React/TypeScript/Vite web application: metrics charts, a log viewer, and a Dev Tools tab that calls the dev routes and state views.

### [UI Inspection](./ui-inspection.md)
The ui tree, layout lint, screenshots, and synthetic input, as snapshot endpoints.

## Stream endpoints

| Endpoint | Rate | Data |
|----------|------|------|
| `/stream/metrics` | every 100 ms per connection, checked every 10 ms | the latest published metrics snapshot plus the top scene's name |
| `/stream/logs` | every 100 ms per connection, everything logged since the last send | log entries from the ring |

There are no other streams.

## Snapshot and control endpoints

| Endpoint | Served | Purpose |
|----------|--------|---------|
| `/api/health` | on the HTTP thread, from the published snapshot | status and uptime |
| `/api/metrics` | on the HTTP thread, from the published snapshot | frame time, main-loop sections, draw counts, ECS per-system times of the last frame |
| `/api/state?what=<view>` | step 10 | `scene`, and each view the running scene registered |
| `/api/ui/tree`, `/api/ui/lint` | step 10 | the top scene's ui |
| `/api/ui/screenshot` | step 10 | PNG of the frame just drawn |
| `/api/control?action=<action>` | step 3 | `exit`, `scene`, `pause`, `resume`, `reload`, `vsync`, `step`, `capture`, and each control the app added |
| `/api/dev/<verb>` | step 3 | each route the running scene registered |
| `/api/input?ev=...` | queued on the HTTP thread, taken at step 3 while the app runs unpaused | synthetic input |

## Build configuration

The dev server is compiled into Debug and RelWithDebInfo builds. A Release build has none unless salal is configured with `-DSALAL_DEV_SERVER_IN_RELEASE=ON` (for profiling). Where it is compiled in, world-sim starts it on its default port; `--http-port <port>` picks another. The server binds `127.0.0.1` only.

The developer client builds with CMake when `BUILD_DEVELOPER_CLIENT` is `ON`, or `AUTO` in a single-config Debug or Development build, and npm is found; it lands in `build/developer-client/`.

Nobody has measured the server's per-frame cost.

## Related Systems

### [Logging System](../logging-system.md)
The logger writes to the console, the run's log file, and, where the dev server runs, its log ring.

### [Diagnostic Drawing](../diagnostic-drawing.md)
In-viewport debug drawing, a separate tool that renders in the game.

## Quick Start

```bash
# Run world-sim (Debug or RelWithDebInfo); the dev server starts on 8081
./build/apps/world-sim/world-sim

# Open the developer client, a static page, and point it at the app's port
open build/developer-client/index.html

# Or ask directly
curl "http://127.0.0.1:8081/api/state?what=scene"
curl http://127.0.0.1:8081/api/metrics
```

Only one instance of an app runs at a time: a second one finds its port busy, logs it, and exits.

## Notes

**Security**: localhost binding only, no authentication, CORS open for the local client.
