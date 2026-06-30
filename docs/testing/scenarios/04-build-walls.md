# Scenario 04: Build walls on foundation

**Status:** ready  
**Last verified:** 2026-06-28

Verifies that a colonist builds all four wall segments on an existing foundation to completion,
unaided.

---

## Preconditions

- A `Built` foundation exists. Either run [03-build-wood-foundation.md](./03-build-wood-foundation.md)
  first, or blueprint and instantly-complete one:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/foundation?pts=<FX0>,<FY0>;<FX1>,<FY1>&material=Wood&built=1"
```

- Colonist has an `AxePrimitive` (needed to source wall materials):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=AxePrimitive&n=1&where=colonist"
```

---

## Setup

Get the foundation `id` from state (needed for `host=`):

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> construction.foundations[0].id  (note as FOUND_ID)
```

Use the same corner coordinates as the foundation (`FX0,FY0` and `FX1,FY1`). Blueprint walls as
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

## Notes

`walls` defaults to `built=1` (instant); always pass `built=0` explicitly when you want a
blueprint that the colonist builds. Omitting it silently skips the scenario.

`close=1` adds a closing segment between the last and first point. For a 4-corner polygon this
produces 4 segments. For a 3-corner (triangle) footprint it produces 3.

On-water and off-navmesh wall placements are refused with the same error as foundations; see
[../regressions.md](../regressions.md).
