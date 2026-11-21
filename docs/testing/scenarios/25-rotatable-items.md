# Scenario 25: rotatable items

**Status:** draft  
**Last verified:** 2026-11-21, steps 1 to 5, 7, 8, 10 to 15, 17 to 21, 23 to 26, 28, 29, 32 and 33; 9 and 16 the same day before PR 542 merged (6: the second box's spawn was moved off the first's collider, so they didn't overlap; 22, 30 and 31 need a held Alt, which injected input can't send; 27 not run); 34 to 56 on 2026-11-21 after PR 543 merged

A parts item draws at any turn with the faces the turn shows, sorts against a colonist and another item along its whole length, doesn't pop at the edge of the screen, draws its detail levels as the camera zooms, selects by its turned shape, and is turned while placing it with Q/E or a drag, snapped square to a wall and flush against it. Placed, it is an obstacle colonists walk round; a ghost over another item, across a wall, or in a clearance is refused with the reason, and a wall can't be drawn through an item or its clearance. Spec: [rotatable-placed-items.md](../../technical/rendering/rotatable-placed-items.md), sections 3, 6.8, 8, 9, 10, 11, 13, 14, and 15.

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev server on 127.0.0.1:8081, or the port given with `--http-port`; `<PORT>` below is that port. Then `curl.exe "http://127.0.0.1:<PORT>/api/dev/reveal?on=1"`.

The test item is `TestPartsBox`: a box 1.2 m wide (local x), 0.6 m deep and 0.8 m tall, front red with an F, back blue with a B, left end green with an L, right end orange with an R, and an arrow on its top pointing to the front. It has three detail levels: at 8 px/m the letters drop, and from 80 px/m a white dot shows in each corner of every side. A spawn is `curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=TestPartsBox&at=<x>,<y>&n=1&scatter=0&turn=<degrees>"`: `at` is the centre of the box's 1.2 by 0.6 m footprint, and `turn` is degrees clockwise with the front down the screen at 0. A spawn that refuses its spot answers HTTP 400 naming water or an obstacle: move the spot a metre and try again. Steps 3 to 6 put Bob and a second box at offsets from that centre.

Read Bob's `id`, `x` and `y` from `/api/state?what=colonists` (BID, BX, BY) and pick a row of open ground near him; call its west end RX,RY.

Obstacles and refusals (steps 34 on), at the Quick Start landing on open ground clear of trees: kill every box; spawn a `TestPartsBox` at `30,27` turned 30 and a `TestClearanceBench` (a bench 1.6 m by 0.5 m keeping a strip 0.6 m deep clear along its front, +y at turn 0) at `30,31` turned 0; `package?def=TestPartsBox&at=24,31&target=37,31.5` (a crate waiting to be installed at 37,31.5); `walls?pts=38.5,25;38.5,29&host=0&built=1`; `foundation?pts=26,30;40,30;40,33;26,33&material=Wood&built=1`. Select Bob, click Draft in his panel, and `teleport?colonist=<BID>&to=27,27`. To order him to a point, jump the camera there at zoom 6 and right-click the window's centre. After step 35, `time?speed=0`, so no colonist installs the crate.

Placing (steps 19 on): `curl.exe "http://127.0.0.1:<PORT>/api/dev/place?def=TestPartsBox"` starts placing it; pointer moves, keys, presses and releases go in through `/api/input` (`ev=move,<x>,<y>`, `ev=keydown,e`, one key a request, `ev=down,<x>,<y>`, `ev=up,<x>,<y>`, `ev=click,<x>,<y>,right`). `/api/state?what=placement` reads the ghost's `turnDegrees` and position and lists placed parts items with their turns. The wall of steps 26 to 31 is `/api/dev/walls?pts=<WX>,<WY>;<WX+5.64>,<WY+2.05>&host=0&built=1`, 6 m at 20 degrees on open ground. Injected input can't hold Alt: steps 22, 30 and 31 are done on the keyboard. Exit is `curl.exe "http://127.0.0.1:<PORT>/api/control?action=exit"`.

---

## Steps

1. Spawn six boxes along the row, 3 m apart, at turns 0, 35, 90, 180, 271 and 325 (`at=<RX>,<RY>`, `<RX+3>,<RY>` and so on), then `curl.exe "http://127.0.0.1:<PORT>/api/control?action=camera&x=<RX+7.5>&y=<RY>&zoom=14"`: at 0 the red front and the arrow pointing down; at 35 red front and orange right end, arrow down and to the left; at 90 only the orange right end, arrow left; at 180 only the blue back, arrow up; at 271 the green left end and a red sliver; at 325 green left end and red front, arrow down and to the right.
2. Teleport Bob out of view, then `curl.exe "http://127.0.0.1:<PORT>/api/state?what=uprights"`: `itemDrawCalls` is 6, plus at most two for each tree that stands among a box's strips and overlaps it.
3. Kill all but one box and spawn one at turn 45 on bare ground, its centre at BX2,BY2. Its high end is the west end. Teleport Bob just in front of it (`curl.exe "http://127.0.0.1:<PORT>/api/dev/teleport?colonist=<BID>&to=<BX2-0.42>,<BY2+0.4>"`) and pause at once (`...control?action=pause`): Bob draws over the box's red front.
4. Resume, teleport Bob behind the high end (`to=<BX2-0.42>,<BY2-0.85>`), pause: the box draws over his legs.
5. The same at the low end: in front (`to=<BX2+0.42>,<BY2+0.85>`) Bob draws over the orange end; behind (`to=<BX2+0.42>,<BY2-0.4>`) the box draws over him.
6. Move Bob away. Spawn a box at turn 0 with its centre at `<BX2-0.5>,<BY2+1.05>`: it draws over the 45 degree box's front. Kill it and spawn one at `<BX2-0.7>,<BY2-0.95>`: the 45 degree box draws over its front.
7. With Bob walking among the six boxes of step 1, read `what=uprights` a few times: `itemDrawCalls` rises by at most the number of boxes he sorts among, never more.
8. Move the camera so a turned box stands half across the left, right, and bottom edges of the screen in turn: the part on screen draws every time; nothing pops in as the camera moves.

Steps 9 to 12 start from a box spawned at turn 35 on bare ground, its centre SX,SY, and the camera on it at zoom 25 (`...control?action=camera&x=<SX>&y=<SY>&zoom=25`).

9. Look at the box: a small white dot shows in each corner of every side it shows.
10. Read `what=uprights`: `viewLevels` lists the boxes at `pxPerMeter` 80.
11. Zoom to 3: the dots are gone, the letters stay, and `viewLevels` reads 24.
12. Zoom to 1: the letters are gone too, `viewLevels` reads 8, and no box is missing.

Steps 13 and 14 start with the camera on SX,SY at zoom 14. The middle of the window is half the `window` size from `what=camera`.

13. Click the middle of the window: the selection panel reads TestPartsBox, Placed, and a yellow outline follows the turned box's top and the sides it shows.
14. Click 160 pixels right of the middle of the window, on bare ground: the selection clears.

Step 15 starts with a second box spawned at turn 0 at `<SX+0.6>,<SY+0.7>`, south of the first and overlapping it on the screen, and the camera on `<SX+0.3>,<SY+0.35>` at zoom 25.

15. Click the middle of the window: the box at turn 0, drawn in front there, is selected, not the 35 degree box behind it.

Step 16 starts with the 45 degree box of step 3 selected by a click on its top and Bob teleported in front of its high end as in step 3.

16. Pause (`...control?action=pause`): Bob draws over the outline where he stands in front of the box, and the rest of the outline draws over the ground and the box.

Step 17 starts with game time stopped (`...dev/time?speed=0`; `action=pause` would hold the click too) and the camera at zoom 14 on Bob's `x` and `y` from `what=colonists`.

17. Click the middle of the window, on Bob: Bob is selected and outlined as before.
18. `curl.exe -i "http://127.0.0.1:<PORT>/api/dev/spawn?def=TestPartsBox&at=<RX>,<RY>&turn="`: HTTP 400 saying `turn=` is empty.

Steps 19 on start with game time running again (`...dev/time?speed=1`).

19. Kill every box and start placing a `TestPartsBox` over open ground: a see-through box follows the pointer, red front and arrow down, turn 0.
20. Press E twice: the ghost turns clockwise to 30, front and orange right end showing.
21. Press Q: 15.
22. Hold Alt and press E: 16. Let go and press E: 30.
23. Press on the ground and drag 100 px right: the ghost stays at the press and turns its red front to the pointer, 270.
24. Release: the box is placed there at 270.
25. Place again: the ghost starts at 270.
26. With the 20 degree wall built, hold the ghost 1 m south of its middle and press E round: it stops at 20, 110, 200 and 290 among the 15 degree steps.
27. Drag to within 7.5 degrees of one of those four: the turn snaps to it.
28. Turned square to the wall, bring the long side within 0.3 m of the face: it jumps flush against the face.
29. Move the pointer along the wall: the ghost stays flush.
30. Hold Alt and drag: the turn follows the pointer with no snap.
31. Under Alt, turned parallel to the wall: it still slides flush.
32. Press on the ground, drag onto the HUD and release there, then move over the ground: the ghost follows the pointer and doesn't turn.
33. Press on the ground and right-click while holding it, then release the left button: placement ends and nothing is selected.
34. Order Bob to `33,27`, past the box on a straight line through it: he walks round it, never through it.
35. Order him back to `27,27`: he walks round it again.
36. Start placing a `TestPartsBox`: its ghost follows the pointer.
37. Press E until `what=placement` reads `turnDegrees` 0: the ghost's red front faces down.
38. Move the pointer onto the box at `30.3,27`: the ghost reads red with "Overlaps an item" beside it (`what=placement` `valid` false, `reason` "Overlaps an item").
39. Move it onto the crate's target at `37.3,31.5`: red with "Overlaps an item".
40. Move it across the built wall at `38.5,27`: red with "A wall is in the way".
41. Move it in front of the bench at `30,31.6`: red with "Too close".
42. Move it to `30,32.6`, past the bench's clearance: no label, `valid` true.
43. Move it to `30,32.4`: it slides to `30,32.15`, flush against the clearance's edge, and stays valid.
44. Move it to `31.25,31`: it slides to `31.4,31`, flush beside the bench's body, and stays valid.
45. Press Esc: placement ends.
46. Pick Wall in the Build menu: the wall tool is active (`what=drawing` `tool` `wall`).
47. Click `35,31.5` on the foundation: one point placed.
48. Move the pointer to `39,31.5`, through the crate's target: the band reads red with "an item or its clearance is in the way" (`what=drawing` `status.valid` false).
49. Click there: no point is added (`status.pointCount` 1).
50. Press Esc: the point is dropped (`status.pointCount` 0).
51. Click `27,31.6`: one point placed.
52. Move the pointer to `33,31.6`, through the bench's clearance and clear of its body: red with the same reason.
53. Click there: no point is added (`status.pointCount` 1).
54. Press Esc until the wall tool closes (`tool` `none`).
55. `spawn?def=TestPartsBox&at=30.2,27&n=1&scatter=0`, into the box: a 400 naming "Overlaps an item".
56. `spawn?def=TestPartsBox&at=30,31.6&n=1&scatter=0`, into the bench's clearance: a 400 naming "Too close".
57. Start placing `TestPartsAreas` (`curl.exe "http://127.0.0.1:<PORT>/api/dev/place?def=TestPartsAreas"`) and hold the pointer over open ground: under the see-through box lie its blue `front`, `side` and `back` use areas, each named on a dark plate, its clearance hatched with a dashed edge, and its collider in cyan.
58. Press E: the box and its overlays turn together.
59. Move the pointer onto a tree trunk: the box and every overlay turn red, with the reason beside it.
60. Press Esc: placement ends.
61. `package?def=TestPartsAreas&at=<RX>,<RY>&target=<RX+4>,<RY>&turn=30`: the target shows a see-through box with the same overlays, turned 30.
62. `package?def=TestPartsAreas&at=<RX>,<RY>&target=<RX+8>,<RY>&turn=120`: a second target shows its box and overlays turned 120, the first unchanged.
63. Exit the game.
