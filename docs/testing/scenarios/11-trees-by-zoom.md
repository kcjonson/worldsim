# Scenario 11: trees by zoom

**Status:** draft  
**Last verified:** 2026-11-11, steps 1 to 3, 5 to 8 and 10; step 4 by camera calls, step 9 not driven

Trees draw at every zoom step from the detail level that fits it, never vanish while the camera moves, sort against colonists, select, and drop wood. Spec: [tree-variety.md](../../technical/asset-system/tree-variety.md).

---

## Steps

Build `world-sim` (RelWithDebInfo), launch it with no flags, click Enter Expedition, then Quick Start. The dev server is on 127.0.0.1:8081. A camera jump is `curl.exe "http://127.0.0.1:8081/api/control?action=camera&x=<x>&y=<y>&zoom=<z>"`, with `x` and `y` optional; the zoom steps are 0.25, 0.5, 0.75, 1, 1.5, 2, 3 (default), 4, 6, 8, 10, 15 and 20. `curl.exe "http://127.0.0.1:8081/api/state?what=uprights"` answers `screenPxPerMeter`, `levels` (one `{pxPerMeter, instances}` per level in use) and `levelRequestsPending`.

1. Read the colonists (`curl.exe "http://127.0.0.1:8081/api/state?what=colonists"`: Bob's `id`, `x` and `y`, noted as BID, BX and BY), then `...camera&x=<BX>&y=<BY>&zoom=3`: trees stand around the colonists, none half-drawn.
2. `...camera&zoom=0.25`, poll `what=uprights` until `levelRequestsPending` is 0: `screenPxPerMeter` is 2, `levels` holds only the 2 px/m level, and the screen shows trees all the way out to the edges, no empty patches.
3. Repeat step 2 at zoom 1 (`screenPxPerMeter` 8), 3 (24), 10 (80) and 20 (160): each time `levelRequestsPending` reaches 0 within a few seconds, and `levels` reports the highest of 2, 4, 8, 24, 40 and 80 at or below `screenPxPerMeter` (80 at both 10 and 20). While `levelRequestsPending` is above 0, `levels` may list two. The trees keep their outline and place between steps; only the leaf marks get finer.
4. Scroll the wheel out and in across the whole range, 20 to 0.25 and back, as fast as you can (or fire the `camera&zoom=` calls back to back): no tree vanishes or blinks at any step (a tree may be coarser for a moment, never absent).
5. Back on Bob at zoom 3 (`...camera&x=<BX>&y=<BY>&zoom=3`), with the game running, not paused, plant a known tree beside Bob (`curl.exe "http://127.0.0.1:8081/api/dev/spawn?def=Flora_TreeOak&at=<BX+4>,<BY>&n=1"`, call its spot TX,TY), then put Bob 1 m north of the trunk (`curl.exe "http://127.0.0.1:8081/api/dev/teleport?colonist=<BID>&to=<TX>,<TY-1>"`) and look at once, before he walks on: the crown hides the part of him it covers.
6. Teleport Bob 1 m south of the trunk (`...teleport?colonist=<BID>&to=<TX>,<TY+1>`): he draws over the trunk and crown.
7. Click a crown (not the trunk; `/api/input` takes screenshot pixels): that tree alone is selected, with one outline around it and none on its neighbours.
8. Look at the landing at zoom 1.5: the ground around the colonists is a clearing, no trunk within a few meters of them.
9. Give Bob an axe (`curl.exe "http://127.0.0.1:8081/api/dev/give?material=AxePrimitive&n=1&where=colonist&slot=hands"`) and let him work, or pick Harvest in the Actions menu and click a tree: when a tree falls it is gone, a wood heap lies where it stood, and `what=colonists` shows Wood in his `hands`.
10. `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`.
