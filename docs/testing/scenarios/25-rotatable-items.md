# Scenario 25: rotatable items

**Status:** draft  
**Last verified:** 2026-11-20, steps 1 to 8

A parts item draws at any turn with the faces the turn shows, sorts against a colonist and another item along its whole length, and doesn't pop at the edge of the screen. Spec: [rotatable-placed-items.md](../../technical/rendering/rotatable-placed-items.md), sections 3, 8, 9, and 10. The test item is `TestPartsBox`, a 1.2 m by 0.6 m box, 0.8 m tall: front red with an F, back blue with a B, left green with an L, right orange with an R, and an arrow on its top pointing to the front.

---

## Steps

Build `world-sim` (RelWithDebInfo), launch it with `--background --no-settings` and no other flags, click Enter Expedition, then Quick Start, then `curl.exe "http://127.0.0.1:8081/api/dev/reveal?on=1"`. The dev server is on 127.0.0.1:8081. Read Bob's `id`, `x` and `y` from `curl.exe "http://127.0.0.1:8081/api/state?what=colonists"` (BID, BX, BY) and pick a row of open ground near him, call its west end RX,RY. A spawn is `curl.exe "http://127.0.0.1:8081/api/dev/spawn?def=TestPartsBox&at=<x>,<y>&n=1&scatter=0&turn=<degrees>"` (turn is clockwise, the front down the screen at 0); a 400 naming water or an obstacle means move the spot a metre and try again.

1. Spawn six boxes along the row, 3 m apart, at turns 0, 35, 90, 180, 271 and 325 (`at=<RX>,<RY>`, `<RX+3>,<RY>` and so on), then `curl.exe "http://127.0.0.1:8081/api/control?action=camera&x=<RX+7.5>&y=<RY>&zoom=14"`: at 0 the red front and the arrow pointing down; at 35 red front and orange right end, arrow down and to the left; at 90 only the orange right end, arrow left; at 180 only the blue back, arrow up; at 271 the green left end and a red sliver; at 325 green left end and red front, arrow down and to the right.
2. `curl.exe "http://127.0.0.1:8081/api/state?what=uprights"` with Bob out of view: `itemDrawCalls` is 6, plus at most two for each tree that stands among a box's strips and overlaps it.
3. Kill all but one box and spawn one at turn 45 on bare ground, at BX2,BY2. Its high end is the west end. Teleport Bob just in front of it (`curl.exe "http://127.0.0.1:8081/api/dev/teleport?colonist=<BID>&to=<BX2-0.42>,<BY2+0.4>"`) and pause at once (`...control?action=pause`): Bob draws over the box's red front.
4. Resume, teleport Bob behind the high end (`to=<BX2-0.42>,<BY2-0.85>`), pause: the box draws over his legs.
5. The same at the low end: in front (`to=<BX2+0.42>,<BY2+0.85>`) Bob draws over the orange end; behind (`to=<BX2+0.42>,<BY2-0.4>`) the box draws over him.
6. Move Bob away. Spawn a box at turn 0 at `<BX2-0.5>,<BY2+1.05>`: it draws over the 45 degree box's front. Kill it and spawn one at `<BX2-0.7>,<BY2-0.95>`: the 45 degree box draws over its front.
7. With Bob walking among the six boxes of step 1, read `what=uprights` a few times: `itemDrawCalls` rises by at most the number of boxes he sorts among, never more.
8. Move the camera so a turned box stands half across the left, right, and bottom edges of the screen in turn: the part on screen draws every time; nothing pops in as the camera moves.
9. `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`.
