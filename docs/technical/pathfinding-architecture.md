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
3. **Infinite world.** Chunks stream around the camera (512 m, load radius 2 today); nav data can only exist for loaded space. Yet agents must traverse unloaded space: the signature scenario is a raid party crossing the globe to the settlement.
4. **The raid scenario end-to-end:** route from anywhere on the planet → arrive near the settlement → coordinate an approach as a group → pick targets → navigate interiors through doors. Section 6 traces it through every tier.
5. **Colony-sim scale.** Dozens to low hundreds of exact agents near the action; arbitrarily many abstract parties far away.
6. **Plays well with the engine:** TimeSystem speed multipliers (up to 10x), determinism-friendly (multiplayer future), async-friendly (TaskPool exists), and the construction contract (walls publish exact band polygons, openings publish portals, shared clearance constant, nav version counter).
7. **No omniscience.** The memory system is a core differentiator: an agent cannot path to, or through, what it has never seen ([Memory System](../design/game-systems/colonists/memory.md)). Navigation runs against the agent's *believed* world: unknown doors don't exist for them, unknown walls don't block their plans until seen, remembered things may be gone on arrival, and raiders know the colony exists, not its floor plan. Section 5 is the mechanism.

## 2. Current State (Baseline)

| Area | Fact | Source |
|------|------|--------|
| Movement | Straight-line beeline to `MovementTarget`; no obstacles, no collision; a colonist will happily walk into an unloaded chunk and keep going | `libs/engine/ecs/systems/MovementSystem.cpp`, `PhysicsSystem.cpp` |
| Agent geometry | No radius/footprint component exists anywhere | ECS components |
| Chunk streaming | 512 m chunks, load radius 2 (5×5 around camera), unload radius 4, async generation, ~4 MB tile data per chunk; **camera-anchored only**, nothing keeps chunks loaded around colonists | `libs/engine/world/chunk/ChunkManager.{h,cpp}` |
| Terrain data | Per-tile `Surface` (Grass/Dirt/Water/Rock/...) in flat arrays per chunk; no passability or move-cost concept yet | `libs/engine/world/chunk/Chunk.h` |
| Planet graph | **The whole generated planet is resident in RAM.** `SphereGrid::neighbors(TileId)` gives hex adjacency; `PlanetSampler::sampleAt(x,y)` gives biome/elevation/water/containing-hex for any world position, loaded or not. Hex width ~4.3 km at n=256 (≈100 chunks per hex) | `libs/world/worldgen/grid/SphereGrid.h`, `sampling/PlanetSampler.h` |
| Simulation LOD | None. Every system updates every entity every frame regardless of distance | ECS systems |
| Spatial index | Grid hash for **placed** entities only (trees etc.), per chunk; dynamic entities have no spatial index | `libs/engine/assets/placement/SpatialIndex.h` |
| Async | `TaskPool` (deterministic ParallelFor) + per-chunk async generation precedent | `libs/foundation/threading/TaskPool.h` |
| SDF/raycast | msdfgen is fonts only; no LOS code anywhere (vision is radius-based) | `VisionSystem.cpp` |

Everything below is new construction; the planet graph is the one tier where the data already exists.

## 3. The Tier Model

Four resolutions, from planet to footstep. An agent uses the coarsest tier that answers its current question.

```
Tier 0  GLOBAL      planet hex graph (already in RAM)     abstract parties, hours of travel
Tier 1  REGIONAL    chunk connectivity components          reachability, coarse routes, loaded-but-far movement
Tier 2  LOCAL       dynamic CDT navmesh per chunk          exact paths around buildings, through doors
Tier 3  MOVEMENT    collision circles + velocity steering  agents take up space, doorway congestion
```

This is the "pathfinding resolution" intuition formalized, with one correction borrowed from X4's attention model: fidelity is anchored to **attention bubbles** (the settlement, the camera, active combat), not just the camera. A raid assaulting the colony runs at full fidelity even if the player is looking elsewhere.

### Tier 0 — Global: the planet hex graph

A raid party crossing the globe is **a record, not entities**: `{ members, position (hex + progress), route, speed }`. Route = A* over `SphereGrid::neighbors()` with edge costs from per-hex data already resident (ocean impassable, mountains/biomes weighted). Advance on a coarse clock tick using the shared game clock (Dwarf Fortress moves armies every 100 ticks; same shape). No geometry, no nav data, no ECS entities. Arrival times survive fast-forward and chunk churn because the clock is the world clock.

### Tier 1 — Regional: chunk connectivity components

Per loaded chunk: walkable **connected components** with only perimeter connectivity stored, linked to neighbor chunks (the Factorio FFF-317 abstraction, which transfers directly to vector chunks; components fall out of the Tier 2 triangulation's faces nearly free). Components carry an ID per traversal class (collision radius × door permissions). Uses:

- **O(1) reachability**: "is the target in my component?" before any path query. The Dwarf Fortress trick; kills nearly all failed searches, which are the expensive ones.
- **Heuristic**: Reverse Resumable A* over the component graph guides Tier 2 searches (Factorio's exact design).
- **Loaded-but-far movement**: agents outside attention bubbles follow component-graph routes with simple integration, skipping the navmesh and avoidance entirely. Movement LOD, not just data LOD.

Built lazily for loaded chunks; invalidated locally by construction edits (the same dirty-section pattern construction already needs).

**As built (P3, shipped 2026-06-23).** The navmesh landed as one merged loaded-area arrangement (P2's decision), not per-chunk meshes, so the regional layer is simpler than the FFF-317 sketch above: components are a flood-fill over the merged triangle adjacency, with no cross-chunk perimeter stitching. Two forests are built per mesh, a *truth* forest (floor plus door spans, for AI goal validity) and a *terrain* forest (walls open, only common-knowledge terrain blocks), whose disconnect is sound for every belief. Reachability is width-aware for *any* clearance, a continuous spread rather than fixed classes: a Kruskal max-spanning reconstruction tree answers `bottleneck(a,b)` (the widest disc a path admits) by LCA, and `diameter > bottleneck` (or a different component) is the sound O(log n) reject, short-circuited inside the path query so every caller skips a failed search. The RRA* heuristic runs on the terrain graph, width-unfiltered, so one resumable reverse search per goal serves every agent regardless of belief or size. Invalidation is a whole-mesh rebuild on change (ConstructionWorld exposes a single version counter), not local dirty-section patching; that refinement is deferred. See the dev log 2026-06-23-navigation-p3-regional-layer.md.

### Tier 2 — Local: dynamic constrained-Delaunay navmesh

The vector-native core, proven in production by StarCraft 2 (CDT navmesh + funnel + steering, units not in the mesh) and documented end-to-end by the jdxdev RTS series:

- **Inputs are exact vector geometry:** wall band polygons from the construction contract, water/terrain feature contours (extracted once per chunk from tile data via marching squares, then simplified), large static entity footprints (config-flagged). No rasterization anywhere.
- **Tiled per chunk** with chunk borders as constrained edges, so meshes stitch like Recast/UE5 streamed tiles and regenerate independently.
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

**Agent shape: routing vs collision (decided 2026-06-23).** Collision (this tier) uses the agent's real, possibly non-circular, per-direction footprint: a long creature is a long thin rectangle, and `FacingDirection` already drives four directional sprites. Routing (Tiers 1 and 2), though, reduces each agent to a scalar clearance equal to half its narrow cross-section, because facing follows velocity, so a creature moving down a corridor presents its narrow side and the long body trails the head along the routed path (articulated). The funnel and the reachability widths therefore consume one scalar (`AgentRadius.radiusMeters`, reread as half the narrow width), not a bounding circle (a snake's bounding circle is huge, so it could enter nothing) and not a fixed set of size classes. Per-direction collision shapes, and deriving the routing clearance from an asset's collision shape, are Tier-3 and asset work, not the router's concern. A resting agent sprawled across a path is a *dynamic* obstacle for crowd avoidance (P5), never baked into the static mesh.

### Tier handoffs

The two classic failure points, with the standard solutions:

- **Materialization.** When a Tier 0 party's route enters the attention/loaded boundary, spawn its members as ECS entities at the boundary on the route's approach bearing (the Mount & Blade / RimWorld caravan handoff). Reverse for de-materialization: surviving raiders that flee past the boundary collapse back into a record.
- **Time consistency.** Abstract travel runs on the shared world clock through TimeSystem speeds, so arrivals are consistent no matter what the player watches. Colonists must not walk off the loaded world: destinations are validated against loaded space (Tier 1 query), and the chunk loader gains anchor points beyond the camera (settlement, active agents) — today it is camera-only, which is a latent bug independent of pathfinding.

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

Belief is only as honest as vision. VisionSystem today is a radius query that sees through walls; under that model a raider strolling past the base learns the entire interior through the stone, and door discovery is trivial. Wall-occluded line of sight ships with belief filtering in P4, built on the same geometry substrate — specified in full in [Vision Architecture](./vision-architecture.md) (visibility polygons over the shared GeometryIndex, which also yields the fog of war). Two consequences worth designing on purpose: **windows pass sight while blocking movement** (window placement becomes a real security tradeoff, and interiors can be scouted through them), and **player-placed blueprints stay broadcast** (the memory design's existing decision — player directives are known to all colonists — so builders never have to "discover" their own work orders).

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

The AI/pathfinding interface this implies is thin: `isReachable(from, to, traversalClass)` (Tier 1, O(1)), `requestPath(...)` (async via TaskPool, answered in a frame or two), `navVersion()` for replan triggers, and spatial target queries filtered by reachability. Squad coordination lives in the AI on top of area assignment; pathfinding stays a service.

## 7. Phasing

Each phase lands independently testable and useful.

**P1 — Agents become physical.** Radius component, dynamic-entity spatial hash, circle collision in PhysicsSystem, basic separation. No pathfinding required; colonists stop standing inside each other. Establishes the agent-geometry substrate everything else needs.

**P2 — Local navmesh, static world.** Per-chunk CDT from terrain contours (water) + flagged entity footprints; triangle A* + corridor width + funnel + LOS skips; MovementSystem follows waypoints. Water blocks movement for the first time. Construction isn't required (its obstacle contract slots in later).

**P3 — Regional layer (shipped 2026-06-23).** Width-aware reachability over the merged navmesh: connected components plus a Kruskal max-spanning reconstruction tree giving `bottleneck(a,b)` by LCA, in a truth forest (goal validity) and a terrain forest (the belief-sound reject). The cheap reject (`diameter > bottleneck`, or a different component) short-circuits the triangle A*, so failed-path costs vanish. Corridor-width filtering (Demyen and Buro) was un-deferred so the one mesh serves any agent size, and an RRA* heuristic on the terrain graph guides successful searches (one resumable reverse search per goal serves every agent). Reframed from the original sketch: one merged mesh (no per-chunk components), variable-clearance bottleneck reachability (not fixed traversal-class IDs), and articulated narrow-cross-section routing (see Tier 3). Deferred: belief-dependent wall clearance (construction guarantees >= 0.7 m gaps), local dirty-section mesh invalidation, and the NavOverlay/HTTP stats surface. See the dev log 2026-06-23-navigation-p3-regional-layer.md.

**P4 — Dynamic world and belief.** Construction obstacle/portal publication consumed (local retriangulation, nav versioning), door constraint edges with permission costs, replanning. Structure edges carry IDs and path queries take the agent's memory as the belief filter; the discovery-replan loop and the two search primitives (wall-follow, vision-coverage pattern) land here. Wall-occluded vision (belief's other half) is its own epic ([Vision Architecture](./vision-architecture.md)) and must land with or before this phase. This is the gate for walls shipping as gameplay, and the milestone where "nobody magically knows your floor plan" becomes true.

**P5 — Crowds.** Velocity-obstacle avoidance with mitigations, occupancy costs, door slot queues. The doorway-congestion acceptance tests live here, plus the scenario regression rig.

**P6 — Global tier and raids.** Hex-graph A*, abstract party records on the world clock, attention bubbles, materialization handoffs, the reachability-per-permission-class query, raider belief seeding (Tier 0 knowledge only) and perimeter scouting built on the P4 search primitives. Unlocks the raid scenario.

## 8. Risks & Open Questions

1. **CDT robustness is the long pole.** Every implementer says so. Mitigations: shared integer-coordinate substrate with construction, constrained inputs (editor invariants, simplified terrain contours), exhaustive geometry tests, and a time-accelerated smoke-test rig before any content depends on it.
2. **Velocity-obstacle deadlocks at doorways** are documented failure modes, not surprises. Mitigations adopted up front (substepping, cornering planes, door queues); FFF-316 same-faction relaxation pre-approved as the fallback.
3. **Camera-only chunk loading** is a correctness hole today (colonists can walk into the void) and pathfinding makes it load-bearing. Chunk anchoring (settlement + active agents) is small but touches ChunkManager policy; schedule it with P2.
4. **Off-screen simulation LOD is bigger than pathfinding.** Needs/AI/vision currently tick for every entity everywhere. Tier 0 sidesteps it for raids (records, not entities), but a general entity-simulation LOD (and the restraint lesson from Kenshi: don't over-simulate the far world) deserves its own future spec.
5. **Time-scale interactions.** 10x game speed multiplies avoidance substeps and replan rates; budget Tier 3 for worst-case speed, or clamp avoidance fidelity at high speeds (movement correctness over crowd aesthetics when fast-forwarding).
6. **Memory/perf envelopes** for per-chunk meshes and component graphs need numbers during P2/P3; the perf tooling from the render overhaul applies.
7. **Mesh-vs-agents contract:** dynamic agents are never navmesh obstacles (SC2 rule); they exist only in Tier 3. Violating this (e.g., "block this tile while crafting") gets modeled as occupancy cost, not geometry.
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
