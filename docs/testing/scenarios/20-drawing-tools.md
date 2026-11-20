# Scenario 20: Drawing tools

**Status:** ready  
**Last verified:** 2026-11-20, in two runs: steps 1 to 32 with the crew landing west of the river, before steps 36 to 41 existed; and steps 36 to 41 (the wall, door and window by hand, from a game started through Quick Start after a game with a step 22 triangle in it, as step 28 does). Steps 33 to 35 are person-only and were not run

Verifies what scenarios 03 and 04 leave out of the drawing tools: the Rectangle tool's quieter edge cases, Freeform with every helper off, each Drawing Setting no other scenario turns on, and the key guide's rows for the opening and Add tools and for the helper legend.

---

## Preconditions

A fresh launch of world-sim with no game started (the settings below are changed from the main menu and hold for the process, so this scenario runs on its own launch). No colonist or item is needed.

---

## Setup

Launch without `--scene=game`, so the splash and main menu show:

```
build\apps\world-sim\RelWithDebInfo\world-sim.exe --http-port <PORT> --background --no-settings
```

Pick Enter Expedition, then Quick Start. The menus' controls have ids in `/api/ui/tree`: `btn_enter_expedition` on the splash, `menu_quick_start` and `menu_settings` on the main menu, and in Settings `settings_back`, a switch per `drawing_<name>_toggle` (`drawing_align_existing_toggle`, `drawing_round_lengths_toggle`, `drawing_guides_from_world_axes_toggle` and the rest) and `drawing_grid_spacing`, whose 0.5 meters segment has no id and is clicked by its text. A switch's row spans the width, so click the switch at the row's left. Poll `?what=colonists` until it answers 200, then centre the camera and read it:

```
curl.exe "http://127.0.0.1:<PORT>/api/control?action=camera&x=40&y=10&zoom=3"
curl.exe "http://127.0.0.1:<PORT>/api/state?what=camera"
```

Every point below is a world point (x, y in meters) on the dry ground east of the creek. Convert it to the pixel `/api/input` takes with the formulas in [the camera view](../README.md#the-camera-view), and read `?what=drawing` after each step. Compare readbacks as numbers (the view prints three decimals: 36 reads 36.000). A tool's click lands on its snap, so a point with a fraction in it reads back to the nearest millimeter or the grid.

---

## Steps

1. Pick Build, then Foundation: `tool` is `foundationRectangle`, `status.hint` is "Click or drag from a corner".
2. Click (40, 10), then press Backspace: `rectangle.firstCorner` is null and `points` is empty.
3. Click (40, 10), then right-click open ground, say (44, 12): the same.
4. Click (40, 10), then click (44, 10), level with it: no rectangle, the first corner waits, no toast, nothing red (`rectangle.corners` empty, `rectangle.firstCorner` still (40, 10)).
5. Click (40, 13), level with the corner the other way: the same.
6. Press Esc: `rectangle.firstCorner` is null and `tool` is still `foundationRectangle`.
7. Pick Freeform in the strip's picker: `tool` is `foundationFreeform`, `status.hint` is "Ready".
8. Click (40, 10), then move to (45.37, 10.31): `snap.lengthSnap` is `Rounded` and `snap.segmentLengthMeters` is 5.400.
9. Click there, then press Backspace: `points` has two points, then one.
10. Click (46, 12) and (44, 15), then press Esc: `status.pointCount` is 3, then 0 with `tool` still `foundationFreeform`. Press Esc again: `tool` is `none`.
11. Pick Build, then Foundation (Freeform opens again). Click (40, 10), (46, 10) and (43, 14), then move to (40.2, 10.2): `snap.closesShape` is true and `status.hint` is "Click origin to close". Click: `?what=construction` lists only that one foundation, with `"area":12`.
12. Pick Build, then Wall: the strip shows the material and thickness cards and no Rectangle or Freeform cards.
13. Pick Build, then Door, and move the pointer to (45, 12): the strip reads "Opening: Door" with no Rectangle or Freeform cards, the key guide lists two rows, "Click a wall" and "Esc / Right-click", and `status.hint` is "no wall in range". Pick Window: the same with "Opening: Window".
14. Move the pointer out of the window (`ev=move,-5,-5`, raw window pixels, not a world point): `followingPointer` is false and `status.hint` is "Hover a wall". Move back over the ground: "no wall in range". Press Esc: `tool` is `none`.
15. Click the foundation at (43, 11) (a tree on the spot takes the first click, so click again to step to the foundation), then pick Add in its panel: `tool` is `foundationEdit`, `status.hint` is "Draw a region across the foundation edge", the strip shows no Rectangle or Freeform cards, and the key guide lists the Freeform rows with "Close the shape and apply it (the tool stays on for the next)" on the second.
16. Move the pointer out of the window and back: `followingPointer` is false with `pointer` null, then true.
17. Click (44, 9) and leave the pointer on it: no red reason beside the cursor, `status.hint` is "Keep placing points" and `verdict.proposed` is false. Press Backspace.
18. Press `?` (inject `char,63`): `keyGuide.collapsed` is true and the guide is a "Keys ?" chip. Press Esc once: `tool` is `none`.
19. Open the top bar's Menu, pick Main Menu, then Settings: the Drawing Settings section shows.
20. Switch Align to existing buildings on and Round lengths to 0.1 m off, and pick 0.5 meters under Grid spacing: the two switches read on and off, and 0.5 meters is the picked segment.
21. Pick Back, then Quick Start: a new game shows, and `settings.roundLengths` is false and `settings.rectangleGridSpacingMeters` is 0.500.
22. Centre the camera again, then make a triangle with a slanting edge as a built foundation (`curl.exe "http://127.0.0.1:<PORT>/api/dev/foundation?pts=30,10;36,10;33,14&material=Wood&built=1"`, quoted so the shell leaves the `;` alone): `?what=construction` lists it `Built`.
23. Pick Build, then Foundation (Rectangle opens, the first pick of this game), and move to (46.3, 8.2): `snap.point` is (46.5, 8). Click, then move to (50.8, 12.4): `rectangle.sizeLabel` is "4.5 m x 4.5 m". Press Esc.
24. Pick Freeform and click (40, 12), then move to (46.37, 12.31): `snap.lengthSnap` is `None` and `snap.segmentLengthMeters` is not a multiple of 0.1.
25. Move to (41.5, 10), on the line from the point parallel to the triangle's slanting edge: `snap.guides[0].source` is `NearbyStructure` and its `relation` is `Parallel`; a guide line shows. Press Esc twice: `tool` is `none`.
26. Open the top bar's Menu, pick Main Menu, then Settings: the Drawing Settings section shows.
27. Switch Align to existing buildings off, and Guides from the world axes, Crossings, Common lengths, Guide distances and Angle readout on: each reads as set. Leave Round lengths off (step 29's exact 6.500 needs it) and the grid as it is.
28. Pick Back, then Quick Start: a new game shows, and `settings.guidesFromWorldAxes` is true and `settings.roundLengths` is still false.
29. Centre the camera again, pick Build, Foundation, then Freeform. Click (36, 8), then move to (36.1, 14.5): `snap.guides[0].source` is `WorldAxes` and its `distanceFromNodeMeters` is 6.500, and a number reading `6.50 m` sits on the guide.
30. Click (42, 12), then move to (36.1, 12.1): `snap.kind` is `GuideCrossing` and `snap.point` is (36, 12), and a dot marks the crossing with both guides drawn.
31. Look at the key guide: under the six Freeform rows it lists "Lined up with something", "A square corner", "Two guides cross", "= and ticks: same length as an edge already built" and "Arc and degrees: the corner's angle", each with its mark, then the "? or H hides this" line.
32. Press Esc twice, then pick Build, Foundation, and Rectangle in the picker: the key guide lists the four Rectangle rows and none of those five lines. Press Esc: `tool` is `none`.
33. Person only: pick Build, Foundation, then Freeform, click a point, hold Alt and move the pointer: the point goes where the pointer is, with no guide and no rounding.
34. Person only: pick Rectangle in the picker, hold Alt and move the pointer between two grid points: the corner lands on the raw pointer, off the grid, and no grid cross shows.
35. Person only: pick Wall, hold Ctrl and click the near edge of a foundation: a wall is queued along the whole edge. Then press Esc until the tool closes.
36. Press Esc until `tool` is `none`. Turn reveal on (`/api/dev/reveal?on=1`), make a built foundation on clear ground (`/api/dev/foundation?pts=18,8;32,8;32,18;18,18&material=Wood&built=1`) and read its id from `?what=construction` (`foundations[0].id`, FOUND_ID), then a wall blueprint on it (`/api/dev/walls?pts=20,10;30,10&material=Wood&thickness=Standard&host=<FOUND_ID>&built=0&close=0`): `?what=construction` lists the wall `Blueprint`. Note its `id` (WALL_ID) and `entity` (WALL_ENTITY).
37. Pick Build, then Door, and move the pointer onto the wall (send two moves, the first only wakes the pointer): `status.hint` is "Click a wall to place", `opening.snap.wall` is WALL_ID, and the door ghost shows on the blueprint wall.
38. Click: a "Door blueprint" toast shows, `?what=construction` lists one opening `Blueprint` on that wall (note its `entity`, DOOR_ENTITY), and the wall's ghost band has a gap at the door.
39. Pick Build, then Window, and click the wall 3 m along from the door: a second opening, `Window` (note its `entity`, WINDOW_ENTITY), `Blueprint` on the same wall. `/api/ui/lint` reads `"count":0`.
40. Press Esc (`ev=keydown,escape`, then read `tool`; if it still reads `opening`, send Esc again): `tool` is `none`. Complete the wall (`/api/dev/complete?id=<WALL_ENTITY>`): the wall reads `Built`, both openings stay `Blueprint`, and the gaps in the wall stay.
41. Complete the openings (`/api/dev/complete?id=<DOOR_ENTITY>`, then `<WINDOW_ENTITY>`): both read `Built`, and the door and window draw in their gaps.

---

## Expected state

```
GET /api/state?what=drawing
  -> tool == "none"
  -> settings.guidesFromWorldAxes == true && settings.guideCrossings == true
  -> settings.roundLengths == false && settings.rectangleGridSpacingMeters == 0.5
  -> settings.alignToExistingBuildings == false
```

---

## Pass / fail

**Pass:** every step's readback and what shows on screen match.

**Fail:** the first step where one differs; note the `drawing` view's value and a screenshot.

---

## Notes

Settings open only from the main menu and last for the launch, so the steps visit them twice: Align to existing buildings, Round lengths and the grid first (the foundation from step 22 gives Align a building to line up with), then the helpers that need no building. Round lengths stays off through the second visit. Steps 19 to 21 and 26 to 28 lose the game they leave, which is why step 22 re-makes a foundation. A new game opens with the key guide unfolded.

Crossings needs at least one guide source on; the world axes are that source here. Steps 33 to 35 need a held modifier key, which injected input cannot send.
