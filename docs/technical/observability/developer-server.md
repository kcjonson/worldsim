# Developer Server

Created: 2025-10-12
Last Updated: 2026-11-11
Status: Active

The dev server no longer lives in this repo. It is salal's `app::DevServer` (salal `libs/app/DevServer`), and salal's `docs/app-api.md` ("Dev server") is its design and reference. The design this doc used to hold (a game thread writing lock-free ring buffers, an HTTP thread reading them at 10 to 20 Hz, per-stream rates, profiler and renderer streams, a JSON config, a console toggle) was never what shipped and is gone; read salal for how the server works. [Observability](./INDEX.md) has the short version: requests park until the main thread serves them at fixed points in the frame, and the two streams (metrics and logs) send every 100 ms.

What follows is what worldsim adds.

## What world-sim registers

The game scene registers everything in `GameScene::onEnter` and holds the registrations for as long as it runs, so its routes, controls, and views exist only while the game scene is up. Most come from `DevCommandHandler::registerWith` (`apps/world-sim/scenes/game/dev/`).

| Kind | Names | Served |
|------|-------|--------|
| Routes (`/api/dev/<verb>`) | `freebuild`, `construction`, `give`, `spawn`, `colonist`, `need`, `time`, `teleport`, `select`, `kill`, `complete`, `foundation`, `foundation-edit`, `walls`, `opening`, `craft`, `tunable`, `flora`, `storage` | before input, at the start of the next frame |
| Controls (`/api/control?action=`) | `camera`, `gpuhold` | before input, at the start of the next frame |
| State views (`/api/state?what=`) | `colonists`, `construction`, `stations`, `storage`, `time`, `landing`, `summary`, `tunables`, `gpuhold`, `uprights` | after render |

The ui tree and lint come from the top scene's ui and need no registration; `what=scene` is answered by the server itself.

A route runs on the main thread before the scene's update, so its changes are in that frame's update; a view runs after render, so it reads the state that frame drew. [Main Loop Timing](../main-loop-timing.md) has both points in the frame.

## Related Documentation

- [Observability](./INDEX.md): endpoints, streams, build configuration
- [Developer Client](./developer-client.md): the browser client and its Dev Tools tab
- [UI Inspection](./ui-inspection.md): ui tree, lint, screenshots, input
- [Logging System](../logging-system.md): what reaches the log stream
