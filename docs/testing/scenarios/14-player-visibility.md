# Scenario 14: player visibility

**Status:** draft  
**Last verified:** 2026-11-20, all 57 steps at these places west of the river. Every step passed except two that could not be judged by eye: step 9, where no trunk triangle was visible in a zoom 12 screenshot (the `cover` view drew them, 0.24 m behind a 520 mm maple and 2.55 m behind a 1362 mm oak), and step 28, where no triangle showed behind the 0.5 m wall at zoom 12 or 24 (`sightBodies` lists it as a `soft` wall body 538 mm across). Step 40's 10x was the top speed button.

Colonists reveal the ground: bright in sight, tinted once seen, black never seen, with soft edges, faint triangles behind trunks, and soft-sided shadows behind walls and tall bodies; colonists see over anything lower than their eyes, a BigRock included. Specs: [player-visibility.md](../../technical/player-visibility.md), [sight-softening.md](../../technical/sight-softening.md).

---

## Setup

Launch as [../README.md](../README.md) says and take Quick Start. It is seeded, so the landing is the same each run: forest west of a river, the drop at (-29, -17) (`/api/state?what=landing`). The river runs through the origin, east of the crew, and blocks them; every place Bob walks to below is on the crew's side. The sight steps spawn a `BigRock` (1.5 m tall, so too low to block sight) and a `TestTallBox` (2 m tall, 1 m square, the tall opaque body). The verbs used are `/api/dev/` `spawn`, `ground`, `select`, `walls`, `opening`, `colonist`, `kill`, `teleport`, `reveal`, `time`, and `visibility?x=&y=` (seen, visible). To order drafted Bob to a point, jump the camera there and right-click the window's centre. Once he is drafted, never left-click empty ground with no tool active: that deselects him, which releases him. +y is south.

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
10. Order him to (-44.6, -52.9): he walks there.
11. Spawn a `BigRock` at (-44.6, -48.9), 4 m south of him: no shadow behind it, the ground past it as bright as round him.
12. Read `visibility?x=-44.6&y=-43.2`, behind the rock: `visible` is true.
13. Read `sightBodies`: none of Bob's bodies is the BigRock, and every trunk is `soft`.
14. Order him to (-44.6, -46.2), past the rock: he walks round it.
15. Order him back to (-44.6, -52.9): he walks round it again.
16. Spawn a `TestTallBox` at (-48.6, -52.9), 4 m west of him: the answer's `entities` holds one id; note it.
17. Read `sightBodies`: the box is `opaque`, about 1555 mm across, and a full shadow runs from it to his rim, soft-sided.
18. Read `visibility?x=-52&y=-52.4`, behind the box: `visible` is false.
19. Read `ground` at (-48.6, -52.4), the footprint's middle: `valid` is false.
20. Order him to (-47, -58.2), then to (-39, -58.2): the box's shadow turns, its soft side sliding with no jump.
21. Watch behind him on that walk: the tinted area never shrinks.
22. Order him to (-52, -50.4), past the box on a straight line through it: he walks round its north face.
23. `kill` the box's id: its shadow and its body are gone.
24. Read `ground` at (-48.6, -52.4): `valid` is true.
25. Order him to (-35, -49.7): he walks there.
26. Spawn a colonist at (-35.5, -48.2), among the trees beside Bob: no trunk's triangle darkens ground the other colonist sees.
27. Remove her: her bright area goes, and his stays.
28. Build a 0.5 m wall from (-30.1, -49.95) to (-30.1, -49.45): a barely visible triangle behind it, about a metre long.
29. Build a 2 m wall from (-30.1, -47.7) to (-30.1, -45.7): a full shadow with soft sides.
30. Build a closed wall loop (-35, -54.2), (-31, -54.2), (-31, -50.2), (-35, -50.2): a 4 m room.
31. Add a door at (-33, -50.2) and a window at (-33, -54.2): a doorway in the south wall, a window in the north.
32. Order Bob to (-33, -52.2): light leaves through the door and the window only, each beam soft-sided.
33. Order him to (-33, -50.9): the beam through the door keeps a bright core.
34. Set the game speed to 0 (`time?speed=0`): no one moves, Bob stays at (-33, -50.9).
35. Spawn a colonist at (-43, -52.2), 8 m west of the room: along its west wall no dark strip, only the shadows off its two west corners.
36. Remove her.
37. Set the game speed back to 1 (`time?speed=1`): Bob's panel still says Controlled.
38. Order Bob to (-37, -47.2), out the door and past the room's south-west corner: the room's shadow swings round the corner with no jump.
39. Order him to (-330, -47) and twice more west into the black: the tint's edge sits just outside his bright rim, no dark band between.
40. Set 10x, order him west into the black twice more, set 1x: the tint's edge sits within a few metres of his rim.
41. Teleport him to (-35, -17), press B, click Crafting Spot, and hover tinted ground at zoom 6: the ghost reads valid.
42. Click: a crafting spot is placed, and the tool closes.
43. Jump 250 m north into the black, press B, click Crafting Spot again, and hover: the ghost is faint dark red; a click places nothing. Press Escape.
44. Right-click the black 100 m north of the edge: a marker at the edge, and Bob walks there and stops.
45. As he arrives: a bright disc opens past the old edge.
46. Right-click the black again: he walks on to the new edge.
47. Turn reveal on: no black, no tint.
48. Right-click 15 m past his seen ground: he walks to the click.
49. Turn reveal off: black and tint return, the ground he opened still tinted.
50. Jump the camera east 300 m at a time to 2.4 km: the Chunks readout never rises, and falls.
51. Teleport Bob to (479, 10) and wait a second: (494, 10) reads seen and visible, (511, 10) neither.
52. Teleport him to (-35, -17) and view (508, 10) at zoom 6: trees past 512 m drawn over the seen edge.
53. Jump 4 km east and back: the same picture.
54. Build a wall from (-29, -20) to (-29, -14), 6 m east of Bob, and zoom 20 on it: the tint starts at its west face.
55. Look at its top: an even mid brown.
56. Turn reveal on: the top turns light tan. Turn reveal off.
57. Click Release, then exit.
