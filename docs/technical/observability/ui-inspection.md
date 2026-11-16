# UI Inspection

Created: 2025-10-12
Last Updated: 2026-11-11
Status: Active

How to see and drive a running app's ui from outside it: the ui tree, layout lint, screenshots, and synthetic input, all on the app's dev server (salal's `app::DevServer`; salal's `docs/app-api.md`, "Dev server", is the reference). The design this doc used to hold (ui, hover, and event streams, an F3 hover inspector, element and click endpoints, golden-image comparison) was never built and is gone. Visual checks look for regressions, never pixel changes: no golden images ([Testing](../../testing/README.md)).

## Endpoints

| Endpoint | Gives | Served |
|----------|-------|--------|
| `/api/ui/tree` | the top scene's ui: each component's type, bounds, and layout properties | after render |
| `/api/ui/lint` | layout invariant violations in that tree, with a count; expect `"count":0` before trusting a screenshot | after render |
| `/api/ui/screenshot` | PNG of the frame just drawn, read from the backbuffer (`readBackbuffer`) in framebuffer pixels: twice the logical size on a HiDPI display | after render |
| `/api/input?ev=...` | synthetic input in logical pixels (halve screenshot coordinates on a HiDPI display): `move`, `down`, `up`, `click`, `scroll`, `keydown`, `keyup`, `char`; repeat `ev=` for a sequence in one request | queued at once, taken before input at the start of a frame while the app runs unpaused |

"After render" is the frame's post-render point and "before input" its pre-frame point; [Main Loop Timing](../main-loop-timing.md) places both. A request waits for the next frame to reach its point, so a screenshot shows a whole frame and input lands as real input does.

The tree and lint walk the scene's ui roots. A scene that draws without ui components answers an empty tree, and its `"count":0` checks nothing. The asset-manager's tree rows don't report their laid-out positions, so its lint count isn't zero; read its top-bar entries instead.

## Use

```bash
# The layout, then the invariants, then a picture
curl http://127.0.0.1:8090/api/ui/tree
curl http://127.0.0.1:8090/api/ui/lint
curl -s http://127.0.0.1:8090/api/ui/screenshot > screenshot.png

# Click, then type 'h'
curl "http://127.0.0.1:8090/api/input?ev=click,100,100&ev=char,104"
```

Ports: ui-sandbox 8090, world-sim 8081, asset-manager 8070. The project CLAUDE.md ("Testing Visual Changes") has the rebuild, launch, and check loop.

## Related Documentation

- [Observability](./INDEX.md): the dev server's endpoints and streams
- [Developer Server](./developer-server.md): what world-sim registers
- [Developer Client](./developer-client.md): the browser client
