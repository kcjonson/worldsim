# Scenario 14: player visibility

**Status:** draft  
**Last verified:** 2026-11-14, steps 1, 10, 11 (the drawing, not the walk), 12 and 13; 2026-11-13, all steps (placement through injected input since salal v0.41.1)  

The player sees only what colonists have seen: bright ground round the crew, tinted ground out to the 150 m landing, black beyond. Things on seen ground select and place; on black ground they do not; a move order into the black walks the colonist to the edge of seen ground. Spec: [player-visibility.md](../../technical/player-visibility.md).

---

## Steps

Build `world-sim` (RelWithDebInfo), launch it with no flags, click Enter Expedition, then Quick Start. The dev server is on 127.0.0.1:8081. A camera jump is `curl.exe "http://127.0.0.1:8081/api/control?action=camera&x=<x>&y=<y>&zoom=<z>"`. `curl.exe "http://127.0.0.1:8081/api/dev/visibility?x=<x>&y=<y>"` says whether a point is `seen` and `visible`. Bob wanders, so read him first (`...api/state?what=colonists`: `id`, `x`, `y` as BID, BX, BY). Input coordinates are the window's logical pixels, which on Windows are screenshot pixels; a position from `/api/ui/tree` is in ui px, times `layoutScale` from `/api/state?what=uiscale`.

1. `...camera&x=<BX>&y=<BY>&zoom=0.5`: a bright disc round Bob, a dimmer tinted disc about 150 m across the landing (river and trees visible in it), black everywhere else.
2. Zoom 3 on Bob: bright ground in his sight, tinted ground past it; the black is far off. `...visibility?x=<BX+100>&y=<BY>` answers `seen` true, `visible` false; the same at 300 m out answers both false.
3. Find a tree 80 m out (`curl.exe "http://127.0.0.1:8081/api/dev/flora?age=mature&near=<BX+80>,<BY>"`), jump to it at zoom 6 and click its crown: it is selected, one outline, `Flora_TreeOak` or `Flora_TreeMaple` in the panel.
4. Jump 250 m out at zoom 6 (all black) and click where a tree would be: nothing is selected and the panel closes.
5. Select Bob (`curl.exe "http://127.0.0.1:8081/api/dev/select?colonist=<BID>"`), click Draft in his panel, jump to zoom 1 on a point 100 m south of him: the black edge is in view.
6. Press B (separate `keydown,b` and `keyup,b` requests), click Crafting Spot in the Build menu, then `/api/input?ev=move,x,y` over tinted ground (`...visibility` says `seen` true, `visible` false) and take a separate screenshot: the ghost sits under the pointer and reads valid. `ev=click,x,y`: a crafting spot is placed (`what=stations` lists it).
7. Jump the camera 250 m out (all black), start Build > Crafting Spot again and `ev=move,x,y`: the ghost follows and reads invalid (a faint dark red). `ev=click,x,y`: nothing is placed (`what=stations` unchanged).
8. Right-click on the black, well past the edge (zoom 1, about 100 m beyond it): a marker shows at the edge of seen ground, not at the click, and Bob walks there and stops.
9. As he arrives, a new bright disc opens round him past the old edge, and `...api/state?what=visibility` shows `seenSquareMeters` larger than before. Right-click into the black again: he walks on to the new edge.
10. `curl.exe "http://127.0.0.1:8081/api/dev/reveal?on=1"`, zoom 0.5 on the landing: every tile is drawn bright, no black, no tint; a right-click up to about 40 m past Bob's seen ground now walks to where you click (farther than the nav mesh reaches it does nothing, as it did before player visibility).
11. `...reveal?on=0`: the black and the tint come back, with the ground Bob opened in step 9 still seen.
12. Zoom 1 at the landing, then jump the camera east 300 m at a time to 2.4 km out, reading `...api/state?what=scene` (`chunks`, the `Chunks:` readout) at each: it never rises, and falls as the chunks the camera alone held unload.
13. `...teleport?colonist=<BID>&to=479,10` (`/api/dev/teleport`), wait a second: `...visibility?x=509&y=10` is `seen`, `x=511` is not, a seen edge a few metres short of the chunk border at 512 m. Teleport him back to the landing, screenshot at `x=508&y=10&zoom=6`, jump to `x=4000` (chunks fall), and back: the same picture, the trees from past the border still drawn over the seen edge, and `...flora?age=mature&near=520,10` still lists trees east of 512 m.
14. Click Release in Bob's panel, then `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`.
