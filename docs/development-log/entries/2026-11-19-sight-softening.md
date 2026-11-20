# 2026-11-19 - Sight softening: soft edges, faint trunk shadows, forest depth

## Summary

Sight no longer has hard edges everywhere. Every obstacle in the collision map has a fixed kind by its own size: walls and big rocks are opaque and cast exact shadows with soft sides, trees and wall stubs are soft and cast only a faint triangle on the cover that closes behind them. The placed things in a direction draw a colonist's sight in that way, so nobody sees deep into a forest. Both edges of the cover fade. Epic WOR-982 ("Sight softening"), built in seven steps, the last of them WOR-990 ("Tests and docs").

## Details

What each step landed:

- Step 0, partial coverage in salal (salal PR 101, worldsim PR 476 for the pin): min and max blends, alpha-only and no-colour writes, the depth states and a depth clear, the `mask` sandbox scene with its value test, and the mask API as merged in salal v0.43.0 (`MixBlendMode`, `MaskPaint::solid`, `MaskPaint::linearGradient`, `MaskImage::svgMask`, `MaskElement`).
- Step 1a, the sim bench (PR 471): `BM_SightForest*` in `libs/sim/vision/SightForest.bench.cpp`, the landing's forest and synthetic stands, a go by counts with five scheduler changes for the record.
- The wall sight occluders (PR 462, WOR-931 "Wall sight occluders are the wall's center line, so the visibility tint starts mid-wall"), which step 2 builds on: a wall occludes by its outline, so the tint starts at its face.
- Step 2, bodies (PR 495): wall bodies and their kinds in `GeometryIndex`, the placed occluder index in `NavigationSystem` (serials, buckets, kinds, arrival times on the sim clock, updated in patch order in `collect()`), shadow edges tagged with their corners, `lead` from the measured frame interval, `SightTunables` with ranges and sliders, and the `what=sightBodies` view.
- Step 3, the sight shape (PR 499): forest depth and the forest shape in `libs/geometry/visibility`, integer throughout, with the slope limit, the polygon path's clip, and the bright shape cut by the forest shape on every path.
- Step 4, the record (PR 504): legs as `SeenArea`'s capsule while their ranges hold, the eased rim, and the five changes to the record's scheduler, each with its test.
- Step 5, the cover (PR 507): the feather on both edges, the record feather in 32 m cells, the pending cut, each colonist's drawn shadows, and the masks composing them, with `what=cover` reporting a frame.
- Step 6, tests and docs (WOR-990): scenario 14's sight-softening steps, player-visibility.md's tunables and measured figures, and fixes to the spec where it had drifted from the code.

Decisions and why:

- A body is opaque or soft by its own size, never by the angle it is seen from: a wall body from 0.6 m across, a placed body from 1.4 m. Kevin asked that nothing flip as a colonist walks, so a kind is fixed when the body enters its index and changes only when the world does. 1.4 m falls between the widest trunk at the landing (1.385 m on its diagonal) and a BigRock (1.76 m), so every tree is soft and every big rock opaque.
- Forest depth pulls the rim in rather than casting each trunk's silhouette. Kevin wanted forest to limit sight ("you can't see deep into the forest") without each trunk carving the shape. Depth is a density over the placed rings in sight, taken at the 64 sides of the sight polygon, capped per ring and floored at 67% of sight, and the shape is convex by construction. A slope limit of 1 m between neighbouring sides bounds what convexity takes from clear directions (about 1.9 m, against up to 9.8 m without it).
- Legs went into the seen record. The forest shape is a polygon that changes as a colonist walks, so the record holds it as legs: a leg runs while its ranges stay within a tolerance and ends on the spread, which keeps bright ground always inside seen ground at a bounded overshoot. Without the five scheduler changes the record was a no-go in forest (the queue reached 101,387 at 50 colonists at 10x).
- The cover is drawn with salal's partial coverage masks, vector pieces carrying a value at each vertex, composed per colonist by the greatest and across colonists by the least darkness. Rule 10 bars any raster, so the fade could not be a blur; the frame's alpha and depth are used only inside the cover's draws.
- Soft shadows are rebuilt per body ring, not per frame. A cadence per body kept some body due on nearly every frame and sent each colonist's soft mesh up again each frame; four rings by distance, each rebuilt after a walk of 10% of its nearest distance, cut uploads at 50 walking from 92 to 36 a frame and their time from 5.6 to 1.7 ms. Due rings go most turned first under a budget of 8,000 pieces a frame, waiting counted as turn. A soft triangle's slicing tolerance became a tunable (12 thousandths of what the player sees), taking 15 triangles a body at the landing against 27.

Measured figures:

- The record's count bars pass at 1x, at 10x, and at 10 colonists: drained by the fifth minute and at the end, backlogs within bounds, queue peaks of 36 and 283, K at most 51.5 mean and 248 greatest.
- The record's time bar 5 is not met at 50 colonists: the mean tick is 0.79 ms against the plain disc's 0.24 at 1x and 1.04 against 0.44 at 10x, the worst 9.45 and 8.33 ms. Step 4 merged by decision with the cost to be brought down after: WOR-1076 ("Seen record: registering a piece costs too much where the record is dense").
- The cover's CPU time a frame at the landing: 0.65 ms at 1 colonist walking, 1.7 ms at 10, 5.2 ms at 50, 0.2 ms at 50 standing. Follow-up WOR-1102 ("Soft cover: bring the per-frame CPU cost down").
- The pending cut at the record's edge: 0.06 ms a frame at 1 walking, 0.66 at 10, 6.2 at 50.

Files: `libs/geometry/visibility/` (ForestDepth, the forest shape, SightClip), `libs/sim/vision/` (GeometryIndex, PlacedOccluderIndex, SightBody, SightFeed, SightTunables, SeenArea), `libs/sim/ecs/systems/VisionSystem.*` and `PlayerVisibilitySystem.*`, `libs/sim/nav/NavigationSystem.*`, `libs/world-view/world/rendering/` (VisibilityCoverRenderer, DrawnShadows, CoverPieces, SeenFeather), and salal's renderer and mask API.

## Related Documentation

- [Sight Softening](../../technical/sight-softening.md)
- [Player Visibility](../../technical/player-visibility.md)
- [Vision Architecture](../../technical/vision-architecture.md)
- [Scenario 14: player visibility](../../testing/scenarios/14-player-visibility.md)

## Next Steps

- WOR-1076 ("Seen record: registering a piece costs too much where the record is dense").
- WOR-1102 ("Soft cover: bring the per-frame CPU cost down").
- WOR-1059 ("A move order into unexplored ground stops in front of a big rock on the line and never gets past it").
- WOR-995 ("Player visibility scenario step 13: ground a teleported colonist is looking at reads visible but not seen"): the step's figures assumed 30 m sight; rewritten as scenario 14's step 34 to read ground inside the forest rim, where seen and visible agree.
- The author's proposals in the spec's decisions log, each awaiting Kevin's word: the 0.6 m wall threshold, the drawn shadows' strengths and caps, the slope limit, the leg tolerance at game speed, and the rest of its "Author's proposals".
