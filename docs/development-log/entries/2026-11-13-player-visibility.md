# 2026-11-13 - Player visibility: unseen ground black, seen ground tinted

## Summary

The player now sees only what colonists have seen. Ground no colonist has looked at is black, ground seen before is tinted with its things shown and selectable, and ground in a colonist's sight right now is drawn as the world was drawn before. Placing, drawing, selecting and move orders all follow the same line: a move order into the black walks the colonist to the edge of the seen ground and stops there, and nothing can be placed on ground nobody has seen. The landing crew starts out knowing the 150 m round the drop.

The seen area is its own record, an exact vector union of colonists' sight shapes kept in a second `PatchableMesh` and apart from the nav mesh, and the cover that draws it is two masked fills through a CSS-shaped mask API that salal gained for this (v0.41.0). No raster, grid, bitmap or mask texture exists anywhere in the feature; that was Kevin's rule, given unprompted, and it shaped everything below.

It was built as a stack of pull requests (374, 375, 377, 378, 382, 387, 386, 388, 389, 390, 392) that were then merged to main through #391, which carries the last six. The spec is `docs/technical/player-visibility.md`; the epic is WOR-754 on Specboard.

## Details

### What was built, in the order it was built

- #374, the spec. Rules 1 to 20, the design, the tunables, prior art (every shipped example found stores the explored area as a raster, which was weighed and set aside), a perf plan and a decisions log. It changed `vision-architecture.md` to point at it and gave the landing crew's starting knowledge to `memory.md`.
- #375, step 1, benches only. A standalone `PatchableMesh` holding only sight outlines as ground rings, driven by exploring legs, wandering, re-walks, camps of 10 and 50 walkers, and the visibility polygons at a doorway (`libs/geometry/nav/SeenRecord.bench.cpp`). Verdict: go, with conditions. `groundCovers` costs about 20 us for every ring over the spot, so the covered test has to read the union's edge instead; a wrapper rather than an upkeep-off option on the mesh; and indoor shapes had to be far smaller than vision's 440-vertex polygons, which patched at 43 ms mean and 90 ms worst at the doorway.
- #377, one `VisionSystem::sightShape(observer)` accessor in place of the polygon-only one: centre, radius, the wall-clipped polygon when there is one, and a revision that changes when it is rebuilt. `VisionOverlay` draws outdoor sight with it.
- #378, the landing crew remembers the landing area (rule 17). `VisionSystem::update`'s per-observer body was split so the same two passes run once with the landing's centre and radius, through the same capability-0 skip and the same 10,000-entry cap as sight. No second discovery path.
- #382, step 3a, benches and the shapes. `computeVisibilityPolygon` takes its rim from `geometry::sightPolygon`, a 64-gon from a compile-time direction table instead of a 720-gon from `std::cos` and `std::sin`, so the ring is the same on every toolchain by construction (the table code is shared with nav's `LimitPolygon` through `core/DirectionTable.h`). `geometry::simpleAround` checks a ring exactly. `geometry::nav::sightShapeCovered` is the covered test, reading the record's edge through a new const `PatchableMesh::meshEdgesNear` with a 5 mm tolerance for wall faces. Doorway polygons fell from 440 vertices to 47 and the patch from 43 ms to 1.4 ms.
- #387, step 3c, the skirt. A shape with walls in sight enters the record grown by 50 mm everywhere off its rim (`geometry::skirtedSightShape` in `libs/geometry/visibility/SightSkirt.{h,cpp}`), while the covered test keeps the true shape. Its test pins a hash of the output so clang's CI checks it against MSVC.
- #386, step 3, the record itself. `libs/sim/vision/SeenArea.{h,cpp}` (the record, naming no mesh type) and `PlayerVisibilitySystem` (priority 46, right after vision). Outdoors a colonist holds a leg, the straight segment from where it began to where he stands, and commits its capsule as one ring when he turns, turns back, jumps more than 2 m in a frame, goes indoors, or is destroyed. The landing is one polygon committed before the crew exists. Dev API: `/api/state?what=visibility`, and `/api/dev/visibility?x=&y=` for the point query, since salal's state views take no parameters.
- #388, step 4, the cover. `worldview::VisibilityCoverRenderer` draws the tint (black at 0.5, masked to the view minus every player colonist's visible shape) and then the opaque unseen fill (masked to the view minus the record and the pending shapes), at keys 30 and 31 after `EntityRenderer::render`. The record uploads only when `epoch()` moves, as however many meshes it takes at 65,535 vertices each, because salal's instanced meshes have 16-bit indices. `/api/dev/reveal?on=1|0` turns it all off, and `scripts/perf` captures run revealed so existing numbers stay comparable (`--no-reveal` times the cover).
- #389, step 5, input. Placement needs the nav check and a seen footprint; drawing a foundation, wall or opening needs every point and the whole shape seen, and the refusal says the area is unexplored; a move order onto unseen ground goes to `lastSeenToward(colonist, click)`, one order to one point; selection takes colonists always, other movers only while visible, and anything else only where its position is seen. This closed WOR-819. Dev verbs are not gated.
- #390, step 6, movers that are not colonists (`AgentRadius`, no `Colonist` tag) draw only while visible. None exist yet, so a test makes one.
- #392, step 3a again, registration. The frontier pieces move out of the bench into `geometry::nav::sightShapeFrontier`, `SeenArea` registers skirt plus pieces, the budget becomes two counts (16 shapes tested a tick, 1 registered), and seen walls and openings join the record as footprints (rule 22).
- #391, the merge of all of it to main, with scenarios 09, 11 and 13 updated, scenario 14 added, the nav doc's notes, and one fix (the corner junction, below).
- salal #95 (CSS `mask` and `mask-composite` on `drawRect`, v0.41.0) and salal #96 (an injected pointer event puts the pointer in the window, v0.41.1). #96 came out of the placement ghost: `/api/input` moves never set `pointerInWindow`, so hover-driven code did nothing under injected input. The gap is as old as salal's dev server. The fix is in the dev server's `InputParser`, which now leads each pointer event with a `PointerEnter`, because only it knows an event is injected. Worldsim's pin is v0.41.1.

### Technical decisions, and what failed first

The record's form. The first draft had a 1 m bit raster and a blurred mask composite. Kevin's reply: "everything data wise we do is vector, NO RASTER." The raster went, and the record became an exact polygon union in the nav mesh's own integer millimetres, which already keeps a grow-only union of overlapping rings.

Whole indoor polygons failed. After the shapes were cheap (#382), registering each indoor polygon whole worked inside ground already seen, where nothing registers, and fell over beyond it. With 2 colonists and an empty record a base registered 775 rings in 10 minutes with patches at 314 ms mean and 1.5 s worst, 171 of them slivers under 0.01 m2 along shadow edges. A polygon's box covers the whole base, so every ring overlaps every other.

Frontier pieces alone failed next (6.3, prototyped in the bench and kept out of the library). Registering only what a polygon adds, cut against the record's edge, brought 2 colonists to 1.2 ms patches, but 20 colonists stopped falling at about 200 rings a minute and 50 ran away: 8,750 rings, worst patch 719 ms, 543 ms of record cost per simulated second. A pinhole in the record deep inside a polygon made a piece of 1 to 10 m2 that overlapped everything near it. An earlier variant of the pieces levelled off nicely but left unrecorded wedges up to 16 mm wide and 25 m long, which the unit test's oracle caught.

The skirt with pieces worked (6.4). Kevin was offered a skirt, a union with no ring registry, or a coarser indoor rule, and said "Try the skirt." Test tight, register loose: the covered test uses the true shape, the record gets the grown one, and slivers and pinholes thinner than the skirt cannot form. Registrations level off at 2, 20 and 50 colonists (1,039, 1,042 and 1,161 rings in 10 minutes, a few a minute after the first), nothing under 0.01 m2, and K where rings register fell from 453 and 2,165 to 57 and 45. Whole polygons with the skirt still lost, since the skirt removes slivers and not size. The price is that seen ground can reach up to 50 mm past true sight; 25 and 100 mm measured the same on rings and cost, so it stayed at 50.

The skirt left the covered test slow in a base beyond the landing (440 to 740 us a polygon), because walls are never seen from inside, so each wall was a hole in the record and its edge ran round every wall. Kevin, asked whether a wall a colonist has seen should count as seen ground: "Yes, walls join." With the 38 wall pieces in, the base's record fell from about 2,300 live triangles to 200 to 270 and the covered test from 480 to 740 us to 14 to 17 us (6.6). Reading each footprint from nav's `extractWalls` meant the visibility polygon's structure pass, which vision already runs for memory, now hands its wall and opening lists to `SightShape` (`segmentsSeen`, `openingsSeen`).

A junction bug survived into #391. A wall junction's footprint was keyed to its smallest wall's id, so a corner stayed a black square until that one wall was seen, even when the other wall of the corner had been. `extractWalls` now hands back every wall meeting at each junction and the junction joins when any is seen (`PlayerVisibilitySystem.CornerJunctionJoinsWithEitherWall`, which fails without the fix).

Two smaller choices. The shape of a leg: the drawn visible disc is 29.84 m for a 30 m sight, 71 mm less than the capsule's inradius allows, so ground never goes from bright to black behind a colonist; and pending shapes count as seen, so the tick a ring commits changes nothing the player sees or can click. And the budget is counts, never times, so the result does not depend on the machine: at 4 shapes tested a tick the queue at 50 colonists indoors at 10x grew without end (204,749 pending after 10 minutes), so it tests 16.

The mask API was rejected twice (decisions log 8). The first version exposed the stencil: `setMask` with write and test modes, a value, a compare. Kevin: "this API is awful. rejected. we follow css naming and best practices." A `pushMask` and `popMask` scope was next, and also rejected: "push/pop arn't in the css language." The third, `mask` as a property of a draw with layers of `mask-image` and `mask-composite`, went to two reviewers at his request, who checked it against the CSS Masking spec and salal's docs, and he approved it. Layers are CSS's: first on top, each composited with what is under it. Only `Add` and `Subtract` are built, and the stencil is named nowhere in the public API. Related, from the same day: Kevin's rule that variable names are whole readable words went into both repos' standards after a `P::` namespace alias in a draft of the code.

### What was measured, and under what load

None of these is a baseline. Every timed run in the stack read `busy` on at least one side of the load check (another session's world-sim, a build, a planet bake), so the times are unconfirmed; counts (rings registered, covered shares, vertices, slivers) do not depend on load and are the evidence behind the decisions above.

- Benches of the record and the tick (6.1 to 6.6), all headless, RelWithDebInfo, one run each. In a base beyond the landing at 50 colonists, the record's cost fell from 80 to 6.2 ms per simulated second once walls joined (0.10 ms a tick at 60 ticks a second). `PlayerVisibilitySystem::update` alone, 36,000 ticks: 0.100 ms mean and 1.15 ms max with 50 colonists walking in the open at 1x; in the base at 50 colonists and 10x, 0.20 ms mean and 0.97 ms p99, with a 22.4 ms max in the final run that registered nothing and coincided with another session's build starting (4.7 ms the run before), worse under load and unconfirmed.
- The cover, in the game at the river landing, zooms 3, 1.5 and 0.75, cover on against main on the same machine: frame p50 8.33 ms on both (vsync-bound), GPU time +0.5 to +1.0 ms (3.03 to 3.67, 2.48 to 3.03 and 3.09 to 4.10 ms), +10 to +13 draw calls. Worse under load, unconfirmed: the feature capture was voided by another session's compiler running, and both load checks read `mostly-idle`, not `idle`. The nightly batch runs with the cover revealed, so it does not settle this on its own (WOR-859).
- Walking into an unseen room, frame max hit 40 ms once and 27 ms once against 8.3 ms otherwise, with another session's world-sim and a build running. Not attributed; the nav mesh grows on the same steps.
- The landing crew's memory: 6,984 entries on the quickstart landing right after it, 70% of the cap, nothing evicted (WOR-861).
- Checked in the game: landing area 70,572 m2 (pi times 150 squared is 70,686); a walk east to x = 262 grew it to 78,189 m2. Tests on the merged tip, RelWithDebInfo: sim 1077, geometry 458, assets 281 (3 skipped, as on main), worldgen 220, world 209, world-view 84, planet-view 20, theme 5, world-sim 24, all passing.
- Not verified in the game: the placement ghost (covered by unit tests, and the reason for salal #96, after which scenario 14's placement steps run through the dev server). The regression pass on the merged tip ran scenarios 01, 03, 04 and 14 clean, 02 partly (steps 1 to 5 and 11 to 13), 12 and 13 partly; a frame-spike walk with reveal on showed no frame over 11 ms.

### What is not built

Section 8 of the spec: the last-seen record for seen things (rule 13, the first build draws live state), the style pass (soft edges, a textured cover, a reveal animation), vegetation thinning sight, the seen area on the minimap (the Minimap epic's), saving the record (there is no game save yet), not loading or drawing chunks with no seen ground (the camera loads them and the cover hides them), smooth indoor sight (the lit area steps with vision's 0.5 m polygon cache), and more than one player faction.

### Open follow-ups

- WOR-858 "The seen area's edge is stepped near walls". The record's outer edge past the landing is a fine sawtooth where successive sight polygons' arcs meet; hard-edged, but visibly stepped at 66% zoom.
- WOR-859 "Time the cover: a cover-on capture in the nightly perf batch, and cut its GPU cost if confirmed".
- WOR-860 "Seen things show as last seen, not live". Rule 13.
- WOR-861 "The landing fills 70% of a colonist's memory cap". Walking evicts the least recently seen entries first.
- WOR-862 "Vision's polygon builds near buildings cost 49 to 174 ms per simulated second at 20 to 50 colonists". Vision's own cost, not the record's; it is the larger one near buildings.
- WOR-850 "PatchableMesh validate fails on a long sliver ground ring whose apex sits millimetres from another ring's vertex".
- WOR-821 "Nav docs and comments: say meshed area, not seen area". The nav mesh's extent is the meshed area; "seen" is the record's.
- WOR-831 "Replace short namespace aliases and one- or two-letter names with readable ones".

### Files

- `libs/geometry/`: `visibility/SightPolygon.h`, `visibility/SightSkirt.{h,cpp}`, `nav/SightCover.h`, `nav/SightFrontier.{h,cpp}`, `core/DirectionTable.h`, `PatchableMesh::meshEdgesNear`, `simpleAround`, `nav/SeenRecord.bench.cpp`, and their tests.
- `libs/sim/`: `vision/SeenArea.{h,cpp}`, `ecs/systems/PlayerVisibilitySystem.{h,cpp}` and its benches, `VisionSystem` (`sightShape`, `rememberArea`, `segmentsSeen` and `openingsSeen`), `DynamicEntityRenderSystem`, `SelectionSystem`, `PlacementSystem`, `DrawingSystem`, `nav::extractWalls`.
- `libs/world-view/world/rendering/`: `VisibilityCoverRenderer`.
- `apps/world-sim`: `GameScene` (the cover's place in the frame, the landing, move orders), the dev command handler (`reveal`, `visibility`).
- `scripts/perf` (reveal around a capture, `--no-reveal`).
- Docs: `docs/technical/player-visibility.md`, `vision-architecture.md`, `nav-mesh-variable-resolution.md`, `main-loop-timing.md`, `docs/design/game-systems/colonists/memory.md`, and `docs/testing/scenarios/` 09, 11, 13 and 14.

## Related Documentation

- [Player visibility](../../technical/player-visibility.md), the rules, design, measurements 6.1 to 6.6, build steps and decisions log
- [Vision Architecture](../../technical/vision-architecture.md)
- [Nav mesh: one mesh, variable resolution](../../technical/nav-mesh-variable-resolution.md)
- [Memory System](../../design/game-systems/colonists/memory.md)
- [Scenario 14: player visibility](../../testing/scenarios/14-player-visibility.md)
- [2026-11-13 - Nav mesh ends at the seen area](./2026-11-13-nav-mesh-ends-at-seen-area.md), the meshed area this sits inside

## Next Steps

- Time the cover with the load check reading `idle` and settle the +0.5 to +1.0 ms (WOR-859).
- The last-seen record (WOR-860) is built with the first thing that can change while nobody watches.
- The style pass and the rest of the not-built list are unscheduled.
