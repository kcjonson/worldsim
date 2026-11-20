# Scenario 14: player visibility

**Status:** ready  
**Last verified:** 2026-11-20

Colonists reveal the ground: bright in sight, tinted once seen, black never seen, with soft edges, faint triangles behind trunks, and soft-sided shadows behind walls and big rocks. Specs: [player-visibility.md](../../technical/player-visibility.md), [sight-softening.md](../../technical/sight-softening.md).

---

## Setup

Launch as [../README.md](../README.md) says and take Quick Start. It is seeded, so the landing is the same each run: forest west of a river, the drop at (-29, -17) (`/api/state?what=landing`), and a BigRock at (-63.2, -40.9), 42 m north-west of the drop. The river runs through the origin, east of the crew, and blocks them; every place below is on the crew's side. The verbs used are `/api/dev/` `select`, `walls`, `opening`, `colonist`, `kill`, `teleport`, `reveal`, `time`, and `visibility?x=&y=` (seen, visible). To order drafted Bob to a point, jump the camera there and right-click the window's centre. Once he is drafted, never left-click empty ground with no tool active: that deselects him, which releases him. +y is south.

---

## Steps

1. Zoom 0.5 on Bob: a bright disc round him, a tinted disc about 300 m across, black beyond.
2. Zoom 3 on Bob: 100 m east reads seen, not visible; 300 m east reads neither.
3. Click the crown of a mature tree 80 m east at zoom 6: it is selected, one outline, oak or maple.
4. Jump 250 m out (all black) and click where a tree stands: nothing is selected.
5. Select Bob and click Draft once: his panel says Controlled.
6. Zoom 3 on him: his bright area's edge is a soft fade, no hard line.
7. Jump to (-33, 129), 146 m south of the drop, at zoom 1: the tint fades into the black, and the black is solid.
8. Press V on Bob: the green rim sits closer to him where trees are dense than where they thin. Press V again.
9. Zoom 12 behind a trunk near him: a faint short triangle behind it, longer behind a thick oak, none reaching the rim.
10. Order him to (-63.2, -48.9), 8 m north of the BigRock: a shadow runs from the rock to his rim, soft-sided.
11. Order him to (-73.6, -45.7), then to (-48.6, -45.7): the rock's shadow turns, its soft side sliding with no jump.
12. Watch behind him on that walk: the tinted area never shrinks.
13. Spawn a colonist at (-49.1, -44.2), among the trees beside Bob: no trunk's triangle darkens ground the other colonist sees.
14. Remove her: her bright area goes, and his stays.
15. Build a 0.5 m wall from (-43.7, -45.95) to (-43.7, -45.45): a barely visible triangle behind it, about a metre long.
16. Build a 2 m wall from (-43.7, -43.7) to (-43.7, -41.7): a full shadow with soft sides.
17. Build a closed wall loop (-48.6, -50.2), (-44.6, -50.2), (-44.6, -46.2), (-48.6, -46.2): a 4 m room.
18. Add a door at (-46.6, -46.2) and a window at (-46.6, -50.2): a doorway in the south wall, a window in the north.
19. Order Bob to (-46.6, -48.2): light leaves through the door and the window only, each beam soft-sided.
20. Order him to (-46.6, -46.9): the beam through the door keeps a bright core.
21. Set the game speed to 0 (`time?speed=0`): no one moves, Bob stays at (-46.6, -46.9).
22. Spawn a colonist at (-56.6, -48.2), 8 m west of the room: along its west wall no dark strip, only the shadows off its two west corners.
23. Remove her.
24. Set the game speed back to 1 (`time?speed=1`): Bob's panel still says Controlled.
25. Order Bob to (-50.6, -43.2), out the door and past the room's south-west corner: the room's shadow swings round the corner with no jump.
26. Order him to (-343, -44) and twice more west into the black: the tint's edge sits just outside his bright rim, no dark band between.
27. Set 10x, order him west into the black twice more, set 1x: the tint's edge sits within a few metres of his rim.
28. Teleport him to (-49, -10), press B, click Crafting Spot, and hover tinted ground at zoom 6: the ghost reads valid.
29. Click: a crafting spot is placed, and the tool closes.
30. Jump 250 m north into the black, press B, click Crafting Spot again, and hover: the ghost is faint dark red; a click places nothing. Press Escape.
31. Right-click the black 100 m north of the edge: a marker at the edge, and Bob walks there and stops.
32. As he arrives: a bright disc opens past the old edge.
33. Right-click the black again: he walks on to the new edge.
34. Turn reveal on: no black, no tint.
35. Right-click 15 m past his seen ground: he walks to the click.
36. Turn reveal off: black and tint return, the ground he opened still tinted.
37. Jump the camera east 300 m at a time to 2.4 km: the Chunks readout never rises, and falls.
38. Teleport Bob to (479, 10) and wait a second: (494, 10) reads seen and visible, (511, 10) neither.
39. Teleport him to (-49, -10) and view (508, 10) at zoom 6: trees past 512 m drawn over the seen edge.
40. Jump 4 km east and back: the same picture.
41. Build a wall from (-43, -13) to (-43, -7), 6 m east of Bob, and zoom 20 on it: the tint starts at its west face.
42. Look at its top: an even mid brown.
43. Turn reveal on: the top turns light tan. Turn reveal off.
44. Click Release, then exit.
