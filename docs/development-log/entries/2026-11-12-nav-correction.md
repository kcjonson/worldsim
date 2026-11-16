# 2026-11-12 - Nav correction: movers mesh, obstacles block, routes by ticket

## Summary

The nav code now matches the design settled on 2026-11-11 (the spec's rules 22 to 38). There is
one mesh at full detail and one patch job; the structural mesh and everything built on it are
gone. Only movers mesh, each its sight distance plus a 16 m buffer, and the camera has no effect
on the mesh. Placed obstacles block, and nothing is passed a price. Nothing leaves the mesh but
what leaves the world. Whether two places are joined is a component label compare; whether a
mover can get there is a route request answered by ticket, searched on a route worker under a
fixed amount of work a tick with no limit on any one search.

This is the work WOR-796 (Nav: the camera must have no effect on the mesh), WOR-797 (Nav: movers
generate the full real mesh around them, placed obstacles included), WOR-798 (Nav: objects movers
cannot path through block in the mesh instead of being priced) and WOR-799 (Nav: one full-detail
mesh with no coarse layer) asked for. None of them is closed: they close when the integration
branch merges. The AI's choice of target is built.

## Details

In the order it was built, on `integration/colonist-survival-with-nav`:

- Component labels (`libs/geometry/nav/MeshComponents.{h,cpp}`). One label per triangle, kept per patch from the patch's report; water and placed obstacles block, walls read open, width zero. A label epoch that moves when a way may have opened. Patch reports now list each changed triangle with why, and the width refresh is one batched call per job with its own report.
- A resumable search (`SearchState` in `PathQuery.h`). Runs in steps, takes any number of goals and answers the nearest by walk, and searches from the goal first when there is one. `pathThrough` is the same search run to the end.
- Chunk holding per mover (`ChunkManager::update(camera, anchors)`, `ChunkAnchor`). Each mover holds the larger of 128 m and its range, keeps the larger of 256 m and twice it.
- The route queue (`libs/sim/nav/RouteQueue.{h,cpp}`). Tickets, `kRouteWorkPerSlice` (18,000) expansions a slice shared `kRouteQuantum` (2,000) at a time, a restart whenever a patch changes what a search read, answers carrying the epochs they hold for.
- Movers mesh, the camera does not (`NavigationSystem`). Each mover wants the 16 m cells within its range, nearest first, its own cell and the eight touching it ahead of everything; only chunks a mover holds are served. Every removal path and every camera input deleted.
- Collision and the snap on every mover, as one rule: the snap records who it leaves inside a placed obstacle and collision leaves them alone.
- Routes by ticket (`requestRoute`, `takeRouteAnswer`, `cancelRoute`, `routeTicketLive`), added beside the old synchronous path, the AI switched to them, then the old path deleted.
- Label queries and position checks on the mesh (`connected`, `connectivityEpoch`; validity, buildability, stand points and the landing read the mesh alone).
- The AI's tickets and target choice. A colonist stands awaiting a route, or walks on while a replacement is out; a target inside an obstacle is routed to its stand point; "no known way" is a verdict on that target for him; a need's source is one nearest-of route request over the nearest few connected candidates.
- Reach and the structural mesh deleted, with the far route, the step budgets, the gave-up record and the settled-ground wake.
- Recipes load after the asset definitions in both bootstrap paths; the async path runs the recipe load on the load worker before it reports done, and an unresolved recipe definition logs an error. Before, the game's normal start resolved recipe ids against an empty index and no craft got materials.
- The geometry clean-up. Tag prices and the squeeze, the second corridor width (`structuralWidthMm`, `detailTags`), the reachability forests and `PatchReport::freedTriangles` and `newTriangles` are deleted. `pathThrough` rejects a query across components by their labels, and a path costs its length. `BM_SearchReach`, `BM_SearchDeadEnds`, `BM_ForestRebuild*` and `BM_SealedGoal` are deleted.
- The nav overlay (N key) draws walkable edges purple, routes yellow and blocked edges red.
- The disc rule. A mover wants every 16 m cell whose nearest point is within R of where he stands (true distance), retaken every 4 m he moves (`kDiscRetakeStepMm`) or when R changes. Before, the distance was measured cell to cell over a 7 x 7 square of cells: a 112 m square reaching 73 to 85 m to its corners. Now it is a disc reaching R to a cell edge, and about R plus one cell diagonal to an obstacle's centre.

Where the build departed from the plan, and why (the spec's entries 103 to 110 have each):

- The route step went in as three (add the ticket API, switch the AI, delete the old path) so every commit built and passed.
- The plan restarted a search once and then held back patches meeting its box. A patch's hole can reach far past its edit, and predicting a patch's freed triangles caught only 37% of them on the fixtures, so no prediction can bound restarts. Searches restart on every hit instead, counted and never limited, and the deferral box was deleted.
- The first hold on additions (everything held while any search spanned ticks, one tick released in 241) starved meshing. Now only placed obstacles of cells away from every mover wait, and each search can hold them for at most `kMaxHeldAdditionTicks` (240) over its whole life.
- First-in-first-out meshing served cells behind a mover before cells ahead; it is nearest first with a near lane now.
- Collision pushed snapped movers back the way they came; the snap and collision are one rule now.
- Position checks moved from the placement data to the mesh, which keeps what was meshed after a chunk unloads.
- A colonist livelocked on a source sealed for his width but connected at zero width. The verdict rule and the nearest-of choice fixed it; the fix round still in progress makes verdicts survive a busy colony.

The findings and fixes of the four review rounds are in the spec's entry 113.

Tests on the final head: sim 1030, geometry 388, assets 280, world 209, world-view 74, world-sim 7.
Worldgen (220), planet-view (20) and theme (5) last ran before the geometry clean-up.

In game: scenario 01 passes and scenario 13 passes. Scenario 12 was only partly exercised: its
setup needs a spot with no bush within 30 m, which Quick Start's landing does not have.

Not done:

- Record slimming, WOR-801 (Nav: cut the mesh cost of a placed obstacle from about 7.2 KB to about 1.25 KB). A placed obstacle still costs about 7.2 KB and meshed dense ground about 435 to 490 MB per square kilometre.
- The geography mesh, WOR-802 (Geography mesh (Tier 1): computed at load, always exists).
- Chunk-border slivers, WOR-809: chunk borders stay constrained edges split every 64 m, so long thin triangles bunch along them.
- Bench fixes, WOR-808, after the geometry clean-up.
- Queries outside the tick waiting on the route slice, WOR-804.
- The route and walking-mover measurements have not run.

Files: `libs/geometry/nav/{MeshComponents,PathQuery,PatchableMesh}.*`,
`libs/sim/nav/RouteQueue.*`, `libs/sim/ecs/systems/{NavigationSystem,StaticRectCollisionSystem,AIDecisionSystem,ActionSystem}.*`,
`libs/sim/ecs/components/{Task,SourceReachMemo,MemoryQueries}.h`,
`libs/world/world/chunk/ChunkManager.*`, and their tests.

## Related Documentation

- [Nav mesh: one mesh, variable resolution](../../technical/nav-mesh-variable-resolution.md): section 1.1 (what is still open), sections 4.4 to 4.8 (the code as built), section 10 entries 99 to 113.
- [Pathfinding architecture](../../technical/pathfinding-architecture.md), [Main loop timing](../../technical/main-loop-timing.md), [Colonist AI behavior](../../design/game-systems/colonists/ai-behavior.md#what-a-colonist-can-get-to).
- [Scenario 13: colonists walk round obstacles](../../testing/scenarios/13-colonists-walk-round-obstacles.md).

## Next Steps

- Close WOR-796 to WOR-799 once the integration branch merges.
- WOR-801 (Nav: cut the mesh cost of a placed obstacle from about 7.2 KB to about 1.25 KB), WOR-809 (chunk-border slivers), WOR-808 (bench fixes), WOR-804 (queries outside the tick waiting on the route slice).
- Later: WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet), WOR-802 (Geography mesh (Tier 1): computed at load, always exists), WOR-754 (Player visibility: unexplored areas (visible, seen, unseen)), WOR-710 (Step 7: Mover footprints in the mesh), WOR-709 (Step 6: Movers (route system out of AIDecisionSystem, a wandering deer)).
