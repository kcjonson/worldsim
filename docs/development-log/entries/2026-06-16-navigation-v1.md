# 2026-06-16 - Navigation v1: colonists path the world

## Summary

Local, vector-native navigation now works end to end. Colonists path around walls, water, and trees on a dynamic navmesh, walk through doors, and physically can't clip through a solid wall. This is the first two tiers of the four-tier pathfinding plan (agents-are-physical + local CDT navmesh) plus the construction-obstacle slice of the dynamic-world tier — enough that walls finally ship as real gameplay rather than decoration.

Delivered across six PRs: #161 (exact in-circle predicate + physical agents), #162 (geometry core: triangulation, navmesh, path query), #163 (review fixes), #165 (per-asset collision shape + NavInputBuilder), #166 (NavigationSystem runtime + path-following), #169 (wall-collision safety net).

## Details

The work splits into a pure exact-geometry layer (`libs/geometry`) and an engine/game runtime layer (`libs/engine`, `apps/world-sim`). All navigation geometry is integer millimeters; floats appear only at the meters↔mm seam and for rendering.

### Geometry (libs/geometry)

- **`inCircle` predicate** (`predicates/Predicates.cpp`) — exact in-circle test via an Int128 lifted determinant, alongside the existing `orientation`. Region-local precondition documented (the degree-4 determinant stays exact while pairwise coordinate differences are within ~2^30 mm).
- **Constrained Delaunay triangulation with holes** (`triangulation/Triangulation.cpp`) — ear clipping that tolerates collinear boundary vertices (a T-junction split leaves three collinear vertices on a face, so this matters on real input: convex ears are clipped first and a collinear corner is absorbed only when no convex ear remains, with a strict-interior blocker test plus an on-diagonal guard), hole bridging, and Lawson flips to restore the Delaunay property. `inputIsValid` rejects edge-crossing holes, not just vertices outside containment.
- **Navmesh assembly** (`nav/NavMesh.cpp`) — segment arrangement → face extraction → walkability classification (inside any walkable border, outside every blocked ring) → triangulation per face → adjacency, with door portals carried through. Supports one or more walkable-bounds polygons.
- **Path query** (`nav/PathQuery.cpp`) — point location, triangle A*, and a funnel (string-pulling) that shrinks by the agent radius and rejects corridors narrower than the disc.

### Engine input (libs/engine)

- **Per-asset collision shape** (`assets/AssetDefinition.h`, parsed in `AssetRegistry.cpp`) — an optional `CollisionShape` (none / circle+offset / polygon, local meters) authored in asset XML, separate from the visual. Obstacle trees declare a base circle at the trunk (Oak 0.1 m, Maple 0.075 m, Palm 0.06 m), deliberately not the canopy; grass and bushes declare nothing and stay passable.
- **NavInputBuilder** (`nav/NavInputBuilder.cpp`) — turns live world state into `geometry::nav::NavMeshInput`. Water tiles become blocked rings via marching squares (edges oriented water-on-the-left so loops come out CCW for shores and CW for islands, then simplified); flora become rings from their collision shape; built walls become resolved bands plus junction fills, with each door cutting a gap (the solid band is replaced by flanking bands over the solid sub-spans, and a portal carries the opening's jamb points + clear width); windows stay solid; blueprints emit nothing. `openingFootprint` moved from the app layer into `libs/engine/construction` so the engine builder can call it.

### Engine runtime (libs/engine, apps/world-sim)

- **NavigationSystem** (`ecs/systems/NavigationSystem.cpp`) — owns the cached navmesh. It rebuilds when `ConstructionWorld::version()` bumps or the loaded-and-placed chunk set changes. The extraction (`buildInput`, which reads the non-thread-safe `ConstructionWorld` and per-chunk data) runs on the main thread; the resulting input is moved into a `std::async` worker that does only the heavy triangulation, and the old mesh keeps serving queries until the new one swaps in. `requestPath(start, goal, agentRadius)` queries the current mesh synchronously and returns waypoints in meters.
- **Path following** — a `NavPath` component carries the route. `AIDecisionSystem` requests a path at the one seam where it sets a destination; `MovementSystem` steers waypoint to waypoint when a valid `NavPath` is present and falls back to the unchanged beeline otherwise.
- **WallCollisionSystem** (`ecs/systems/WallCollisionSystem.cpp`) — the safety net for agents the navmesh can't help (no path yet, an unreachable goal still beelining, or shoved into a wall by crowd separation). It pushes an overlapping agent disc out to the band edge, with two relaxation passes for corners and a position-only correction. Door gaps are exempt using the same clear-width span the navmesh cuts the band at, so the two agree.
- **NavOverlay** (`apps/world-sim/.../nav/NavOverlay.cpp`) — `N` toggles a debug draw of the mesh wireframe and each colonist's remaining route.

### Key decisions

- **One merged loaded-area mesh, not per-chunk meshes.** Per-chunk inputs are cached; the whole loaded area triangulates as a single arrangement so paths cross chunk seams without stitching. The cost (re-triangulating the region on a change) is paid off-thread.
- **Main-thread extraction, async triangulation.** `ConstructionWorld` isn't thread-safe, so the snapshot is taken on the game thread and only the self-contained `NavMeshInput` crosses to the worker (owned by value, no dangling).
- **Collision agrees with the mesh by construction.** Both the door-gap navmesh cut and the collision-exemption span use the identical clear-width formula, so a colonist routed through a doorway is never pushed out of it.
- **Determinism kept where it was claimed.** The agent-separation fallback normal uses a fixed integer-indexed direction table rather than `cos`/`sin` (libm varies across platforms), preserving the multiplayer-replay property.

### Tests

geometry-tests 204/204 (in-circle exactness sweeps, collinear/diagonal-hole triangulation, navmesh build, path-through-door); engine-tests cover the collision system, the asset collision-shape parser, NavInputBuilder (water contours with island holes, flora octagons, wall bands, a door splitting a band, an end-to-end extract→build→query), NavigationSystem (path through a door, blocked by window/wall, rebuild on version change, deferred wiring), and WallCollisionSystem (push-out, door-gap pass-through, window block, corner). Sandbox-verified in the running game: the mesh builds from live terrain, a colonist follows a computed path, and dropping a wall rebuilds the mesh to triangulate around it.

## Related Documentation

- Spec: `/docs/technical/pathfinding-architecture.md`
- Geometry foundations: [2026-06-12 - Geometry Foundations](./2026-06-12-geometry-foundations.md)
- Construction (the obstacle source): [2026-06-13 - Construction Epic D: walls](./2026-06-13-construction-epic-d-walls.md), [2026-06-14 - Construction Epic F1: openings](./2026-06-14-construction-epic-f1-openings.md)

## Next Steps

- **P3 — regional layer:** chunk connectivity components + a reachability API + RRA* heuristic, for long-range and cross-region queries.
- **P4 — belief planning:** memory-filtered planning and discovery replans, which need the Vision System (occlusion & discovery) before they mean anything.
- **P5 — crowds:** velocity-obstacle avoidance, occupancy costs, door-slot queues.
- **P6 — global tier + raids:** planet hex-graph A* and abstract party records.
- **Optional polish:** brighter debug overlay, waypoint substepping for fast movers (moot at colonist speeds), a nav-state line in the colonist info panel.
