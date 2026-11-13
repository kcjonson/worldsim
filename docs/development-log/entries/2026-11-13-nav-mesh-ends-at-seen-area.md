# 2026-11-13 - Nav mesh ends at the seen area

## Summary

The nav mesh is now the union of what movers have seen, and it ends there. Each mover's
visibility limit, a 16-sided polygon holding the disc of his sight distance plus a buffer, goes
into the mesh as a ground outline; the placed obstacles inside it are found by entity and go in
ahead of or with it; outside the union there are no triangles at all. Chunk squares, the 64 m
points on their sides, kept points, partners, and 16 m cells are gone, so every vertex is a corner
of a placed or built object, a point of terrain, a corner of a visibility limit, or a crossing of
two of those. The landing meshes `kLandingSightMeters` round the drop before the crew stands.

This is WOR-813 (terrain meshed a chunk at a time where the mesh should end at the seen area) and
WOR-812 (slanted outlines patched differently from a fresh build), on five stacked pull requests, none merged yet. Kevin's reasons are in decision entry
116 of the spec; the design as built is its section 4.4, and its tunables, costs and known limits
are 4.11.

## Details

### What was built, by pull request

- #369, slanted outlines. `buildNavMesh` classifies faces by a parity flood from the frame face instead of one sample per face, so it agrees with the patched mesh beside a bent edge. The patch seeds a group of new triangles from the kept cover across a fixed edge, else from a sample clear of every nearby ring's snap band, else by reading each ring's containment along a ray against its outline as rounded; when nothing can be read it fails loudly (`PatchReport::unreadCovers`). A slanted-outline fuzz failed on main and passes.
- #370, no kept points. Pins and their bookkeeping, and the Arrangement's point-site argument, are deleted. Validate fails any vertex that ends no constrained edge.
- #371, overlapping ground outlines. Ground outlines may overlap, nest, touch or share sides; `groundBoundaries` gives each ground edge its boundary intervals, the stretches where ground differs across it, and only those are arranged. Partners, `BoundsSeams` and the overlap check are deleted. `groundCovers(outline)` answers whether adding an outline would change nothing. Ground edges with intervals and ground rings get grids of their own. Removal and move of a ground outline work with no extra code. A ground outline spanning `kMaxGroundExtentMm` is refused (`PatchReport::refusedRings`).
- #372, the mesh edge as a wall. `isObstacleEdge` reads outside the mesh as blocked, so corridor widths keep a mover's radius off the edge of the seen area; a corner counts as an obstacle when any edge of its fan is one.
- #373, the game side. `nav::visibilityLimit` builds the limit from a compile-time direction table (`LimitPolygon.h`, pinned by a test). `trackMovers` queues a limit when a mover appears, has walked `kLimitRetakeStepMm`, or changes range, unless the meshed ground holds it. A limit goes in when its chunks are ready, placed and held; its placed obstacles are queried by entity in its box grown by `kFootprintReachMeters` and go in first. A mover's first limit, and his newest once he nears the edge of his served limit, are urgent and go in outside every budget; `stepWithinSight` stops a mover where he would see past the mesh until it lands. Removals are looked up by key. `RouteOutcome::StartOffMesh` became `AwaitingMesh`, answered only while the start or a goal lies in a limit on its way in, and released when a limit is dropped. Dev `teleport` and `colonist` accept unseen ground. `splitLongEdges` and `maxBoundsEdgeMm` are deleted. Benches for walking movers, the landing, and hanging water.

### Technical decisions

- Cut points where a placed-obstacle or water outline crosses the seen edge are kept; they are where an object's outline meets a limit's.
- Water stays cut at chunk sides until water outlines merge across chunks.
- `kLimitSides` 16, `kLimitRetakeStepMm` 4 m, `kSightBufferMeters` 16 m (now set by what it must cover: the step, a step of margin, and travel during one landing), `kLandingSightMeters` 150 m, each one constant.
- A limit is urgent when its mover has no served limit or stands `kSightBufferMeters` minus `kLimitRetakeStepMm` from the served centre, which leaves one step of margin for any sight. A mover whose urgent limit cannot go in stops short, task and route kept.
- A placed obstacle that lands over ground already meshed marks the routes it crosses, as a wall does. A refused ring stays out of the obstacle index.
- The landing's cleared placed obstacles are taken out of the mesh, and the removal waited for, before the crew stands.
- The AI stamps the awaited-ground epoch when it asks for a route, and re-asks an `AwaitingMesh` answer no sooner than `kReEvalInterval`.
- The game logs `unreadCovers` and `refusedRings` as errors once per patch, naming the outline.

### Bugs found and fixed on the way

- A waist between two facing corners narrower than a disc let the disc through: a corner counted as an obstacle only when one of the triangle's own edges at it was one. It is on main; it passed 6 of 6 corner-to-corner cases and 9 of 12 limit-waist cases before the fix.
- A patch could miss splitting an unchanged edge that a new segment meets as given but not as rounded (a corner 0.2 mm under a bent piece), and a later rounding cascade could leave a vertex the fresh build has. Both are fixed by pulling in edges whose input the new segment meets; the first is on main too.
- Two test fingerprints differed between clang and MSVC because two random draws were arguments of one call; the draws are hoisted.

### Known and not fixed

- The path query does not gate the edge between adjacent start and goal triangles, so a disc slightly wider than a waist can cross it when it starts in the triangle beside it. On main before this work; filed as a bug.
- Urgent work has no per-tick ceiling: 159 ms worst tick at 50 movers and 360 ms at 100 at 10x (one run each, RelWithDebInfo, headless).
- A budgeted limit in dense ground costs 4.4 to 6.6 ms on the worker against the 2 ms job budget.
- Open ground with no placed objects has no interior vertices; its triangles span the open region (1,049 m longest edge in a 1 km swept square). Kevin's decision on what to do is open.
- Walls do not clip a limit; crafting stations and storage do not block movement; a player move order onto unseen ground does nothing, silently.

### Files

- `libs/geometry/nav/`: `NavMesh.{h,cpp}`, `PatchableMesh.{h,cpp}`, `LimitPolygon.h`, `PathQuery` tests, `Arrangement.{h,cpp}` (point sites deleted), and their tests and benches.
- `libs/sim/`: `ecs/systems/NavigationSystem.{h,cpp}`, `nav/NavInputBuilder.{h,cpp}`, `nav/RouteQueue.h`, `ecs/systems/AIDecisionSystem.{h,cpp}`, `ecs/components/Task.h`, `ecs/systems/MeshBand.test.h`, and the nav and AI tests and benches.
- `apps/world-sim`: the dev command handler (teleport and colonist onto unseen ground).
- Docs: `docs/technical/nav-mesh-variable-resolution.md` (rules 39 to 42, sections 4.1 to 4.12, entry 116), `docs/technical/main-loop-timing.md`, `docs/technical/pathfinding-architecture.md`, `docs/testing/scenarios/13-colonists-walk-round-obstacles.md`.

## Related Documentation

- [Nav mesh: one mesh, variable resolution](../../technical/nav-mesh-variable-resolution.md), sections 1 (rules 39 to 42), 4.4, 4.11, 4.12 and entry 116
- [Scenario 13: colonists walk round obstacles](../../testing/scenarios/13-colonists-walk-round-obstacles.md)
- [2026-11-12 - Chunk borders dissolve](./2026-11-12-chunk-border-dissolve.md), which this replaces

## Next Steps

- Merge the five pull requests bottom-up once each is green on its head.
- Player visibility (WOR-754, unexplored areas: visible, seen, unseen) is the next epic and starts in a session of its own.
