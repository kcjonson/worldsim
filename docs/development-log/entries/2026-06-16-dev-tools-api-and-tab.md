# Dev/Test Tools: HTTP verbs + developer-client tab

**Date:** 2026-06-16

## Summary

Added a set of dev/test HTTP verbs to put the sim into a desired state on demand, plus a
synchronous world-state readback, plus a "Dev Tools" tab in the developer-client that drives
them. Before this, the dev API was construction-only and write-only; every check went through a
screenshot. Now you can spawn entities/colonists, hand out materials, poke colonist and time
state, and read the world back as JSON a test can assert on.

The debug server (`libs/foundation/debug`) stays domain-agnostic: it queues generic
`DevCommand`s and carries the readback handshake, but `GameScene` interprets the verbs and does
the serialization (it owns the ECS).

## Verbs (all under `/api/dev/<verb>`, dev builds only)

New, on top of the existing `freebuild`/`foundation`/`walls`/`opening`:

- `spawn?def=&at=x,y&n=1&scatter=0` — N copies of any asset via `PlacementSystem::spawnEntity`; deterministic sunflower scatter; rejects unknown defNames.
- `colonist?at=x,y&n=1&name=` — N full colonists via `GameScene::spawnColonist`.
- `give?material=&n=&where=site|loose|colonist|storage[&at=x,y]` — replaces `givewood`. site reuses `creditMaterialToSites`; loose drops packaged item-entities (capped 50); colonist/storage write to the nearest matching `Inventory` (storage path ignores accept rules — dev cheat).
- `need?colonist=&need=&value=0..100` — set one need (Needs.h labels, case-insensitive).
- `time?speed=0..3|set=HH:MM|skip=Nh|Nm` — drives `TimeSystem` (new `setTimeOfDay`/`skipTime` helpers).
- `teleport?colonist=&to=x,y` — move a colonist, clear its movement target + velocity.
- `select?colonist=|at=x,y` — `SelectionSystem::selectColonist` (by id or nearest).
- `kill?colonist=` — `destroyEntity`, clearing the selection if it pointed there.
- `complete?id=<blueprintEntity>` — `forceCompleteBlueprint` (per-entity freebuild).

Deferred: `demolish` — it's work-driven (mark `demolishing`, a colonist tears it down) with
cascade/walls-still-stand guards; `freebuild` + scene reload cover the testing need for now.

## Readback: `GET /api/state?what=summary|colonists|construction|time`

The async dev-command queue can't return data (it drains after the HTTP handler returns, and the
HTTP thread must not touch the ECS). So `/api/state` mirrors the **screenshot** handshake:
`requestState` parks the HTTP thread on an atomic while the game thread serializes during its
frame (`consumeStateRequest` + `deliverState`). `GameScene::serializeState` builds the JSON
(colonists: id/name/pos/8 needs/action; construction: foundations; time: snapshot; summary:
counts). This is what turns the API from write-only into assertable.

## Dev Tools tab (developer-client)

`apps/developer-client` is a **static** single-file React build (`vite-plugin-singlefile`, opened
via `file://`) — NOT a live/dev server. You build it (`npm run build` or CMake) and open the HTML;
there is no `vite dev` step (don't add one). Added a third tab with sections mirroring the verbs
(Spawn, Resources, Colonist, Time, Construction, World state). `DevToolsService.ts` is the first
part of the client that *sends* (one-shot `fetch()` to `/api/dev/*` and `/api/state`) rather than
streaming SSE. CORS `*` on the debug server makes the cross-origin fetch work from `file://`.

## Files

- `libs/foundation/debug/DebugServer.{h,cpp}` — state-request channel + `/api/state` route.
- `apps/world-sim/scenes/game/GameScene.cpp` — new `dev*` verbs, `serializeState`, per-frame state drain; deleted `devGiveWood`.
- `libs/engine/ecs/systems/TimeSystem.{h,cpp}` — `setTimeOfDay`, `skipTime`.
- `libs/foundation/debug/DebugServer.test.cpp` — `parseDevVerb` cases refreshed to current verbs.
- `apps/developer-client/src/` — `services/DevToolsService.ts`, `components/DevToolsPanel.{tsx,module.css}`, `App.tsx` tab wiring.

## Verification

API verified end-to-end against a live game via curl + `/api/state` readback: `colonist` (3
colonists), `need` (Bob Hunger 98.7→4.9), `teleport` (exact pos), `time` (speed 3, +4h skip),
`spawn`/`give` queued ok; a screenshot confirmed oak trees + berry bushes render in the sunflower
scatter. `parseDevVerb` unit tests green (7/7). Developer-client builds clean (`tsc && vite build`).

## Next steps

- Open the static dev-client (`apps/developer-client/dist/index.html`, or `build/developer-client/` via CMake `-DBUILD_DEVELOPER_CLIENT=ON`) against a running game to exercise the tab.
- Optional: `demolish` verb; inventory in the colonist readback; `/api/state?what=query|count`.
