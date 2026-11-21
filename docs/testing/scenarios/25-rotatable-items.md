# Scenario 25: rotatable items

**Status:** draft  
**Last verified:** 2026-11-20, steps 1 to 15 and 17 (11 and 16 need a held Alt, which injected input can't send)

A parts item draws at any turn with the faces the turn shows, sorts against a colonist and another item along its whole length, doesn't pop at the edge of the screen, and is turned while placing it with Q/E or a drag, snapped square to a wall and flush against it. Spec: [rotatable-placed-items.md](../../technical/rendering/rotatable-placed-items.md), sections 3, 8, 9, 10, and 14.

---

## Preconditions

Fresh launch (Quick Start): `world-sim --scene=game --background --no-settings` (RelWithDebInfo), dev server on 127.0.0.1:8081, or the port given with `--http-port`; `<PORT>` below is that port. Then `curl.exe "http://127.0.0.1:<PORT>/api/dev/reveal?on=1"`.

The test item is `TestPartsBox`: a box 1.2 m wide (local x), 0.6 m deep and 0.8 m tall, front red with an F, back blue with a B, left end green with an L, right end orange with an R, and an arrow on its top pointing to the front. A spawn is `curl.exe "http://127.0.0.1:<PORT>/api/dev/spawn?def=TestPartsBox&at=<x>,<y>&n=1&scatter=0&turn=<degrees>"`: `at` is the centre of the box's 1.2 by 0.6 m footprint, and `turn` is degrees clockwise with the front down the screen at 0. A spawn that refuses its spot answers HTTP 400 naming water or an obstacle: move the spot a metre and try again. Steps 3 to 6 put Bob and a second box at offsets from that centre.

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
9. Kill every box, camera on open ground at zoom 14, then `curl.exe "http://127.0.0.1:<PORT>/api/dev/place?def=TestPartsBox"` and move the pointer over the ground (`/api/input?ev=move,<x>,<y>`): a see-through box follows the pointer, red front and arrow down; `/api/state?what=placement` reads `placing`, `turnDegrees` 0.
10. Press E twice (`ev=keydown,e`, one request each): the ghost turns clockwise to 30 degrees, front and orange right end showing. Press Q: 15.
11. Hold Alt (on the keyboard) and press E: 16, a degree. Let go of Alt and press E: 30.
12. Press on the ground and drag about 100 px to the right (`ev=down,<x>,<y>&ev=move,<x+100>,<y>`): the ghost stays where the press was and turns its red front to the pointer, `turnDegrees` 270. Release (`ev=up,<x+100>,<y>`): the box is placed there showing its red front to the right, and `items` in `what=placement` lists it at 270.
13. Place again (`place?def=TestPartsBox`): the ghost starts at 270, the turn last used.
14. Build a wall at 20 degrees: `/api/dev/walls?pts=<WX>,<WY>;<WX+5.64>,<WY+2.05>&host=0&built=1`. Move the ghost to about 1 m south of the wall's middle: `turnDegrees` snaps to 20, 110, 200 or 290 as Q, E or a drag brings it within 7.5 degrees of one, and Q and E stop at those four on their way round.
15. Turned square to the wall, bring the ghost's long side within 0.3 m of the wall face: it jumps flush against the face and stays there while the pointer moves along the wall.
16. Hold Alt by the wall and drag: the turn follows the pointer freely with no snap to the wall or the step; turned parallel to the wall again (`turnDegrees` 20 or 200 under Alt) it still slides flush.
17. Right-click: placement ends, `what=placement` reads `none`.
18. `curl.exe "http://127.0.0.1:<PORT>/api/control?action=exit"`.
