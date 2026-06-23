# 2026-06-23 - Navigation P3: regional layer (reachability, connectivity, RRA*)

## Summary

The navigation system gained its regional layer: a cheap, sound reachability test that rejects a provably-unreachable goal before the triangle A* runs, plus the corridor-width machinery that makes every query honor an agent's actual size. A colonist asked to reach a walled-off room or a spot across a lake now stops in O(log n) instead of exploring the whole connected region before giving up, and a gap too narrow for an agent yields "no path" rather than a path that clips the wall. The fix that pays off most at scale: an RRA* heuristic that, on a concave detour, cut the A* from 82 node expansions to 16 for the same path, and shares one reverse search across every agent heading to the same goal (the future raid horde).

Landed as five tested, committed increments (P3.1 through P3.5), geometry first, then the engine wiring, with the spec and tracking reconciled in P3.6.

## Context: a design decision settled first

The original spec ([pathfinding-architecture.md](../../technical/pathfinding-architecture.md) section 3) sketched the regional layer as per-chunk connectivity components with traversal-class IDs (the Factorio FFF-317 abstraction). Two realities reshaped that, and one agent-model question had to be answered before any of it made sense.

- **The mesh is one merged loaded-area arrangement, not per-chunk meshes** (P2's decision). So components are a flood-fill over the existing `NavTriangle::neighbor[3]` adjacency, with no cross-chunk perimeter stitching.
- **Agents are not discs, and not a fixed set of size classes.** Every asset has its own collision shape, and a colonist already has four directional states. A long snake is a long thin rectangle.

The settled model (articulated routing): collision (Tier 3) keeps the real, per-direction footprint, but routing reduces each agent to a scalar clearance equal to half its narrow cross-section, because facing follows velocity, so a creature moving down a corridor presents its narrow side and the long body trails the head along the routed path. This keeps the shipped P2 funnel intact (its scalar shrink is reread as half the narrow width) and means reachability must answer for any continuous clearance, not a handful of classes. A bounding circle (rejected) would make a snake unable to enter anything; orientation-aware C-space routing (rejected) is gold-plating for creatures that always turn to face travel.

## Details

### P3.1 - Corridor-width filtering, un-deferred (geometry)

`buildNavMesh` now computes Demyen and Buro corridor widths per triangle (the max disc diameter that fits passing the two edges meeting at each apex), measured against **common-knowledge obstacles only** (water and flora, the `faceBlocker < 0` faces), stored as `NavTriangle::edgePairWidthMm[3]`; door clear widths are threaded onto portal edges as `edgeClearWidthMm[3]`. The triangle A* width-filters on these, so it replaces the funnel's old silent midpoint-collapse (which produced a clipping path through a too-narrow gap) as the passability authority. The A* search state became (triangle, entry-edge) because the squeeze to leave a triangle depends on the edge entered (the apex shared by entry and exit). All comparisons are exact integer (squared distances), so determinism holds. Clearance is measured as true distance to obstacle features, never edge length, so an open-floor CDT sliver stays unconstrained instead of falsely narrow. Belief-dependent wall clearance is deferred: construction guarantees >= 0.7 m wall gaps, so it only matters for oversized agents in built corridors, content that does not exist yet.

### P3.2 - Accelerated point location (geometry)

`locateTriangle` was an O(triangles) linear scan, called twice per reachability query. Replaced with a CSR uniform grid built into `NavMesh` (cell sized to roughly one triangle), with candidates stored ascending per cell so the grid reproduces the linear scan's lowest-index tie-break exactly. The grid is one cell wider than the AABB, so a point inside any triangle always lands in a cell that triangle was inserted into.

### P3.3 - Width-aware connectivity and the cheap reject (geometry)

Two Kruskal max-spanning reconstruction-tree forests, built over the triangle dual graph in `buildNavMesh`: a **truth forest** (floor plus door spans) for AI goal validity, and a **terrain forest** (walls treated as open, only common-knowledge terrain blocks) whose disconnect is sound for every possible belief. Edge capacity is the sound disc diameter a portal admits: `min` over the two triangle sides of `max(apex widths at the portal's endpoints)`, plus the door clear width. `bottleneck(a, b)` (the widest disc any path between two triangles admits) is the capacity at their LCA, answered by binary lifting. The public `reachable()` rejects when the two triangles are in different components, or when `bottleneck < diameter`; `pathThrough` short-circuits on this before the A* allocation, so `requestPath`, `isReachable`, and `AIDecisionSystem` all skip a failed search with no call-site change.

Soundness (the only direction relied on): a disc of diameter D crossing a path uses at each portal an apex passage <= that portal's capacity, so D <= the path's min capacity <= `bottleneck(a, b)`. Hence `D > bottleneck` proves unreachable. The terrain forest is sound for every belief because the most optimistic agent (knows nothing, routes through every unseen wall) is exactly the terrain graph, and any belief is a subgraph of it. A `false` from `reachable()` therefore never hides a real path. This is verified by a fuzz test: 5 meshes times 400 (start, goal, radius, belief) tuples assert that a reject implies `pathThrough` also returns empty.

### P3.4 - Engine wiring (engine)

`NavigationSystem::isReachable` was a full `requestPath().has_value()`; it now delegates to `geometry::nav::reachable` (O(log n)). Its contract is the sound asymmetric reject: `false` means definitely unreachable, `true` means maybe. No-mesh returns `true` so colonists still beeline during startup and outdoor play. `AIDecisionSystem`'s `Blocked` outcome (stop, zero velocity, `NavState::CantFindWayTo`) is byte-identical, because it routes through `requestPath`, which already short-circuits via P3.3.

### P3.5 - RRA* heuristic and A* instrumentation (geometry + engine)

A Reverse Resumable A*: one backward Dijkstra from the goal triangle, run on the **terrain graph, width-unfiltered, with the forward cost metric**, giving the exact remaining graph distance as the forward heuristic. That choice makes it admissible and consistent for every forward query (any agent's true remaining cost is a shortest path on a subgraph with the same edge costs, so it is never overestimated), and belief- and radius-agnostic, so one resumable reverse search per goal triangle serves every agent. `pathThrough` takes an optional `RraCache*` and uses it only when it targets the located goal, else the straight-line fallback (so the pure free function and all geometry-tests are unchanged). `NavigationSystem` owns the caches keyed by goal triangle, cleared on each mesh swap (triangle indices go stale) and bounded at 64. `PathResult` gained `nodesExpanded` and `peakOpenSet`, aggregated by `NavigationSystem::navQueryStats()` so a later overlay can confirm the win.

## Files

- `libs/geometry/nav/NavMesh.{h,cpp}` - corridor widths (edge-pair + door clear), point-location grid, the two reachability forests, the face-walkable predicates (`isFloorFace`, `isCommonKnowledgeTerrainFace`, `truthTraversable`, `terrainTraversable`), `reachableInForest` / `bottleneckInForest`.
- `libs/geometry/nav/PathQuery.{h,cpp}` - width-filtered (triangle, entry-edge) A*, grid-backed `locateTriangle`, the `reachable()` reject + `pathThrough` short-circuit, the RRA* heuristic seam, `PathResult` instrumentation.
- `libs/geometry/nav/RraCache.h` (new) - the resumable reverse-search cache + `rraHeuristic`.
- `libs/geometry/nav/{NavMesh,PathQuery,Reachability,RraHeuristic}.test.cpp` - 33 new geometry tests.
- `libs/engine/ecs/systems/NavigationSystem.{h,cpp}` - `isReachable` on the reject, per-goal RRA* cache lifecycle, `NavQueryStats`.
- `libs/engine/ecs/systems/NavigationSystem.test.cpp` - 6 new engine tests.

## Testing

geometry-tests 255 (was 222), engine-tests 721 (was 718), all green. Acceptance gates: the reject-implies-no-path soundness fuzz, the RRA* admissibility check against a brute-force reverse Dijkstra, same-path-fewer-expansions (82 to 16 measured), sliver safety, fit/no-fit by exact clearance threshold, and truth-unreachable-but-belief-routable (the terrain reject must not fire when an agent could optimistically route through unseen walls).

## Related Documentation

- Spec: [pathfinding-architecture.md](../../technical/pathfinding-architecture.md) (section 3 Tier 1/3 and section 7 P3, reconciled to the as-built design)
- Prior phases: [2026-06-16 - Navigation v1](./2026-06-16-navigation-v1.md), [2026-06-19 - Navigation P4 belief filtering](./2026-06-19-navigation-belief-filtering.md)

## Next Steps

- **P5 - Crowds:** velocity-obstacle avoidance, occupancy costs, door slot queues, the regression rig. This is where per-direction collision shapes (Tier 3) and resting-agent-as-dynamic-obstacle land.
- **P6 - Global tier and raids:** hex-graph A*, abstract party records, attention bubbles, materialization handoffs, raider scouting. Consumes P3's reachability for staging and target selection.
- **Deferred from P3:** belief-dependent wall clearance for oversized agents; local dirty-section mesh invalidation (whole-mesh rebuild today); the NavOverlay/HTTP surface for the A* instrumentation; deriving an agent's routing clearance from its asset collision shape (the narrow cross-section).
