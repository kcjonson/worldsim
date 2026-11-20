# Scenario 14: player visibility

**Status:** draft  
**Last verified:** 2026-11-19

Colonists reveal the ground: bright in sight, tinted once seen, black never seen, with soft edges, faint triangles behind trunks, and soft-sided shadows behind walls and big rocks. Specs: [player-visibility.md](../../technical/player-visibility.md), [sight-softening.md](../../technical/sight-softening.md).

---

## Setup

Launch as [../README.md](../README.md) says and take Quick Start. It is seeded, so the landing is the same each run: forest by a river, and a BigRock at (0.4, 48.3), 48 m south of the drop. The verbs used are `/api/dev/` `select`, `walls`, `opening`, `colonist`, `kill`, `teleport`, `reveal`, `time`, and `visibility?x=&y=` (seen, visible). To order drafted Bob to a point, jump the camera there and right-click the window's centre. +y is south.

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
10. Order him to (0.4, 40.3), 8 m north of the BigRock: a shadow runs from the rock to his rim, soft-sided.
11. Order him to (-10, 43.5), then to (10, 43.5): the rock's shadow turns, its soft side sliding with no jump.
12. Watch behind him on that walk: the tinted area never shrinks.
13. Build a 0.5 m wall from (14.9, 43.25) to (14.9, 43.75): a barely visible triangle behind it, about a metre long.
14. Build a 2 m wall from (14.9, 45.5) to (14.9, 47.5): a full shadow with soft sides.
15. Build a closed wall loop (10, 39), (14, 39), (14, 43), (10, 43): a 4 m room.
16. Add a door at (12, 43) and a window at (12, 39).
17. Order Bob to (12, 41): light leaves through the door and the window only, each beam soft-sided.
18. Order him to (12, 42.3): the beam through the door keeps a bright core.
19. Spawn a colonist at (8, 41), west of the room: the ground west of the room, hidden from Bob, is bright, not dark.
20. Remove her: that ground turns tinted.
21. Order Bob to (300, 44) and twice more into the black: the tint's edge sits just outside his bright rim, no dark band between.
22. Set 10x, order him into the black twice more, set 1x: the same.
23. Teleport him to (24, 11).
24. Press B, click Crafting Spot, and hover tinted ground at zoom 6: the ghost reads valid.
25. Click: a crafting spot is placed.
26. Hover the black 250 m out: the ghost is faint dark red; a click places nothing. Press Escape.
27. Right-click the black 100 m past the edge: a marker at the edge, and Bob walks there and stops.
28. As he arrives: a bright disc opens past the old edge.
29. Right-click the black again: he walks on to the new edge.
30. Turn reveal on: no black, no tint.
31. Right-click 15 m past his seen ground: he walks to the click.
32. Turn reveal off: black and tint return, the ground he opened still tinted.
33. Jump the camera east 300 m at a time to 2.4 km: the Chunks readout never rises, and falls.
34. Teleport Bob to (479, 10): (494, 10) reads seen and visible, (511, 10) neither.
35. Teleport him to (24, 11), view (508, 10) at zoom 6, jump 4 km east and back: the same picture.
36. Build a wall from (30, 8) to (30, 14), 6 m east of Bob, and zoom 20 on it: the tint starts at its west face.
37. Its top is an even mid brown; with reveal on, light tan. Turn reveal off.
38. Click Release, then exit.
