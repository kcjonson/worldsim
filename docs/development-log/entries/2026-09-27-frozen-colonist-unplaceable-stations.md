# 2026-09-27 - Frozen colonist and unplaceable stations after the organic terrain merges

**Board:** WOR-493 (both symptoms), WOR-488 (thirsty colonist stalls after a far camera pan)

## Summary

On main after the organic terrain epic, the quickstart colonist stood still from load and crafting stations wouldn't place. They were two separate defects, both older than the epic, and the epic's geometry changes exposed them.

The colonist froze on a route that was deferred for want of a mesh and never requested again. Placement failed because `buildNavMesh` silently dropped any face its per-face triangulator rejected, and real chunk-clipped water and dense tree layouts produce such faces. The nav mesh is now one constrained Delaunay triangulation of the whole arrangement, so no face can drop. A deferred route is requested again when the nav generation moves, and the "Waiting for the area to settle" hold now gives way to the first real wander.

## Root causes

**Stale pre-mesh route.** The first AI decision runs about 0.5 s before the first region mesh lands. `requestNavPath` holds that route ("no mesh yet") with movement off and no path. Replan-on-discovery needs a valid path, the chain-leg repath needs an active target, and the periodic re-eval re-selects the same option, so `isSameTask` keeps the task unrouted. The requestNavPath comment claimed the re-eval would pick it back up; it doesn't.

Before #282 the landing sat off-mesh in a dropped face, and stranded recovery cleared the task by accident. #282's land-field rings put the landing on a valid face and exposed it.

**The no-option hold never ended.** The hold is a Wander with movement off. A re-eval that picked a real Wander counted it as the same task, so the colonist parked until a non-Wander option won. This is the lingering half of WOR-488.

**Dropped faces.** `buildNavMesh` triangulated each arrangement face with `triangulateWithHoles` (Eberly hole bridges, ear clipping, Lawson flips) and skipped any face it rejected. Replaying dumped `NavMeshInput` from the live game showed two ways that happens:
- Water rings are clipped per 512 m chunk, so a river crossing a chunk corner leaves pieces that meet at one vertex. While chunks stream in, the land face's hole is pinched there, `ringIsSimple` rejects it, and the whole region reads walkable=0.
- A 9159 m² land face with 305 tree holes failed in `mergeHoles` ("no visible bridge"), which dropped the east bank. This is the path the 2026-06-28 zero-walkable fix patched. It still failed on the tree layout a chunk reload produces (see WOR-494), so it hit after a far pan.

**Lagging generation.** `NavigationSystem::generation()` was the max over per-region swap counts. A region whose own count trailed the max swapped in unseen, so stamped routes missed that rebuild.

## Bisect (quickstart, at load, no camera moves)

| Merge | Load mesh walkable/tris | Colonist | Placement near landing |
|---|---|---|---|
| 9153fc44 (#277) | 1398/3010, landing off-mesh | snapped off, harvests, then parks in the hold | 1 of 6 points |
| 095bd068 (#278) | 1398/3011 | same | 1 of 6 |
| fdc01d0b (#281) | 1392/2999 | snapped off, wanders | 1 of 6 |
| 76324c80 (#282) | 3971/5434, landing on-mesh | frozen | 4 of 6 |
| 075bdd7b, 28c2cd7a | 3971/5434 | frozen | 4 of 6 |

The east-bank face drop at load predates the epic's last merges. #282 fixed it at load, but that exposed the stale route. #283's deferred recenter and the retired-build cap aren't involved.

## Details

- **Nav triangulation.**
  - `libs/geometry/triangulation/ConstrainedDelaunay.{h,cpp}` is new. It inserts vertices in index order with Lawson legalization, recovers constraints by Anglada cavity retriangulation, and uses exact integer predicates throughout.
  - `buildNavMesh` builds the CDT of the arrangement, with every arrangement edge as a constraint, and finds faces by flood fill across unconstrained edges.
  - Each face is classified once, from its largest triangle's centroid taken exactly in 3x-scaled space. The classification semantics are unchanged.
  - Deleted `Triangulation.{h,cpp,test.cpp}`, hole nesting, and `representativeOutsideHoles`.
  - dump 0's build dropped from 552 ms to 216 ms. The arrangement's all-pairs split is now about 93% of what remains.
- **AI.**
  - `NavState::AwaitingMesh` plus `Task::deferredNavGeneration`. `applyNavOutcome` is the one mapping from a route outcome to nav state, used by every `requestNavPath` call site.
  - A deferred-route retry in `AIDecisionSystem::update` fires when `generation()` moves.
  - A Wander counts as the same task only while its movement is live.
  - The panel shows "Waiting for the area to settle" for a deferred route.
- **Nav generation.** `generation()` counts every region's swap.
- **Tests.**
  - geometry-tests 339 (dumped-input fixtures in `NavMeshDumpRings.test.h`, checked against a brute-force ring oracle; touching-ring and collinear-hole cases; random scenes).
  - engine-tests 1037, adding:
    - `LandAroundWaterPinchedAtAChunkCornerIsValid`;
    - `GenerationMovesWhenALaggingRegionSwaps`;
    - `IdleHoldTakesLiveWanderOnceMeshLands` and `HeldGatherFoodRouteRequestedOnceMeshLands`, each failing before its fix.
- **In game (RelWithDebInfo, quickstart):**
  - the colonist walks within a second of load;
  - a Crafting Spot placed through the Production menu at (11.9, -0.5) got a Basic Box crafted at it;
  - after panning to (70030, 0) and back, every land probe point is valid, placement works near the landing, and a thirsty colonist drinks within 2 s.

## Related documentation

- `docs/technical/pathfinding-architecture.md` (how the mesh is triangulated now)
- `docs/technical/organic-terrain/terrain-polygons-architecture.md` D9 (rings meeting at a chunk corner are ordinary input)
- `docs/design/game-systems/colonists/memory.md` (deferred-route panel state)

## Next steps

- WOR-494: chunks streamed in during play place flora with the hard-coded seed 12345, so trees move after a reload.
- WOR-495: a task whose route is Blocked can re-select itself forever.
- The arrangement's all-pairs split dominates nav build time; an x-sweep prefilter is noted in known-issues.
