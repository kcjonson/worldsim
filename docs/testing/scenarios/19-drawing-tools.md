# Scenario 19: Drawing tools

**Status:** ready

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

Pick Enter Expedition, then Quick Start. Poll `?what=colonists` until it answers 200, then centre the camera and read it:

```
curl.exe "http://127.0.0.1:<PORT>/api/control?action=camera&x=40&y=10&zoom=3"
curl.exe "http://127.0.0.1:<PORT>/api/state?what=camera"
```

Every point below is a world point (x, y in meters) on the dry ground east of the creek. Convert it to the pixel `/api/input` takes with the formulas in [the camera view](../README.md#the-camera-view), and read `?what=drawing` after each step. A tool's click lands on its snap, so a point with a fraction in it reads back to the nearest millimeter or the grid.

---

## Steps

1. Pick Build, then Foundation: `tool` is `foundationRectangle`, `status.hint` is "Click or drag from a corner".
2. Click (40, 10), then press Backspace: `rectangle.firstCorner` is null and `points` is empty.
3. Click (40, 10), then right-click open ground: the same.
4. Click (40, 10), then click (44, 10), level with it: no rectangle (`rectangle.corners` is empty), `rectangle.firstCorner` is still (40, 10), `status.valid` true with an empty `status.message`, no toast and nothing red; `?what=construction` lists no foundation.
5. Click (40, 13), level with the corner the other way: the same.
6. Press Esc: `rectangle.firstCorner` is null and `tool` is still `foundationRectangle`.
7. Pick Freeform in the strip's picker: `tool` is `foundationFreeform`, `status.hint` is "Ready".
8. Click (40, 10), then move to (45.37, 10.31): `snap.lengthSnap` is `Rounded` and `snap.segmentLengthMeters` is 5.400.
9. Click there, then press Backspace: `points` has two points, then one.
10. Click (46, 12) and (44, 15), then press Esc: `status.pointCount` is 3, then 0, and `tool` is still `foundationFreeform`. Press Esc again: `tool` is `none`.
11. Pick Build, then Foundation (Freeform opens again). Click (40, 10), (46, 10) and (43, 14), then move to (40.2, 10.2): `snap.kind` is `Origin`, `snap.closesShape` true, `status.hint` "Click origin to close". Click: `?what=construction` lists one foundation with `"area":12`.
12. Pick Build, then Wall: the strip shows the material and thickness cards and no Rectangle or Freeform cards.
13. Pick Build, then Door, and move the pointer to (45, 12): the strip reads "Opening: Door" beside the Wood card and shows no Rectangle or Freeform cards, the key guide lists two rows, "Click a wall" and "Esc / Right-click", and `status.hint` is "no wall in range". Pick Window: the same.
14. Move the pointer out of the window (`ev=move,-5,-5`): `followingPointer` is false, `pointer` is null and `status.hint` is "Hover a built wall". Move back over the ground: `status.hint` is "no wall in range". Press Esc: `tool` is `none`.
15. Click the foundation at (43, 11) (a tree on the spot takes the first click, so click again to step to the foundation), then pick Add in its panel: `tool` is `foundationEdit`, `status.hint` is "Draw a region across the foundation edge", the strip shows no Rectangle or Freeform cards, and the key guide lists the Freeform rows with "Close the shape and apply it (the tool stays on for the next)" on the second.
16. Move the pointer out of the window and back: `followingPointer` is false with `pointer` null, then true.
17. Press `?` (inject `char,63`): `keyGuide.collapsed` is true and the guide is a "Keys ?" chip. Press Esc twice: `tool` is `none`.
18. Open the top bar's Menu, pick Main Menu, then Settings. Switch Align to existing buildings on and Round lengths to 0.1 m off, and pick 0.5 meters under Grid spacing. Pick Back, then Quick Start: `keyGuide.collapsed` is false (a new game opens the guide unfolded), and `settings` reads `alignToExistingBuildings` true, `roundLengths` false and `rectangleGridSpacingMeters` 0.500.
19. Centre the camera again, then blueprint a triangle with a slanting edge as a built foundation: `foundation?pts=30,10;36,10;33,14&material=Wood&built=1`.
20. Pick Build, then Foundation (Rectangle opens, the first pick of this game), and move to (46.3, 8.2): `snap.kind` is `Grid` and `snap.point` is (46.5, 8). Click, then move to (50.8, 12.4): `rectangle.sizeLabel` is "4.5 m x 4.5 m". Press Esc.
21. Pick Freeform and click (40, 12), then move to (46.37, 12.31): `snap.lengthSnap` is `None` and `snap.segmentLengthMeters` is not a multiple of 0.1.
22. Move to (41.5, 10), on the line from the point parallel to the triangle's slanting edge: `snap.kind` is `GuideLine` and `snap.guides` has one guide with `source` `NearbyStructure` and `relation` `Parallel`; a dotted guide shows. Press Esc twice: `tool` is `none`.
23. Open the top bar's Menu, pick Main Menu, then Settings. Switch Align to existing buildings off, and Guides from the world axes, Crossings, Common lengths, Guide distances and Angle readout on. Pick Back, then Quick Start: `settings` reads those five true and `alignToExistingBuildings` false.
24. Centre the camera again, pick Build, Foundation, then Freeform. Click (36, 8), then move to (36.1, 14.5): `snap.kind` is `GuideLine`, `snap.guides` is one guide with `source` `WorldAxes` and `distanceFromNodeMeters` 6.500, and a number reading `6.50 m` sits on the guide.
25. Click (42, 12), then move to (36.1, 12.1): `snap.kind` is `GuideCrossing`, `snap.guides` has two `WorldAxes` guides, `snap.point` is (36, 12), and a dot marks the crossing with both guides drawn.
26. Look at the key guide: under the six Freeform rows it lists "Lined up with something", "A square corner", "Two guides cross", "= and ticks: same length as an edge already built" and "Arc and degrees: the corner's angle", each with its mark.
27. Press Esc twice, then pick Foundation and Rectangle in the picker: the key guide lists the four Rectangle rows and none of those five lines.
28. Press Esc until the tool closes. Person only: pick Freeform, click a point, hold Alt and move the pointer: the point goes where the pointer is, with no guide and no rounding, and `snap.kind` is `None`.
29. Person only: pick Wall, hold Ctrl and click the near edge of a foundation: a wall is queued along the whole edge (`?what=construction` lists a new wall segment on it).

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

Settings open only from the main menu and last for the launch, so the steps visit them twice: Align to existing buildings, Round lengths and the grid first (the foundation from step 19 gives Align a building to line up with), then the helpers that need no building. Steps 18 and 23 lose the game they leave, which is why step 19 re-makes a foundation.

Crossings needs at least one guide source on; the world axes are that source here. Steps 28 and 29 need a held modifier key, which injected input cannot send.
