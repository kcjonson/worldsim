# Scenario 03: Build wood foundation over trees

**Status:** ready  
**Last verified:** 2026-06-28

Verifies the full foundation construction lifecycle: a colonist clears on-footprint trees, chops
for materials, and builds a `Wood` foundation to completion, unaided, without freezing.

---

## Preconditions

Colonist has an `AxePrimitive`. Either run [01-craft-axe-and-box.md](./01-craft-axe-and-box.md)
first, or give the axe directly:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/give?material=AxePrimitive&n=1&where=colonist&slot=hands"
```

---

## Setup

Find Bob's position:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=colonists"
# -> colonists[0].x, colonists[0].y  (note as BX, BY)
```

Choose a ~3x3 area on valid land that has at least one or two trees and a berry bush
(`Flora_BerryBush`) standing on it (the forest around the Quick Start landing usually provides
this). They must be world-generated: `/api/dev/spawn` makes runtime entities the clearing gate
doesn't see. Note the corner coordinates as `FX0,FY0` and `FX1,FY1`.

Blueprint the foundation (built=0 queues the colonist to build it):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/foundation?pts=<FX0>,<FY0>;<FX1>,<FY1>&material=Wood&built=0"
```

Note the foundation `id` from `?what=construction` for later assertions:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> construction.foundations[0].id  (note as FOUND_ID)
```

Accelerate time:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/time?speed=3"
```

---

## Steps

1. Poll construction until the phase moves through `Clearing`. Bob should fell any trees on the
   footprint; they disappear (one action each):

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].phase == "Clearing"  (initial)
# -> wait until phase != "Clearing"
```

2. The berry bush on the footprint is cleared too: it disappears after one harvest (it doesn't
   regrow in place), even when Bob's arms are already full of wood, and Bob never shows
   `Delivering Berry to build site`:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=colonists"
# -> colonists[0].action never "Delivering Berry to build site"
```

3. Phase moves to `AwaitingMaterials`. Bob chops nearby trees and provisions wood:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].phase == "AwaitingMaterials"
# -> foundations[0].delivered.Wood increases over time
```

4. Phase moves to `UnderConstruction` once materials are delivered. Bob builds:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].phase == "UnderConstruction"
# -> foundations[0].progress increases toward 1.0
```

5. Keep Bob's needs up every ~2 real minutes:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Energy&value=95"
```

6. Poll until foundation is `Built` at `progress == 1.0`:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].state == "Built"
# -> foundations[0].progress == 1.0
```

---

## Expected state

```
GET /api/state?what=construction
  -> foundations[0].state == "Built"
  -> foundations[0].progress == 1.0
  -> foundations[0].material == "Wood"
  -> foundations[0].phase == "Complete"  (or equivalent terminal phase)
```

---

## Pass / fail

**Pass:** foundation reaches `state == "Built"` and `progress == 1.0` with no manual
intervention beyond need top-ups. All four phases completed: Clearing -> AwaitingMaterials ->
UnderConstruction -> Built.

**Fail:** Bob freezes in any phase without progressing. Note the phase and `colonists[0].action`
value at the time of freeze. Cross-check against the harvest-reachability regression in
[../regressions.md](../regressions.md).

---

## Notes

The footprint need not be a perfect rectangle; the `pts` param accepts any polygon as
semicolon-separated `x,y` pairs. A simple `FX0,FY0;FX1,FY0;FX1,FY1;FX0,FY1` rectangle works.

On-water footprints (or footprints that straddle water) are refused. See the on-water placement
regression in [../regressions.md](../regressions.md).
