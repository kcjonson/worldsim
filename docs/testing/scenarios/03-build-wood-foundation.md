# Scenario 03: Build wood foundation over trees

**Status:** ready  
**Last verified:** 2026-11-13

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
this). A tree or bush placed with `/api/dev/spawn` blocks the footprint and is cleared the same way
as world-generated ones. Note the corner coordinates as `FX0,FY0` and `FX1,FY1`.

Blueprint the foundation (built=0 queues the colonist to build it):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/foundation?pts=<FX0>,<FY0>;<FX1>,<FY0>;<FX1>,<FY1>;<FX0>,<FY1>&material=Wood&built=0"
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
   regrow in place), even when Bob's arms are already full of wood, and no berries are delivered
   to the site. Use a bush the world generated, not a spawned one, and look at the screen
   (`control?action=camera&x=<bush x>&y=<bush y>&zoom=8`, then a screenshot): the bush is no
   longer drawn once the phase leaves `Clearing`, and none is drawn on the built foundation:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].delivered never has a "Berry" key
```

3. Phase moves to `AwaitingMaterials`. Bob chops nearby trees and provisions wood:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].phase == "AwaitingMaterials"
# -> foundations[0].delivered.Wood increases over time
```

4. Watch a fell during `AwaitingMaterials` that leaves a heap of logs at the stump: Bob's next trip is to that heap, not to another tree, and he carries it to the site (`delivered.Wood` rises) before he cuts again.

5. Phase moves to `UnderConstruction` once materials are delivered. Bob builds:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].phase == "UnderConstruction"
# -> foundations[0].progress increases toward 1.0
```

6. Keep Bob's needs up every ~2 real minutes:

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Hunger&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Thirst&value=95"
curl.exe "http://127.0.0.1:<PORT>/api/dev/need?colonist=<BOB_ID>&need=Energy&value=95"
```

7. Poll until foundation is `Built` at `progress == 1.0`:

```
curl.exe "http://127.0.0.1:<PORT>/api/state?what=construction"
# -> foundations[0].state == "Built"
# -> foundations[0].progress == 1.0
```

   Screenshot the built foundation: it sits on bare ground, with no bush, grass tuft or stone
   drawn on it.

8. Blueprint an L-shaped foundation beside the first (six points: an 8 m square with its north-east
   quarter cut away, `pts=X,Y;X+8,Y;X+8,Y+4;X+4,Y+4;X+4,Y+8;X,Y+8`), placed so a tree stands in the
   notch (`X+6,Y+6`) and grass grows there. Bob does not clear the notch's tree (no "Clearing ...
   from build site" for it), the site leaves `Clearing` with it standing (he may still cut it later
   for Wood, as any tree), and once built the notch's grass is still drawn while the floor is bare. A packaged box (one not yet placed) lying on its floor before the blueprint was drawn sits
   just outside the floor once building starts; one lying in the notch has not moved.

9. With the site built, give Bob an armful of wood (`/api/dev/give?material=Wood&n=10&where=colonist&slot=hands`): if no box in the world still takes Wood, the next time he wanders he puts it down as a heap where he stands (`colonists[0].hands` empties, no deposit anywhere); if a box does take Wood, he carries it there instead.

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

On-water footprints (or footprints that straddle water, a narrow stream included) are refused.
See the on-water placement regression in [../regressions.md](../regressions.md).
