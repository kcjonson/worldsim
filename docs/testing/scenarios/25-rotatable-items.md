# Scenario 25: rotatable items

**Status:** draft  
**Last verified:** 2026-11-21, steps 9 to 18; steps 1 to 8 on 2026-11-20

A parts item draws at any turn with the faces the turn shows, sorts against a colonist and another item along its whole length, doesn't pop at the edge of the screen, draws its detail levels as the camera zooms, and selects by its turned shape. Spec: [rotatable-placed-items.md](../../technical/rendering/rotatable-placed-items.md), sections 3, 6.8, 8, 9, 10, and 15.

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev server on 127.0.0.1:8081, or the port given with `--http-port`; `<PORT>` below is that port. Then `curl.exe "http://127.0.0.1:<PORT>/api/dev/reveal?on=1"`.

The test item is `TestPartsBox`: a box 1.2 m wide (local x), 0.6 m deep and 0.8 m tall, front red with an F, back blue with a B, left end green with an L, right end orange with an R, and an arrow on its top pointing to the front. It has three detail levels: at 8 px/m the letters drop, and from 80 px/m a white dot shows in each corner of every side. A spawn is `curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=TestPartsBox&at=<x>,<y>&n=1&scatter=0&turn=<degrees>"`: `at` is the centre of the box's 1.2 by 0.6 m footprint, and `turn` is degrees clockwise with the front down the screen at 0. A spawn that refuses its spot answers HTTP 400 naming water or an obstacle: move the spot a metre and try again. Steps 3 to 6 put Bob and a second box at offsets from that centre.

Read Bob's `id`, `x` and `y` from `/api/state?what=colonists` (BID, BX, BY) and pick a row of open ground near him; call its west end RX,RY.

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

17. Click 60 pixels below the middle of the window, on Bob's legs: Bob is selected and outlined as before.
18. `curl.exe -i "http://127.0.0.1:<PORT>/api/dev/spawn?def=TestPartsBox&at=<RX>,<RY>&turn="`: HTTP 400 saying `turn=` is empty.
19. `curl.exe "http://127.0.0.1:<PORT>/api/control?action=exit"`.
