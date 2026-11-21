# Scenario 25: rotatable items

**Status:** draft  
**Last verified:** 2026-11-20, steps 1 to 11, 13 to 19, and 22 and 23 (12, 20 and 21 need a held Alt, which injected input can't send); 2026-11-21, steps 24 to 46

A parts item draws at any turn with the faces the turn shows, sorts against a colonist and another item along its whole length, doesn't pop at the edge of the screen, and is turned while placing it with Q/E or a drag, snapped square to a wall and flush against it. Placed, it is an obstacle colonists walk round; a ghost over another item or its clearance is refused with the reason, and a wall can't be drawn through one. Spec: [rotatable-placed-items.md](../../technical/rendering/rotatable-placed-items.md), sections 3, 8, 9, 10, 11, 13, and 14.

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev server on 127.0.0.1:8081, or the port given with `--http-port`; `<PORT>` below is that port. Then `curl.exe "http://127.0.0.1:<PORT>/api/dev/reveal?on=1"`.

The test item is `TestPartsBox`: a box 1.2 m wide (local x), 0.6 m deep and 0.8 m tall, front red with an F, back blue with a B, left end green with an L, right end orange with an R, and an arrow on its top pointing to the front. A spawn is `curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=TestPartsBox&at=<x>,<y>&n=1&scatter=0&turn=<degrees>"`: `at` is the centre of the box's 1.2 by 0.6 m footprint, and `turn` is degrees clockwise with the front down the screen at 0. A spawn that refuses its spot answers HTTP 400 naming water or an obstacle: move the spot a metre and try again. Steps 3 to 6 put Bob and a second box at offsets from that centre.

Read Bob's `id`, `x` and `y` from `/api/state?what=colonists` (BID, BX, BY) and pick a row of open ground near him; call its west end RX,RY.

Obstacles and refusals (steps 24 on), at the Quick Start landing on open ground clear of trees: kill every box; spawn a `TestPartsBox` at `30,27` turned 30 and a `TestClearanceBench` (a bench 1.6 m by 0.5 m keeping a strip 0.6 m deep clear along its front, +y at turn 0) at `30,31` turned 0; `package?def=TestPartsBox&at=24,31&target=37,31.5` (a crate waiting to be installed at 37,31.5); `walls?pts=38.5,25;38.5,29&host=0&built=1`; `foundation?pts=26,30;40,30;40,33;26,33&material=Wood&built=1`. Select Bob, click Draft in his panel, and `teleport?colonist=<BID>&to=27,27`. To order him to a point, jump the camera there at zoom 6 and right-click the window's centre. After step 25, `time?speed=0`, so no colonist installs the crate.

Placing (steps 9 on): `curl.exe "http://127.0.0.1:<PORT>/api/dev/place?def=TestPartsBox"` starts placing it; pointer moves, keys, presses and releases go in through `/api/input` (`ev=move,<x>,<y>`, `ev=keydown,e`, one key a request, `ev=down,<x>,<y>`, `ev=up,<x>,<y>`, `ev=click,<x>,<y>,right`). `/api/state?what=placement` reads the ghost's `turnDegrees` and position and lists placed parts items with their turns. The wall of steps 16 to 21 is `/api/dev/walls?pts=<WX>,<WY>;<WX+5.64>,<WY+2.05>&host=0&built=1`, 6 m at 20 degrees on open ground. Injected input can't hold Alt: steps 12, 20 and 21 are done on the keyboard. Exit is `curl.exe "http://127.0.0.1:<PORT>/api/control?action=exit"`.

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
9. Kill every box and start placing a `TestPartsBox` over open ground: a see-through box follows the pointer, red front and arrow down, turn 0.
10. Press E twice: the ghost turns clockwise to 30, front and orange right end showing.
11. Press Q: 15.
12. Hold Alt and press E: 16. Let go and press E: 30.
13. Press on the ground and drag 100 px right: the ghost stays at the press and turns its red front to the pointer, 270.
14. Release: the box is placed there at 270.
15. Place again: the ghost starts at 270.
16. With the 20 degree wall built, hold the ghost 1 m south of its middle and press E round: it stops at 20, 110, 200 and 290 among the 15 degree steps.
17. Drag to within 7.5 degrees of one of those four: the turn snaps to it.
18. Turned square to the wall, bring the long side within 0.3 m of the face: it jumps flush against the face.
19. Move the pointer along the wall: the ghost stays flush.
20. Hold Alt and drag: the turn follows the pointer with no snap.
21. Under Alt, turned parallel to the wall: it still slides flush.
22. Press on the ground, drag onto the HUD and release there, then move over the ground: the ghost follows the pointer and doesn't turn.
23. Press on the ground and right-click while holding it, then release the left button: placement ends and nothing is selected.
24. Order Bob to `33,27`, past the box on a straight line through it: he walks round it, never through it.
25. Order him back to `27,27`: he walks round it again.
26. Start placing a `TestPartsBox`: its ghost follows the pointer.
27. Press E until `what=placement` reads `turnDegrees` 0: the ghost's red front faces down.
28. Move the pointer onto the box at `30.3,27`: the ghost reads red with "Overlaps an item" beside it (`what=placement` `valid` false, `reason` "Overlaps an item").
29. Move it onto the crate's target at `37.3,31.5`: red with "Overlaps an item".
30. Move it across the built wall at `38.5,27`: red with "A wall is in the way".
31. Move it in front of the bench at `30,31.6`: red with "Too close".
32. Move it to `30,32.6`, past the bench's clearance: no label, `valid` true.
33. Move it to `30,32.4`: it slides to `30,32.15`, flush against the clearance's edge, and stays valid.
34. Move it to `31.25,31`: it slides to `31.4,31`, flush beside the bench's body, and stays valid.
35. Press Esc: placement ends.
36. Pick Wall in the Build menu: the wall tool is active (`what=drawing` `tool` `wall`).
37. Click `35,31.5` on the foundation: one point placed.
38. Move the pointer to `39,31.5`, through the crate's target: the band reads red with "an item or its clearance is in the way" (`what=drawing` `status.valid` false).
39. Click there: no point is added (`status.pointCount` 1).
40. Press Esc: the point is dropped (`status.pointCount` 0).
41. Click `27,31.6`: one point placed.
42. Move the pointer to `33,31.6`, through the bench's clearance and clear of its body: red with the same reason, and a click there adds no point.
43. Press Esc until the wall tool closes (`tool` `none`).
44. `spawn?def=TestPartsBox&at=30.2,27&n=1&scatter=0`, into the box: a 400 naming "Overlaps an item".
45. `spawn?def=TestPartsBox&at=30,31.6&n=1&scatter=0`, into the bench's clearance: a 400 naming "Too close".
46. Exit the game.