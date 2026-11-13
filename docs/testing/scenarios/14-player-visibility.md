# Scenario 14: player visibility

**Status:** draft  
**Last verified:** 2026-11-13, all steps but 6 and 7 (placement needs the real mouse; injected pointer moves don't move the ghost)

The player sees only what colonists have seen: bright ground round the crew, tinted ground out to the 150 m landing, black beyond. Things on seen ground select and place; on black ground they do not; a move order into the black walks the colonist to the edge of seen ground. Spec: [player-visibility.md](../../technical/player-visibility.md).

---

## Steps

Build `world-sim` (RelWithDebInfo), launch it with no flags, click Enter Expedition, then Quick Start. The dev server is on 127.0.0.1:8081. A camera jump is `curl.exe "http://127.0.0.1:8081/api/control?action=camera&x=<x>&y=<y>&zoom=<z>"`. `curl.exe "http://127.0.0.1:8081/api/dev/visibility?x=<x>&y=<y>"` says whether a point is `seen` and `visible`. Bob wanders, so read him first (`...api/state?what=colonists`: `id`, `x`, `y` as BID, BX, BY). Steps 6 and 7 need a real mouse, since the placement ghost follows the real pointer only.

1. `...camera&x=<BX>&y=<BY>&zoom=0.5`: a bright disc round Bob, a dimmer tinted disc about 150 m across the landing (river and trees visible in it), black everywhere else.
2. Zoom 3 on Bob: bright ground in his sight, tinted ground past it; the black is far off. `...visibility?x=<BX+100>&y=<BY>` answers `seen` true, `visible` false; the same at 300 m out answers both false.
3. Find a tree 80 m out (`curl.exe "http://127.0.0.1:8081/api/dev/flora?age=mature&near=<BX+80>,<BY>"`), jump to it at zoom 6 and click its crown: it is selected, one outline, `Flora_TreeOak` or `Flora_TreeMaple` in the panel.
4. Jump 250 m out at zoom 6 (all black) and click where a tree would be: nothing is selected and the panel closes.
5. Select Bob (`curl.exe "http://127.0.0.1:8081/api/dev/select?colonist=<BID>"`), click Draft in his panel, jump to zoom 1 on a point 100 m south of him: the black edge is in view.
6. By hand: Build > Crafting Spot, move the mouse over tinted ground: the ghost follows and reads valid. Click: a crafting spot is placed.
7. By hand: Build > Crafting Spot, move the mouse over black ground: the ghost reads invalid. Click: nothing is placed.
8. Right-click on the black, well past the edge (zoom 1, about 100 m beyond it): a marker shows at the edge of seen ground, not at the click, and Bob walks there and stops.
9. As he arrives, a new bright disc opens round him past the old edge, and `...api/state?what=visibility` shows `seenSquareMeters` larger than before. Right-click into the black again: he walks on to the new edge.
10. `curl.exe "http://127.0.0.1:8081/api/dev/reveal?on=1"`, zoom 0.5 on the landing: every tile is drawn bright, no black, no tint; the right-click of step 8 now walks to where you click.
11. `...reveal?on=0`: the black and the tint come back, with the ground Bob opened in step 9 still seen.
12. Click Release in Bob's panel, then `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`.
