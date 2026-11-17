# Scenario 04: Build walls on foundation

**Status:** ready  
**Last verified:** 2026-11-13

Verifies that a colonist builds all four wall segments on an existing foundation to completion,
unaided.

---

## Preconditions

- A `Built` foundation exists. Either run [03-build-wood-foundation.md](./03-build-wood-foundation.md)
  first, or blueprint and instantly-complete one:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/foundation?pts=<FX0>,<FY0>;<FX1>,<FY0>;<FX1>,<FY1>;<FX0>,<FY1>&material=Wood&built=1"
```

- Colonist has an `AxePrimitive` (needed to source wall materials):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=AxePrimitive&n=1&where=colonist&slot=hands"
```

---

## Setup

Get the foundation `id` from state (needed for `host=`):

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> construction.foundations[0].id  (note as FOUND_ID)
```

Use the same four corners as the foundation. Blueprint walls as
a closed polygon (built=0, close=1):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/walls?pts=<FX0>,<FY0>;<FX1>,<FY0>;<FX1>,<FY1>;<FX0>,<FY1>&material=Wood&thickness=Standard&host=<FOUND_ID>&built=0&close=1"
```

Note the wall count from `?what=construction`:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> construction.walls — expect 4 segments for a 4-corner polygon with close=1
```

Accelerate time:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/time?speed=3"
```

---

## Steps

1. Confirm 4 wall segments appear in blueprint state:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> construction.walls.length == 4
# -> each wall: state == "Blueprint", progress == 0
```

2. Watch Bob source wood and build. Poll walls until all segments progress:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> walls[N].state == "UnderConstruction", walls[N].progress > 0
```

3. Keep Bob's needs up every ~2 real minutes:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Energy&value=95"
```

4. Poll until all 4 walls are `Built`:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> all walls[N].state == "Built" and walls[N].progress == 1.0
```

5. At the Quick Start creek, `walls?pts=6.5,8;9.5,8&host=0&close=0&built=0` and `spawn?def=BasicBox&at=7.9,8` both answer 400 and create nothing.
6. With the wall tool held over the creek at (7.9, 8), the status reads "not on buildable ground" and a click adds no point.
7. On the east bank, `walls?pts=9.9,5;9.9,11&host=0&close=0&built=0` and `spawn?def=BasicBox&at=9.9,8` both answer 200.
8. `ground?pts=7.9,8` reads `valid` true and `buildableNow` false, and Bob still walks through the creek.

---

## Expected state

```
GET /api/state?what=construction
  -> walls.length == 4
  -> walls.every(w => w.state == "Built" && w.progress == 1.0)
  -> walls.every(w => w.material == "Wood")
```

---

## Pass / fail

**Pass:** all 4 wall segments reach `state == "Built"` and `progress == 1.0` without manual
intervention beyond need top-ups.

**Fail:** Bob freezes or repeats "Waiting for the area to settle" — check the wall-trap recovery
regression in [../regressions.md](../regressions.md). Note `colonists[0].action` and which wall
segment has stalled (`state`, `progress`).

---

## Drawing a chain with the wall tool

Run these after the pass / fail check above: they add blueprint wall segments, which would break its every-wall-built check.

9. Pick Wall in the Build menu, click two points on a built foundation, and move the pointer out of the game's window: the band to the pointer, the marker, and its label go, and the two points and the band between them stay.
10. Move the pointer back over the foundation: the band follows it from the second point, and a click places a third point.
11. Double-click a fourth point: the chain ends, `?what=construction` lists three more wall segments, the strip reads "Click a foundation to start", and no toast says the wall is too short.
12. Click two different points on the foundation a second apart: each places a point and the chain goes on; Enter ends it.

---

## Notes

`walls` defaults to `built=1` (instant); always pass `built=0` explicitly when you want a
blueprint that the colonist builds. Omitting it silently skips the scenario.

`close=1` adds a closing segment between the last and first point. For a 4-corner polygon this
produces 4 segments. For a 3-corner (triangle) footprint it produces 3.

On-water and off-navmesh wall placements are refused with the same error as foundations; see
[../regressions.md](../regressions.md).
