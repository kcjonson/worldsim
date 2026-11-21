# Scenario 14: player visibility

**Status:** draft  
**Last verified:** 2026-11-19

Colonists reveal the ground: bright in sight, tinted once seen, black never seen, with soft edges, faint triangles behind trunks, and soft-sided shadows behind walls and tall bodies; colonists see over anything lower than their eyes, a BigRock included. Specs: [player-visibility.md](../../technical/player-visibility.md), [sight-softening.md](../../technical/sight-softening.md).

---

## Setup

Launch as [../README.md](../README.md) says and take Quick Start. It is seeded, so the landing is the same each run: forest by a river. The sight steps spawn a `BigRock` (1.5 m tall, so too low to block sight) and a `TestTallBox` (2 m tall, 1 m square, the tall opaque body). The verbs used are `/api/dev/` `spawn`, `ground`, `select`, `walls`, `opening`, `colonist`, `kill`, `teleport`, `reveal`, `time`, and `visibility?x=&y=` (seen, visible). To order drafted Bob to a point, jump the camera there and right-click the window's centre. +y is south.

---

## Steps

1. Zoom 0.5 on Bob: a bright disc round him, a tinted disc about 300 m across, black beyond.
2. Zoom 3 on Bob: 100 m east reads seen, not visible; 300 m east reads neither.
3. Click the crown of a mature tree 80 m east at zoom 6: it is selected, one outline, oak or maple.
4. Jump 250 m out (all black) and click where a tree stands: nothing is selected.
5. Select Bob and click Draft once: his panel says Controlled.
6. Zoom 3 on him: his bright area's edge is a soft fade, no hard line.
7. Jump to (0, 150) at zoom 1: the tint fades into the black, and the black is solid.
8. Press V on Bob: the green rim sits closer to him where trees are dense than where they thin. Press V again.
9. Zoom 12 behind a trunk near him: a faint short triangle behind it, longer behind a thick oak, none reaching the rim.
10. Order him to (0.4, 40.3): he walks there.
11. Spawn a `BigRock` at (0.4, 44.3), 4 m south of him: no shadow behind it, the ground past it as bright as round him.
12. Read `visibility?x=0.4&y=50`, behind the rock: `visible` is true.
13. Read `sightBodies`: none of Bob's bodies is the BigRock, and every trunk is `soft`.
14. Order him to (0.4, 47), past the rock: he walks round it.
15. Order him back to (0.4, 40.3): he walks round it again.
16. Spawn a `TestTallBox` at (-3.6, 40.3), 4 m west of him: the answer's `entities` holds one id; note it.
17. Read `sightBodies`: the box is `opaque`, about 1555 mm across, and a full shadow runs from it to his rim, soft-sided.
18. Read `visibility?x=-7&y=40.8`, behind the box: `visible` is false.
19. Read `ground` at (-3.6, 40.8), the footprint's middle: `valid` is false.
20. Order him to (-2, 35), then to (6, 35): the box's shadow turns, its soft side sliding with no jump.
21. Watch behind him on that walk: the tinted area never shrinks.
22. Order him to (-7, 42.8), past the box on a straight line through it: he walks round its north face.
23. `kill` the box's id: its shadow and its body are gone.
24. Read `ground` at (-3.6, 40.8): `valid` is true.
25. Order him to (10, 43.5): he walks there.
26. Spawn a colonist at (9.5, 45), among the trees beside Bob: no trunk's triangle darkens ground the other colonist sees.
27. Remove her: her bright area goes, and his stays.
28. Build a 0.5 m wall from (14.9, 43.25) to (14.9, 43.75): a barely visible triangle behind it, about a metre long.
29. Build a 2 m wall from (14.9, 45.5) to (14.9, 47.5): a full shadow with soft sides.
30. Build a closed wall loop (10, 39), (14, 39), (14, 43), (10, 43): a 4 m room.
31. Add a door at (12, 43) and a window at (12, 39): a doorway in the south wall, a window in the north.
32. Order Bob to (12, 41): light leaves through the door and the window only, each beam soft-sided.
33. Order him to (12, 42.3): the beam through the door keeps a bright core.
34. Set the game speed to 0 (`time?speed=0`): no one moves, Bob stays at (12, 42.3).
35. Spawn a colonist at (2, 41), 8 m west of the room: along its west wall no dark strip, only the shadows off its two west corners.
36. Remove her.
37. Set the game speed back to 1 (`time?speed=1`): Bob's panel still says Controlled.
38. Order Bob to (8, 46), out the door and past the room's south-west corner: the room's shadow swings round the corner with no jump.
39. Order him to (300, 44) and twice more into the black: the tint's edge sits just outside his bright rim, no dark band between.
40. Set 10x, order him into the black twice more, set 1x: the tint's edge sits within a few metres of his rim.
41. Teleport him to (24, 11), press B, click Crafting Spot, and hover tinted ground at zoom 6: the ghost reads valid.
42. Click: a crafting spot is placed.
43. Hover the black 250 m out: the ghost is faint dark red; a click places nothing. Press Escape.
44. Right-click the black 100 m past the edge: a marker at the edge, and Bob walks there and stops.
45. As he arrives: a bright disc opens past the old edge.
46. Right-click the black again: he walks on to the new edge.
47. Turn reveal on: no black, no tint.
48. Right-click 15 m past his seen ground: he walks to the click.
49. Turn reveal off: black and tint return, the ground he opened still tinted.
50. Jump the camera east 300 m at a time to 2.4 km: the Chunks readout never rises, and falls.
51. Teleport Bob to (479, 10) and wait a second: (494, 10) reads seen and visible, (511, 10) neither.
52. Teleport him to (24, 11) and view (508, 10) at zoom 6: trees past 512 m drawn over the seen edge.
53. Jump 4 km east and back: the same picture.
54. Build a wall from (30, 8) to (30, 14), 6 m east of Bob, and zoom 20 on it: the tint starts at its west face.
55. Look at its top: an even mid brown.
56. Turn reveal on: the top turns light tan. Turn reveal off.
57. Click Release, then exit.
