# Pathfinding & Navigation — Technical Architecture

**Status:** Design
**Created:** 2026-06-12
**Consumes:** the obstacle/portal contract in [Building & Construction Architecture](./building-construction-architecture.md) (D5)

How agents move through an infinite, all-vector world: global routing across the planet, regional routing through loaded chunks, exact local navigation around freeform buildings, and physical agents that take up space. The organizing idea is **resolution tiers**: navigation fidelity scales with proximity to things that matter, because most of an infinite world isn't loaded and doesn't need exact answers.

---

## 1. Requirements

From the game, not from the literature:

1. **Vector-native.** The entire world is vector geometry: terrain features, freeform polygon buildings, even moving entities. Navigation should consume exact geometry, not rasterize it away.
2. **Agents take up space (hard requirement).** Colonists, animals, and raiders cannot overlap. Doorway congestion, queues at chokepoints, and crowd pressure should emerge from agents being physical.
3. **Infinite world.** Chunks stream around the camera and around movers (512 m; in the game the camera loads 3 by 3 chunks and keeps 5 by 5). Nav mesh exists only where movers have generated it, each within its sight distance plus a buffer, and it only grows; the camera's chunks are drawn, not meshed. Yet agents must traverse unloaded space: the signature scenario is a raid party crossing the globe to the settlement.
4. **The raid scenario end-to-end:** route from anywhere on the planet → arrive near the settlement → coordinate an approach as a group → pick targets → navigate interiors through doors. Section 6 traces it through every tier.
5. **Colony-sim scale.** Dozens to low hundreds of exact agents near the action; arbitrarily many abstract parties far away.
6. **Plays well with the engine:** TimeSystem speed multipliers (up to 10x), determinism-friendly (multiplayer future), async-friendly (TaskPool exists), and the construction contract (walls publish exact band polygons, openings publish portals, shared clearance constant, nav version counter).
7. **No omniscience.** The memory system is a core differentiator: an agent cannot path to, or through, what it has never seen ([Memory System](../design/game-systems/colonists/memory.md)). Navigation runs against the agent's *believed* world: unknown doors don't exist for them, unknown walls don't block their plans until seen, remembered things may be gone on arrival, and raiders know the colony exists, not its floor plan. Section 5 is the mechanism.

## 2. Baseline at writing (2026-06-12)

The state when this doc was written, kept as the starting point the tiers were planned from. Rows that have changed since say so; the others still hold.

| Area | Fact | Source |
|------|------|--------|
| Movement | At writing: a straight-line beeline to `MovementTarget`, no obstacles, no collision. Since: routes from the nav mesh followed waypoint by waypoint, and agents pushed apart and out of walls and placed obstacles by the collision systems | `libs/sim/ecs/systems/MovementSystem.cpp`, `PhysicsSystem.cpp`, `CollisionSystem.cpp` |
| Agent geometry | At writing: no radius or footprint component. Since: `AgentRadius` | ECS components |
| Chunk streaming | 512 m chunks, load radius 2 (5×5 around camera), unload radius 4, async generation, ~4 MB tile data per chunk. At writing camera-anchored only. Since: anchors (every mover, by the reach of his visibility limit; `NavigationSystem::appendAnchors`) load the chunks within the larger of 128 m and that reach, and keep those within the larger of 256 m and twice it | `libs/world/world/chunk/ChunkManager.{h,cpp}` |
| Terrain data | Per-tile `Surface` (Grass/Dirt/Water/Rock/...) in flat arrays per chunk; no passability or move-cost concept yet | `libs/world/world/chunk/Chunk.h` |
| Planet graph | **The whole generated planet is resident in RAM.** `SphereGrid::neighbors(TileId)` gives hex adjacency; `PlanetSampler::sampleAt(x,y)` gives biome/elevation/water/containing-hex for any world position, loaded or not. Hex width ~4.3 km at n=256 (≈100 chunks per hex) | `libs/worldgen/worldgen/grid/SphereGrid.h`, `sampling/PlanetSampler.h` |
| Simulation LOD | None by distance: nothing runs less often for being far from the camera. Rates are per system, not per entity: vision every 5 frames, goal polls every 30 or 60 frames, AI decisions gated per colonist (an event, else every 0.5 s); [Main loop timing](./main-loop-timing.md) has the full list | ECS systems |
| Spatial index | Grid hash for **placed** entities only (trees etc.), per chunk; dynamic entities have no spatial index | `libs/assets/assets/placement/SpatialIndex.h` |
| Async | `TaskPool` (deterministic ParallelFor) + per-chunk async generation precedent | `libs/foundation/threading/TaskPool.h` |
| SDF/raycast | msdfgen is fonts only; no LOS code anywhere (vision is radius-based) | `VisionSystem.cpp` |

Everything below is new construction; the planet graph is the one tier where the data already exists.

## 3. The Tier Model

Four resolutions, from planet to footstep. An agent uses the coarsest tier that answers its current question.

```
Tier 0  GLOBAL      planet hex graph (already in RAM)     abstract parties, hours of travel
Tier 1  REGIONAL    geography mesh, computed at load                       reachability, coarse routes, loaded-but-far movement
Tier 2  LOCAL       dynamic CDT navmesh per chunk          exact paths around buildings, through doors
Tier 3  MOVEMENT    collision circles + velocity steering  agents take up space, doorway congestion
```

This is the "pathfinding resolution" intuition formalized, with one correction borrowed from X4's attention model: fidelity is anchored to **attention bubbles** (the settlement, the camera, active combat), not just the camera. A raid assaulting the colony runs at full fidelity even if the player is looking elsewhere. The nav mesh itself has no camera-driven fidelity: movers generate it in full, it only grows, and the camera has no effect on it ([nav-mesh-variable-resolution.md](./nav-mesh-variable-resolution.md), rules 22, 23 and 31 to 34).

### Tier 0 — Global: the planet hex graph

A raid party crossing the globe is **a record, not entities**: `{ members, position (hex + progress), route, speed }`. Route = A* over `SphereGrid::neighbors()` with edge costs from per-hex data already resident (ocean impassable, mountains/biomes weighted). Advance on a coarse clock tick using the shared game clock (Dwarf Fortress moves armies every 100 ticks; same shape). No geometry, no nav data, no ECS entities. Arrival times survive fast-forward and chunk churn because the clock is the world clock.

### Tier 1 — Regional: chunk connectivity components

Tier 1 is the geography mesh, also called the regional mesh. It is much less detailed than the real mesh (the nav mesh, Tier 2). The real mesh is a detail view of it, as game tiles are a detail view of world tiles. The two must be aligned; one is derived from the other and then filled in (rule 38 of [nav-mesh-variable-resolution.md](./nav-mesh-variable-resolution.md)). It is computed when the world loads and always exists. What it is built from, its resolution, and how alignment works are to be designed under WOR-802 (Geography mesh (Tier 1): computed at load, always exists).

The next two paragraphs do not match that description. The first is an earlier sketch, components that fall out of the real mesh's faces, built lazily for loaded chunks; the second is what was built. Neither is a mesh computed at load. WOR-802 replaces both.

**Earlier sketch.**

Per loaded chunk: walkable **connected components** with only perimeter connectivity stored, linked to neighbor chunks (the Factorio FFF-317 abstraction, which transfers directly to vector chunks; components fall out of the Tier 2 triangulation's faces nearly free). Components carry an ID per traversal class (collision radius × door permissions). Uses:

- **O(1) reachability**: "is the target in my component?" before any path query. The Dwarf Fortress trick; kills nearly all failed searches, which are the expensive ones.
- **Heuristic**: Reverse Resumable A* over the component graph guides Tier 2 searches (Factorio's exact design).
- **Loaded-but-far movement**: agents outside attention bubbles follow component-graph routes with simple integration, skipping the navmesh and avoidance entirely. Movement LOD, not just data LOD.

Built lazily for loaded chunks; invalidated locally by construction edits (the same dirty-section pattern construction already needs).

**As built at P3 (shipped 2026-06-23); the forests, the bottleneck tree and the whole-mesh rebuild described here are since deleted, and component labels and route requests (below) replace them. Not the geography mesh.** The navmesh landed as one merged loaded-area arrangement (P2's decision), not per-chunk meshes, so the regional layer is simpler than the FFF-317 sketch above: components are a flood-fill over the merged triangle adjacency, with no cross-chunk perimeter stitching. Two forests are built per mesh, a *truth* forest (floor plus door spans, for AI goal validity) and a *terrain* forest (walls open, only common-knowledge terrain blocks), whose disconnect is sound for every belief. Reachability is width-aware for *any* clearance, a continuous spread rather than fixed classes: a Kruskal max-spanning reconstruction tree answers `bottleneck(a,b)` (the widest disc a path admits) by LCA, and `diameter > bottleneck` (or a different component) is the sound O(log n) reject, short-circuited inside the path query so every caller skips a failed search. The RRA* heuristic runs on the terrain graph, width-unfiltered, so one resumable reverse search per goal serves every agent regardless of belief or size. Invalidation is a whole-mesh rebuild on change (ConstructionWorld exposes a single version counter), not local dirty-section patching; that refinement is deferred. See the dev log 2026-06-23-navigation-p3-regional-layer.md.

### Tier 2 — Local: dynamic constrained-Delaunay navmesh

The vector-native core, proven in production by StarCraft 2 (CDT navmesh + funnel + steering, units not in the mesh) and documented end-to-end by the jdxdev RTS series:

- **Inputs are exact vector geometry:** wall band polygons from the construction contract, water/terrain feature contours (each chunk's terrain polygon `navRings`, built once per chunk and clipped to its square, see [terrain-polygons-architecture.md](organic-terrain/terrain-polygons-architecture.md) D9), large static entity footprints (config-flagged). No rasterization anywhere.
- **One mesh that ends at the seen area.** Ground is the union of movers' visibility limits, each a polygon round where a mover stands, and the mesh ends at that union's edge with no chunk borders in it; nothing is regenerated. A change patches the triangles around it ([nav-mesh-variable-resolution.md](./nav-mesh-variable-resolution.md) 4.1, 4.3, 4.4).
- **Local dynamic updates:** construction changes retriangulate only the affected triangles (insert: retriangulate intersected set; remove: regenerate around freed constraints). This is the jdxdev design, ~50-100x faster than full rebuilds for single obstacles.
- **Query pipeline:** triangle A* (guided by the Tier 1 heuristic) with **corridor-width filtering** (Demyen & Buro: each triangle pair knows its max passable width, so one mesh serves all agent radii) → funnel with per-query portal shrinking by agent radius → line-of-sight waypoint skipping.
- **Doors are constraint edges with metadata:** traversal flag + cost per faction/permission class. A locked door is a high-cost breakable edge for raiders, which is what makes "attack the weakest point" emerge (the They Are Billions trick) without any special siege AI.
- In-house implementation, consistent with how we build (construction's geometry core shares the substrate: integer coordinates, exact predicates, the planar machinery from `libs/geometry`). Every source that built a dynamic CDT says robustness is where the months go; the mitigations are the same ones construction already commits to (integer coords make vertex merging exact, editor invariants keep inputs clean) plus a scripted smoke-test rig with time acceleration (jdxdev's hard-won advice).

### Tier 3 — Movement: agents take up space

The hard requirement, decomposed:

- **Collision circles.** Agents get a radius component and become physical in PhysicsSystem (circle-circle and circle-vs-wall-band resolution). A spatial hash for dynamic entities (the placed-entity SpatialIndex pattern, extended) serves neighbor queries. With real collision, arching and clogging at bottlenecks emerge for free (social-force literature).
- **Anticipatory avoidance:** ORCA-style velocity obstacles, implemented in-house with the published mitigations adopted from day one (Sunshine-Hill, Game AI Pro 3): substep the solver per movement tick, time horizons / cornering planes so path-following around corners doesn't deadlock, penalty scoring over hard constraints. RVO2's source (Apache-2.0) is the reference implementation to borrow concepts from, per our usual practice.
- **Congestion shaping:** small occupancy costs on mesh edges where crowds stand (planners route around loiterers, the RimWorld/TAB trick) and **door slot queues** (a door portal owns N slots; agents without a slot wait at a queue point or repath). Full space-time reservation (WHCA*) is disproportionate at our agent counts; doors are the only place it matters and slots get the same observable behavior.
- **Escape hatch, pre-approved:** if same-faction crowd collision ever produces unfixable clogs, Factorio's shipped answer (FFF-316) is to relax same-faction collision to a separation force while keeping hard collision against other factions and geometry. Documented here so the fallback is a tuning decision, not a redesign.

**Agent shape: routing vs collision (decided 2026-06-23).** Collision (this tier) uses the agent's real, possibly non-circular, per-direction footprint: a long creature is a long thin rectangle, and `FacingDirection` already drives four directional sprites. Routing (Tiers 1 and 2), though, reduces each agent to a scalar clearance equal to half its narrow cross-section, because facing follows velocity, so a creature moving down a corridor presents its narrow side and the long body trails the head along the routed path (articulated). The funnel and the reachability widths therefore consume one scalar (`AgentRadius.radiusMeters`, reread as half the narrow width), not a bounding circle (a snake's bounding circle is huge, so it could enter nothing) and not a fixed set of size classes. Per-direction collision shapes, and deriving the routing clearance from an asset's collision shape, are Tier-3 and asset work, not the router's concern. A resting agent sprawled across a path is a *dynamic* obstacle for crowd avoidance (P5). Whether mover footprints go into the mesh is no longer ruled out: the nav mesh spec puts them in (risk 7), not built yet, WOR-710 (Step 7: Mover footprints in the mesh).

### As built: runtime validity and beeline removal (2026-06-28)

The multi-region sim area this section used to describe (a square region per colonist and one for
the viewport, rebuilt whole) is gone. Its replacement is one persistent mesh patched in place,
specified in [nav-mesh-variable-resolution.md](./nav-mesh-variable-resolution.md), whose section 1
(rules 22 to 38) is the design. The nav mesh (the real mesh) is one mesh at full detail, with no
coarse or structural layer. Only movers generate it, each adding everything within its own sight
distance plus a buffer, every object movers cannot path through included and blocking. It only
grows: meshed ground never leaves it, and a chunk unloading does nothing to it. The camera has no effect on
it, and nav routes any place to any place. Whether two places are joined by meshed ground is a
component label compare (`connected`); whether a given mover can get there is a route request,
answered by ticket a tick or more later. A route search has no limit of its own: a route worker
does a fixed amount of search work a tick (`kRouteWorkPerSlice`, sized from a time target),
shared by every request, and a search carries on across ticks until it finds a goal or has
explored all the mover can reach, which answers "no known way" (spec 4.6).

The geography mesh (also called the regional mesh) is Tier 1 above. In the design it is computed
when the world loads and always exists. It is much less detailed than the real mesh, and the real
mesh is a detail view of it, as game tiles are a detail view of world tiles. The two must be
aligned; one is derived from the other and then filled in (rule 38). It is not the removed
coarse layer. As built it does not exist: WOR-802 (Geography mesh (Tier 1): computed at load,
always exists).

The correction of 2026-11-12 brought the code to that design for WOR-796 (Nav: the camera must
have no effect on the mesh), WOR-797 (Nav: movers generate the full real mesh around them, placed
obstacles included), WOR-798 (Nav: objects movers cannot path through block in the mesh instead of
being priced) and WOR-799 (Nav: one full-detail mesh with no coarse layer); their in-game pass is
what remains. Still open, as that spec's section 1.1 lists: the memory per placed obstacle, WOR-801
(Nav: cut the mesh cost of a placed obstacle from about 7.2 KB to about 1.25 KB); the geography mesh, WOR-802; goals beyond
meshed ground, WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet); player
visibility, WOR-754 (Player visibility: unexplored areas (visible, seen, unseen)), without which a
move order or placement on ground no mover has meshed does nothing or reads invalid; mover
footprints, WOR-710 (Step 7: Mover footprints in the mesh); and nav queries made outside the sim
tick waiting on the route slice as well as the patch job. Its section 10 is what was built.

**`isValidPosition`: the one runtime validity predicate.** A point is valid when it is meshed
ground a mover may stand on, not water and not inside a placed obstacle in the mesh. It reads the
mesh alone, never the placement data, so it holds after a chunk unloads. Every world-positioning path respects it (One Path Rule): dev spawn,
give-loose, colonist, teleport and the build-placement preview.

**World-data-vs-runtime boundary (load-bearing).** The 3D world/planet data exists ONLY to spawn
chunk data at initial load and during chunk generation. It is NOT read at runtime for anything
else. Runtime walkability / "is this a valid world position" is a nav-mesh query, never a
world-data query: do NOT reach back into `terrainTraversable` / `isWaterAt` to answer validity.
The nav mesh already bakes in water, obstacles, clearance, and connectivity, so it is the single
runtime authority; this keeps a clean load-time-vs-runtime separation. A point off meshed ground is not
placeable.

**Vector, not tile/grid.** Runtime positions are continuous and walkability is polygonal nav
faces. No "tile" or grid math for runtime positioning or validity. Entity occupancy and collision
will be represented IN the mesh in future (agents take space in the mesh, so the mesh encodes
"occupied" as not-walkable, consistent with risk 7's "block this tile while crafting" being
occupancy cost, not geometry); placement and drops are therefore not designed around occupancy,
they just find a valid nearby mesh position.

**Beeline removal.** All straight-line movement is gone (it superseded the section 2 baseline).
Every move is a navmesh A* path or an explicit error-snap to valid ground; the path graph already
excludes blocked faces. A durable `ecs::Colony` origin in `GameWorldState` is the last-resort
recovery snap, and on-mesh landing is guaranteed-clear (clear entities in a radius, spawn at the
walkable face center).

**`findValidPositionNear` (designed, not yet built).** The companion to `isValidPosition` and the
single "where does a dropped thing land" primitive: inventory overflow, generic ground-drops,
cancelling a foundation that still holds materials, deconstructing a station that holds contents,
and completing a craft with a full pack. Signature
`findValidPositionNear(origin, minDist = 1m, maxDist = unbounded)`: returns the nearest valid
nav-mesh position at least `minDist` from the origin. `minDist` defaults to 1m so a drop never
lands on the origin; `maxDist` is unbounded so a drop never fails to place (it self-bounds to the
active mesh, since the dropper is already on it). Homed beside `nearestPathablePoint` (a drop-spot
search, not a recovery snap). No occupancy modeling in the search — the mesh encodes occupancy as
not-walkable, so the search just returns a valid nearby position. Build it WITH its consumers, not
speculatively.

The algorithm is three tiers, not a ring sweep. `minDist` is a provable lower bound on the
answer, so once the circle of radius `minDist` touches walkable mesh, a point on it is optimal and
no search is needed:

1. `locateTriangle(origin)`.
2. One-query shortcut: test the point at `minDist` in the preferred direction with
   `isValidPosition`; if it misses, intersect the `minDist` circle with the origin face plus
   adjacency neighbors and pick a point on a walkable arc (exact, distance == `minDist`).
3. Rare fallback for an origin pinned in a sub-`minDist` pocket: best-first over mesh adjacency,
   faces popped by nearest distance, each yielding its closest point at least `minDist` out
   (closest-point-on-triangle, pushed onto the circle), first hit wins, stop when the frontier
   lower bound passes it.

Exact (continuous circle-vs-triangle, no angular stepping), near-constant in the common case,
reuses locate plus the adjacency graph plus closest-point-on-triangle. Direction pick is a
per-consumer parameter: a deterministic seed so drops fan out, plus optional aim (overflow
spreads; craft output toward the colonist).

**How the mesh is triangulated (2026-09-27).** `buildNavMesh` arranges every input ring edge
(`buildArrangement` splits at every crossing and touch and merges overlaps, so the edges form a
planar straight-line graph), then builds one constrained Delaunay triangulation of the whole
arrangement with every arrangement edge a constraint (`geometry::buildConstrainedDelaunay`: exact
incremental insertion in vertex order, cavity retriangulation to recover each constraint, a last
Lawson pass). A face is a flood fill across unconstrained edges. Each face is classified once,
from its largest triangle's centroid taken exactly in 3x-scaled integer space, against the input
rings: it must lie inside some walkable bound, the smallest solid ring containing it tags it, and
otherwise even-odd parity over the hole-capable water rings decides water or floor. Faces outside
every walkable bound are dropped.

This replaced triangulating each face on its own (half-edge face extraction, CW cycles nested as
holes, Eberly hole bridges, ear clipping), which silently dropped any face it couldn't
triangulate. Two routine inputs defeated it. Water clipped to chunk squares meets at a chunk
corner, and while the other two chunks are still generating only the diagonal pieces exist, so
the land face's hole boundary pinches at that corner and the simplicity check rejected the face.
And a land face with a few hundred tree holes made hole bridging find no visible bridge; that code
was patched once (2026-06-28) and still failed on real layouts. Either way the land around the
colonist vanished and most of the region read unwalkable. In a CDT of the arrangement a weakly
simple face or a face with any number of holes is just more constraints, and no face can drop
out. The real inputs are regression fixtures in `libs/geometry/nav/NavMeshDumpRings.test.h`.

### Tier handoffs

The two classic failure points, with the standard solutions:

- **Materialization.** When a Tier 0 party's route enters the attention/loaded boundary, spawn its members as ECS entities at the boundary on the route's approach bearing (the Mount & Blade / RimWorld caravan handoff). Reverse for de-materialization: surviving raiders that flee past the boundary collapse back into a record.
- **Time consistency.** Abstract travel runs on the shared world clock through TimeSystem speeds, so arrivals are consistent no matter what the player watches. Colonists must not walk off the meshed world: a route is planned only over ground movers have meshed, and a destination off it answers "no known way" until a mover meshes the way (WOR-776 (Nav: build a path toward a goal whose ground is not meshed yet) has movers head that way and mesh as they go). The chunk loader holds chunks around every mover, each by its own range (risk 3).

## 4. The Ray Marching Verdict

The notes said "ray marching"; evaluated seriously, it's half right, and the half that's right is worth keeping.

**What it cannot be: the router.** Following the gradient of an obstacle distance field is the classic potential-field planner, and its failure is textbook, not anecdotal: local minima wherever attraction and repulsion balance, i.e. every concave pocket. Freeform player buildings are concave pockets by design. Every shipped system that superficially looks like distance-field navigation (Supreme Commander 2, AoE4 flow fields) is actually a *goal-seeded* distance transform (a Dijkstra/fast-marching integration, i.e. a graph search) bounded by a hierarchical graph on top. They built the graph anyway. No shipped game routes by marching an obstacle SDF.

**What it should be: the local perception layer.** Distance-to-geometry queries are the natural fit for an all-vector world, and we can do them *analytically* against exact polygons (cheaper and exact, no sampled field needed, though a per-chunk sampled field via jump flooding is a GPU option if query volume ever demands it):

- Line-of-sight checks (also wanted by VisionSystem, which currently sees through walls)
- Clearance: "does a radius-r agent fit through here," spawn placement validation
- A wall-repulsion/clearance term feeding Tier 3 steering (the context-steering pattern: danger map sampled in N directions, merged with the path's preferred velocity)

So: ray marching joins the architecture as the query-and-steering substrate inside Tiers 2/3, and the routing is graphs all the way up. Claybook is the existence proof that SDF queries can carry a whole game's collision; nobody has made them carry routing.

## 5. Belief-Filtered Navigation

Most colony sims let every agent path with perfect knowledge; this game explicitly does not. Navigation therefore plans against **belief**, not truth. The trick is doing that without per-agent nav data.

### The belief model

One shared truth (the tiers above), per-agent sparse deltas:

- **Common knowledge: terrain.** Land, water, and the planet graph are stable and known to everyone. A raid "knows about your colony in a general sense": a Tier 0 location, nothing more. (Maps and exploration could later make even terrain discoverable; out of scope here.)
- **Per-agent knowledge: structures and entities.** Memory already stores known entities with last-known positions; structures (wall segments, openings) become rememberable the same way, keyed by the stable structure IDs the construction contract publishes.
- A structure has three belief states for a given agent: **unknown** (treated as absent — plans go straight through a wall never seen), **known-current** (true state), and **stale** (remembered state differs from truth: the demolished wall still blocks their plans, the new door doesn't exist for them yet).

### Mechanism

Truth navmesh constraint edges carry structure IDs. A path query takes the agent's memory as a **filter predicate**: an edge is crossable iff believed-absent or believed-passable. One mesh, one component graph; belief costs a sparse-set lookup per constrained edge touched by the search, not a second navmesh.

The plan-walk-see-replan loop is the classic **freespace assumption** from the D* family of planners: plan optimistically through unknown space, walk, let VisionSystem update memory, replan when newly-seen geometry cuts the current corridor. At colony scale a full replan on contradiction is fine; D* Lite-style incremental repair is the documented optimization if replan volume ever bites. The "colonist walks toward the new wall, sees it, re-routes" behavior is this loop working, with no special case.

Believed reachability is optimistic by construction (unknown walls don't partition space), so Tier 1's answer splits in two: *unreachable in truth* (the pathfinder's no, used for goal validity the AI is allowed to know about, e.g. its own faction's built geometry) versus *no believed route* (the agent's no). The AI treats them differently: the latter triggers search or give-up, never a silent failure.

### Hard dependency: vision occlusion

Belief is only as honest as vision. VisionSystem today is a radius query that sees through walls; under that model a raider strolling past the base learns the entire interior through the stone, and door discovery is trivial. Wall-occluded line of sight ships with belief filtering in P4, built on the same geometry substrate — specified in full in [Vision Architecture](./vision-architecture.md) (visibility polygons over the shared GeometryIndex, which also yields the unexplored area). Two consequences worth designing on purpose: **windows pass sight while blocking movement** (window placement becomes a real security tradeoff, and interiors can be scouted through them), and **player-placed blueprints stay broadcast** (the memory design's existing decision — player directives are known to all colonists — so builders never have to "discover" their own work orders).

### Search behaviors

Two nav primitives — a wall-following route along known geometry, and a vision-coverage pattern over an area — support three AI-owned behaviors:

- **Door discovery.** The agent knows a wall but no opening, so no believed route exists. Travel to the structure, wall-follow the exterior while vision sweeps it, until an opening enters memory (replan through it) or the loop closes (record "sealed, as far as I know"). A raider scouting the player's perimeter and a new colonist finding the kitchen are the same behavior.
- **Stale-target arrival.** Tasks target memory entries, so the goal is the *remembered position*, not the live entity. Arrive; vision reconciles; if the tree died or the stack was hauled away, the memory entry invalidates, the task fails gracefully, the AI re-evaluates. Arrival-then-act is already the action flow; this adds only the reconcile.
- **Last-known-position search** (hunting). Goal is the LKP from memory; on arrival without contact, run an expanding coverage pattern biased toward where the target could plausibly have gone. V1 is an expanding ring of vision-coverage waypoints. The upgrade path is **occupancy maps** (Isla): probability mass for the target diffuses across the map and the searcher sweeps the highest-mass regions — shipped as the core mechanic of Third Eye Crime, and it fits a colonist hunting a deer and a raider hunting fleeing colonists equally.

### What this does to raiders

Raiders materialize with Tier 0 knowledge only. Target selection filters to *believed* targets, so pathing straight to a storage room they've never seen is structurally impossible: their planner contains no storage room. The observable raid becomes approach → see the exterior → probe the perimeter → enter where belief says entry exists → discover the interior the hard way. Player counterplay (decoy doors, hidden stores, windowless vaults) emerges from the knowledge model instead of scripted AI.

### Costs

Per-agent belief is the sparse data Memory already holds (the scale analysis in the memory design doc covers it). Replan rate is bounded by vision events and self-extinguishes: a wall seen once never surprises that agent again. Social memory sharing (the planned Phase 2 feature) converges the colony on a shared picture without a global cheat. The genuinely new risk is readability, not performance — see risk 8.

## 6. The Raid, End to End

The signature scenario through every tier:

1. **Departure (Tier 0).** A raid spawns at a hostile site hexes or continents away: one record. A* over the hex graph (ocean impassable, mountains costly) yields a route; the party advances on the world clock for in-game hours or days. Cost: a few hundred bytes and one A* on a graph that's already in RAM.
2. **Approach (handoff).** The route crosses the settlement's attention boundary. Members materialize at the boundary along the approach bearing, clock-consistent: if the journey took 30 hours, they arrive 30 hours after departure regardless of what the player did.
3. **Staging (Tier 1 + AI).** The raid AI queries the component graph for a staging area: reachable from the arrival point, near the settlement, outside colonist sight ranges. Assignment of raiders to staging slots is the coordination mechanism (Halo's squad-to-area pattern: coordinate by assigning areas and roles, not by micromanaging paths). Movement to staging uses component-graph routes; full fidelity isn't needed yet unless watched.
4. **Scouting and target selection (Tiers 1/2 + AI).** The raid knows only what it has seen (section 5): candidate targets come from belief, not from the world. Scouts run the door-discovery behavior around the perimeter, filling the raid's shared picture. Reachability then filters per **door-permission class** over believed geometry: "reachable if we break doors" is a component query where player doors are traversable-at-cost. The weakest *known* entry point falls out of path costs, not scripted siege logic; the storage room they've never seen simply isn't a target yet.
5. **Assault (Tiers 2 + 3).** Raiders request exact paths through their believed layout: triangle A*, corridor-width filtered for each body size, funneled through door portals. Door slot queues form the siege crush at the breach; collision circles make the crowd physical; a colonist holding the doorway is actually holding a doorway. Surprises mid-assault (an interior wall they didn't know, a corridor the player just sealed) hit the same discovery-replan loop as everyone else.
6. **Rout (handoff, reversed).** Survivors fleeing past the boundary collapse back into a Tier 0 record and route home.

The AI/pathfinding interface this implies is thin: `connected(a, b)` (a label compare, no search) to filter candidates, `requestRoute(start, goals, radius, belief)` for the nearest of several goals by walk, answered by ticket on a later tick (`takeRouteAnswer`, `cancelRoute`, `routeTicketLive`), `generation()` and `connectivityEpoch()` for replan triggers and remembered verdicts, and position queries on the mesh (see [Nav mesh at variable resolution](./nav-mesh-variable-resolution.md) 4.6). Squad coordination lives in the AI on top of area assignment; pathfinding stays a service.

## 7. Phasing

Each phase lands independently testable and useful.

**P1 — Agents become physical.** Radius component, dynamic-entity spatial hash, circle collision in PhysicsSystem, basic separation. No pathfinding required; colonists stop standing inside each other. Establishes the agent-geometry substrate everything else needs.

**P2 — Local navmesh, static world.** Per-chunk CDT from terrain contours (water) + flagged entity footprints; triangle A* + corridor width + funnel + LOS skips; MovementSystem follows waypoints. Water blocks movement for the first time. Construction isn't required (its obstacle contract slots in later).

**P3 — Regional layer (shipped 2026-06-23; the forests and bottleneck tree are since deleted, see the nav mesh spec).** Width-aware reachability over the merged navmesh: connected components plus a Kruskal max-spanning reconstruction tree giving `bottleneck(a,b)` by LCA, in a truth forest (goal validity) and a terrain forest (the belief-sound reject). The cheap reject (`diameter > bottleneck`, or a different component) short-circuits the triangle A*, so failed-path costs vanish. Corridor-width filtering (Demyen and Buro) was un-deferred so the one mesh serves any agent size, and an RRA* heuristic on the terrain graph guides successful searches (one resumable reverse search per goal serves every agent). Reframed from the original sketch: one merged mesh (no per-chunk components), variable-clearance bottleneck reachability (not fixed traversal-class IDs), and articulated narrow-cross-section routing (see Tier 3). Deferred: belief-dependent wall clearance (construction guarantees >= 0.7 m gaps), local dirty-section mesh invalidation, and the NavOverlay/HTTP stats surface. See the dev log 2026-06-23-navigation-p3-regional-layer.md.

**P4 — Dynamic world and belief.** Construction obstacle/portal publication consumed (local retriangulation, nav versioning), door constraint edges with permission costs, replanning. Structure edges carry IDs and path queries take the agent's memory as the belief filter; the discovery-replan loop and the two search primitives (wall-follow, vision-coverage pattern) land here. Wall-occluded vision (belief's other half) is its own epic ([Vision Architecture](./vision-architecture.md)) and must land with or before this phase. This is the gate for walls shipping as gameplay, and the milestone where "nobody magically knows your floor plan" becomes true.

**P5 — Crowds.** Velocity-obstacle avoidance with mitigations, occupancy costs, door slot queues. The doorway-congestion acceptance tests live here, plus the scenario regression rig.

**P6 — Global tier and raids.** Hex-graph A*, abstract party records on the world clock, attention bubbles, materialization handoffs, the reachability-per-permission-class query, raider belief seeding (Tier 0 knowledge only) and perimeter scouting built on the P4 search primitives. Unlocks the raid scenario.

## 8. Risks & Open Questions

1. **CDT robustness is the long pole.** Every implementer says so. Mitigations: shared integer-coordinate substrate with construction, constrained inputs (editor invariants, simplified terrain contours), exhaustive geometry tests, and a time-accelerated smoke-test rig before any content depends on it.
2. **Velocity-obstacle deadlocks at doorways** are documented failure modes, not surprises. Mitigations adopted up front (substepping, cornering planes, door queues); FFF-316 same-faction relaxation pre-approved as the fallback.
3. **Camera-only chunk loading** was a correctness hole (colonists could walk into the void) that pathfinding made load-bearing. Resolved by chunk anchoring: every mover keeps the chunks within the larger of 128 m and its meshing range loaded (`ChunkManager::update` with `NavigationSystem::appendAnchors`).
4. **Off-screen simulation LOD is bigger than pathfinding.** Needs/AI/vision currently tick for every entity everywhere. Tier 0 sidesteps it for raids (records, not entities), but a general entity-simulation LOD (and the restraint lesson from Kenshi: don't over-simulate the far world) deserves its own future spec.
5. **Time-scale interactions.** 10x game speed multiplies avoidance substeps and replan rates; budget Tier 3 for worst-case speed, or clamp avoidance fidelity at high speeds (movement correctness over crowd aesthetics when fast-forwarding).
6. **Memory/perf envelopes** for per-chunk meshes and component graphs need numbers during P2/P3; the perf tooling from the render overhaul applies.
7. **Mesh-vs-agents contract:** dynamic agents are never navmesh obstacles (SC2 rule); they exist only in Tier 3. Violating this (e.g., "block this tile while crafting") gets modeled as occupancy cost, not geometry. Reversed by [nav-mesh-variable-resolution.md](./nav-mesh-variable-resolution.md) (4.3.3, rule 11): mover footprints go in the mesh, patched as they move. Not built (step 7).
8. **Belief readability.** A colonist confidently walking toward a wall they don't know about is *correct* behavior that looks like a pathfinding bug. Mitigation is designed: the colonist info panel's current-task line surfaces navigation states with status colors ("Going to" / "Re-routing" / "Searching for" as mild warning / "Can't find a way to" as blocked — vocabulary in the [Memory System](../design/game-systems/colonists/memory.md) UI section), so the movement layer must expose its state per agent. Plus the planned memory debug overlay. Replan churn under belief is bounded by vision events and self-extinguishing, but compounds with 10x speed; coalesce discovery events per agent per tick.
9. **Wildlife belief policy.** Do animals get belief too, or path on truth? Full belief for a deer is over-modeling; truth-pathing animals are omniscient prey. Open design question for the wildlife work; the filter mechanism supports either, per agent class.

## 9. Prior Art (Research Summary)

| Source | Takeaway |
|--------|----------|
| **StarCraft 2** navigation (Anhalt, GDC 2011) | Production CDT navmesh + funnel + steering; units are not in the mesh. The Tier 2/3 split. |
| **jdxdev RTS series** (parts 1-3 + flowfields postmortem) | The full vector-native playbook: dynamic CDT updates, fixed-point precision lessons, Demyen corridor widths for variable radii, boids corridor pathologies, smoke-test rig. ([part 2](https://www.jdxdev.com/blog/2021/07/06/rts-pathfinding-2-dynamic-navmesh-with-constrained-delaunay-triangles/), [part 3](https://www.jdxdev.com/blog/2021/12/07/rts-pathfinding-3-variable-agent-size-smoke-tests-navmesh-fixes/)) |
| **Demyen & Buro**, triangulation pathfinding thesis | Corridor-width filtering: one mesh, all agent sizes. ([thesis](https://skatgame.net/mburo/ps/thesis_demyen_2006.pdf)) |
| **Factorio FFF-317 / FFF-316** | Chunk-component abstraction + Reverse Resumable A* as heuristic; and the shipped retreat from same-faction collision. ([FFF-317](https://factorio.com/blog/post/fff-317), [FFF-316](https://factorio.com/blog/post/fff-316)) |
| **Dwarf Fortress** (Adams interview) | Connected-component reachability cache kills failed searches; armies as abstract records on a 100-tick clock. ([interview](https://stackoverflow.blog/2021/12/31/700000-lines-of-code-20-years-and-one-developer-how-dwarf-fortress-is-built/)) |
| **AoE4** (Cheng, GDC 2022) | Portal-graph + on-demand flow segments + steering at 1600 units; formation leader pattern; measured budgets. We need a fraction of this; informs the ceiling. ([slides](https://media.gdcvault.com/GDC+2022/Speaker+Slides/Pathing+In+Age_Cheng_Frank+2022-03-29+00.16.38.pdf)) |
| **Sunshine-Hill**, "RVO and ORCA: How They Really Work" | The honest failure catalog (flicker, cornering deadlocks) and the shippable mitigations we adopt up front. ([chapter](https://www.gameaipro.com/GameAIPro3/GameAIPro3_Chapter19_RVO_and_ORCA_How_They_Really_Work.pdf)) |
| **Silver 2005**, Cooperative Pathfinding | WHCA* space-time reservations; we scope it down to door slot queues. ([paper](https://cdn.aaai.org/ojs/18726/18726-52-22369-1-10-20210928.pdf)) |
| **X4: Foundations** attention model | Fidelity bubbles anchored to what matters, not the camera. |
| **Mount & Blade / RimWorld caravans** | The abstract-record ↔ materialized-encounter handoff at the boundary, clock-consistent. |
| **Kenshi** (Hunt interviews) | The restraint lesson: the far world is flags and spawn modulation, not simulation; "the more dynamic a game gets, the more unstable it becomes." |
| **Potential fields literature** (Choset et al.) | The proof that obstacle-gradient routing local-minima traps in concave geometry; why ray marching can't route. ([notes](https://www.cs.columbia.edu/~allen/F17/NOTES/potentialfield.pdf)) |
| **Claybook** (Aaltonen, GDC 2018) | SDF queries can carry collision/LOS for a whole game; the legitimate role for the ray-marching hunch. ([slides](https://media.gdcvault.com/gdc2018/presentations/Aaltonen_Sebastian_GPU_Based_Clay.pdf)) |
| **Context steering** (Fray) | The proven shape of ray-cast steering inputs. ([chapter](https://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter18_Context_Steering_Behavior-Driven_Steering_at_the_Macro_Scale.pdf)) |
| **Halo 2** squad AI (Isla) | Coordinate raids by assigning areas/roles, not paths. ([proceeding](https://www.gamedeveloper.com/programming/gdc-2005-proceeding-handling-complexity-in-the-i-halo-2-i-ai)) |
| **D* Lite / freespace assumption** (Koenig & Likhachev, AAAI 2002; Stentz's D*) | The canonical plan-optimistically-through-unknown, replan-on-observation family; our belief loop, with incremental repair as the optimization. |
| **Occupancy maps / Third Eye Crime** (Isla, GDC 2006 + AIIDE 2013) | Probabilistic last-known-position search, shipped as a whole game's core mechanic; the upgrade path for hunting and raider search behavior. |
| **Frontier exploration** (Yamauchi 1997) | Explore by walking the boundary of known space; the formal shape of the perimeter-probe / door-discovery behavior. |
| **They Are Billions** | Breakable barriers as high-cost edges → attack-the-weakest-point emerges from path costs. |
| **UE5 World Partition / Recast tiles** | Streamed, independently-rebuilt nav tiles with stitched borders; the pattern behind per-chunk meshes. ([docs](https://dev.epicgames.com/documentation/unreal-engine/world-partitioned-navigation-mesh)) |

## 10. Related Documents

- [Building & Construction Architecture](./building-construction-architecture.md) — the obstacle/portal contract (D5) this spec consumes
- [Building & Construction design spec](../design/game-systems/world/building-construction.md) — pathing clearance constraints
- [Chunk Management System](./chunk-management-system.md) — streaming this spec tiles against
- [World Generation Implementation](./world-generation-implementation.md) — SphereGrid, the Tier 0 graph
- [3D to 2D Sampling](./3d-to-2d-sampling.md) — PlanetSampler, hex↔world mapping
- [AI Behavior](../design/game-systems/colonists/ai-behavior.md) — the decision system pathfinding serves
- [Memory System](../design/game-systems/colonists/memory.md) — the belief source for section 5
- [Vision Architecture](./vision-architecture.md) — belief's write path; shares the GeometryIndex perception substrate
