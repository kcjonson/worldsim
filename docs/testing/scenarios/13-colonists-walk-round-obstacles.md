# Scenario 13: colonists walk round obstacles

**Status:** ready  
**Last verified:** 2026-11-12

A colonist walks into fresh dense ground and goes round the obstacles, and panning the camera away
changes nothing. The nav mesh ends at what colonists have seen and grows as they walk. Spec: [nav-mesh-variable-resolution.md](../../technical/nav-mesh-variable-resolution.md)
(4.4 to 4.7).

---

## Steps

Build `world-sim` (RelWithDebInfo), launch it with no flags, click Enter Expedition, then Quick Start. The dev server is on 127.0.0.1:8081. A camera jump is `curl.exe "http://127.0.0.1:8081/api/control?action=camera&x=<x>&y=<y>&zoom=<z>"`. Quick Start lands in a forest by a river.

1. Read the colonists (`curl.exe "http://127.0.0.1:8081/api/state?what=colonists"`: Bob's `id`, `x` and `y` as BID, BX, BY), select him (`curl.exe "http://127.0.0.1:8081/api/dev/select?colonist=<BID>"`), click Draft in his panel, and `...camera&x=<BX>&y=<BY>&zoom=3`: his task reads "Controlled".
2. Right-click a spot in the forest about 40 m from Bob, away from the river and the other colonists: a marker shows, he walks there going round every trunk on the way, never through one, and never stops dead or steps back as trees come in ahead of him.
3. Repeat step 2 five times, each from where he stands and in the same direction, about 200 m past where anyone has been: the same each time; screenshots along the way show no trunk he walked through.
4. Right-click a spot about 40 m ahead again, then at once `...camera&x=<BX+2000>&y=<BY>&zoom=3` and poll `what=colonists` every few seconds for 30 s: Bob's `x` and `y` keep moving toward the spot and stop on it.
5. `...camera&x=<Bob's x>&y=<Bob's y>&zoom=3`: he stands at the spot with no trunk under him, as he would had the camera stayed.
6. Move the camera 300 m past where Bob has been and right-click there: no marker, and Bob does not move (no colonist has been near that ground; until player visibility exists that is expected).
7. Press N, then `...camera&x=<BX>&y=<BY>&zoom=0.5`: the nav overlay's mesh round the landing is a disc about 150 m in radius (plus the band Bob walked), and nothing is drawn past its edge.
8. On the side of the disc away from Bob's walk, right-click about 40 m outside its edge: no marker, and Bob does not move.
9. Right-click just inside the disc's edge on that side, then about 40 m farther out each time the mesh reaches it, three times: the mesh grows along his way ahead of him, and he never stands at its edge for more than a moment.
10. Walk Bob in a loop of about 100 m across that starts and ends on the walked band, then across the middle of the loop: the loop leaves an unmeshed hole, and walking across it fills it.
11. Press N to hide the overlay, click Release in Bob's panel, then `curl.exe "http://127.0.0.1:8081/api/control?action=exit"`.
