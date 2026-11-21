# Scenario 04: Build walls on foundation

**Status:** ready  
**Last verified:** 2026-11-20: the steps now numbered 22 to 33, and the loop-and-divider layout of 34 to 40 as one step with the second door on the divider, on 4d104363; steps 34 to 40 as now written not rerun; the earlier wording of the two-rooms steps, 5, 7, and 8 in part, and a variant of 1 to 4, on ab0f3eec

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

The crew lands west of the river, so put Bob on the east bank (`<BOB_ID>` is `colonists[0].id`):

```
curl.exe "http://127.0.0.1:<PORT>/api/dev/teleport?colonist=<BOB_ID>&to=12,8"
```

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
# -> at least one wall shows progress above 0 while its state is still "Blueprint" (at speed 3 one wall can finish between two polls), and all end "Built"
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
6. With the wall tool held over the creek at (7.9, 8), the status reads "not on buildable ground" and a click adds no point (`status.valid` false, `status.message` and `verdict.reason` "not on buildable ground", `status.pointCount` 0 after the click).
7. On the east bank, `walls?pts=9.9,5;9.9,8&host=0&close=0&built=0` and `spawn?def=BasicBox&at=9.9,8` both answer 200. The chain adds a fifth wall.
8. `ground?pts=7.9,8` reads `valid` true and `buildableNow` false, and Bob still walks through the creek.

---

## Expected state

```
GET /api/state?what=construction
  -> walls.length == 4 before step 7 and 5 after it; walls[0..3] are the Setup walls, walls[4] is step 7's
  -> each of walls[0..3]: state == "Built" && progress == 1.0
  -> each of walls[0..3]: material == "Wood"
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

The drawing steps below read `/api/state?what=drawing`. Their points are world points inside the built foundation (convert them to pixels with `what=camera`, [the camera view](../README.md#the-camera-view)); for the four-corner foundation of the precondition, take points a meter or more in from its edges on a foundation at least 6 m x 4 m (8 m x 6 m from (14, 0) to (22, 6) is buildable at the Quick Start landing). Centre the camera on it with `control?action=camera&x=18&y=3&zoom=3`, and again after each new game. A move out of the window is `ev=move,-5,-5`, which pauses a tool the way a real exit does.

9. Pick Wall in the Build menu, click two points on a built foundation (say (16, 2) and (20, 2)), and move the pointer out of the game's window: the band to the pointer, the marker, and its label go, and the two points and the band between them stay (`followingPointer` false, `pointer` and `snap` null, `points` still two, `status.hint` "Right-click / Enter to finish").
10. Move the pointer back over the foundation at a spot that is not the second point (say (20, 5)): the band follows it from the second point (`followingPointer` true), and a click places a third point (`status.pointCount` 3).
11. Double-click a fourth point: the chain ends, `?what=construction` lists three more wall segments, the strip reads "Click a foundation to start" (`status.hint`, `status.pointCount` 0), and no toast says the wall is too short.
12. Click two different points on the foundation a second apart: each places a point and the chain goes on (`status.pointCount` 2); Enter ends it (`status.pointCount` 0, one more wall segment).
13. With no point placed, press Esc: the wall tool closes (`tool` `none`). With a chain of two or more points Esc ends the wall first and keeps the tool; press Esc again to close it.
14. Open the top bar's Menu and pick Main Menu: the main menu shows.
15. Pick Settings and, under Drawing Settings, switch Angle readout on if it is off: it reads on.
16. Pick Back, then Quick Start: a new game shows, and `settings.angleReadout` is true.
17. Teleport Bob back to (12, 8) as in Setup, make the built foundation again with the precondition's `built=1` command and centre the camera on it again: `?what=construction` lists it `Built`.
18. Pick Wall in the Build menu and click (16, 3) and (19, 3) inside the built foundation, leaving the pointer on the second: the two points and the band between them show, nothing red, and no "wall too short" (`status.hint` "Right-click / Enter to finish", `verdict.proposed` false).
19. Move the pointer off at an angle that is not square: an arc and the angle between the two walls show at the middle point.
20. Read the new foundation's id from `?what=construction` (`foundations[0].id`, FOUND_ID for this game) and make a built wall on it: `walls?pts=15,5;21,5&material=Wood&thickness=Standard&host=<FOUND_ID>&built=1&close=0` (inside the 8 m x 6 m foundation from (14, 0) to (22, 6)): `?what=construction` lists the wall `Built`.
21. Pick Door in the Build menu, click the wall at (18, 5), and leave the pointer there: the door shows in the wall with no red ghost and no "openings overlap" (`verdict.proposed` false, `status.hint` "Click a wall to place").

---

## Two rooms sharing a wall

In a new Quick Start game, at speed 1 (nothing moves or meshes at speed 0). The second room reuses the first room's east wall, so that shared wall has the lower id where it meets the outer walls. Ids come from the views: the foundation's, the walls' (`walls[].id` with `x0,y0,x1,y1`), and the doors' from `?what=construction`; Eve's from `?what=colonists`, by `name`. Points are world points, turned into pixels with `what=camera` ([the camera view](../README.md#the-camera-view)). Centre the camera with `control?action=camera&x=28.5&y=5.5&zoom=12`, so no destination lies under the entity panel (a right-click under the panel gives no order).

22. `foundation?pts=12,1;32,1;32,8;12,8&material=Wood&built=1`: `?what=construction` lists it `Built`; its `id` is FOUND_ID.
23. `walls?pts=25,2;28,2;28,7;25,7&material=Wood&thickness=Standard&host=<FOUND_ID>&built=1&close=1`: four walls, all `Built`.
24. `walls?pts=28,2;31,2;31,7;28,7&material=Wood&thickness=Standard&host=<FOUND_ID>&built=1&close=1`: three more, seven in all, all `Built`; the wall from (28, 2) to (28, 7) has a lower id than every wall from step 24.
25. `opening?seg=<id of the wall from (25, 2) to (28, 2)>&type=Door&t=0.5&built=1`: a door at (26.5, 2), `Built`.
26. `opening?seg=<id of the wall from (28, 2) to (28, 7)>&type=Door&t=0.5&built=1`: a door at (28, 4.5), `Built`.
27. `colonist?at=29.5,10&name=Eve`: Eve is listed at (29.5, 10).
28. `select?colonist=<EVE_ID>`: her panel opens.
29. `control?action=pause`.
30. Click Draft: its position is `entity_info_draft`'s `screenBounds` centre in `/api/ui/tree` times `layoutScale` from `?what=uiscale`.
31. Right-click (29.5, 4.5).
32. `control?action=resume`: Eve was drafted where she stood, before wandering. Read her `x,y` until they stop changing: 28 < x < 31 and 2 < y < 7, inside the second room. On the way she passes north of the rooms (y < 2) and through both doors.
33. Right-click (29.5, 11): read her `x,y` until they stop changing: y > 7.5, outside the rooms, south of them.
The same rooms drawn as one loop and a divider, which here has the higher id:

34. Start a new Quick Start game and centre the camera as above.
35. Run step 22's `foundation` command in this game: `?what=construction` lists it `Built`; its `id` is FOUND_ID.
36. `walls?pts=25,2;31,2;31,7;25,7&material=Wood&thickness=Standard&host=<FOUND_ID>&built=1&close=1`: four walls, all `Built`.
37. `walls?pts=28,2;28,7&material=Wood&thickness=Standard&host=<FOUND_ID>&built=1&close=0`: seven walls, all `Built`; the divider splits the north and south walls at x = 28, and the divider from (28, 2) to (28, 7) has the highest id.
38. `opening?seg=<id of the north piece from (25, 2) to (28, 2)>&type=Door&t=0.5&built=1`: a door at (26.5, 2), `Built`.
39. `opening?seg=<id of the divider from (28, 2) to (28, 7)>&type=Door&t=0.5&built=1`: a door at (28, 4.5), `Built`.
40. Run steps 27 to 32 in this game: after step 32, Eve's `x,y` read 28 < x < 31 and 2 < y < 7, inside the second room.

**Pass:** steps 22 to 40 read as written. **Fail:** Eve stops anywhere else after step 32 or 40, for instance against the outside of the south wall near (28, 7.4), or `?what=colonists` never shows her inside the second room.

---

## Notes

The crew lands about 28 m west of the river, which blocks movement, and every point here is on the east bank, hence the teleport in Setup (repeat it after each new game).

`walls` defaults to `built=1` (instant); always pass `built=0` explicitly when you want a
blueprint that the colonist builds. Omitting it silently skips the scenario.

`close=1` adds a closing segment between the last and first point. For a 4-corner polygon this
produces 4 segments. For a 3-corner (triangle) footprint it produces 3.

Steps 32 and 40 depend on what Eve has seen. A wall counts as seen once its end shows at a corner, so a colonist who has wandered down both the west and the east side of the rooms knows both north walls without the door in them, and answers "no known way" to step 31's order: correct for what she knows. That is why steps 29 to 32 draft her before she moves; drafted that way, she must get in, and anything else is a fail.

On-water and off-navmesh wall placements are refused with the same error as foundations; see
[../regressions.md](../regressions.md).
