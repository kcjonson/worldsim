# 2026-06-28 - Navmesh zero-walkable fix, multi-region nav, and reliable craft provisioning

## Summary

One long session on `debug/navmesh-zero-walkable` (PR #240) that started as a formal debug
session for a navmesh that classified every face as blocked, and grew into a rework of how the
sim area is meshed, how a runtime position is validated, and how a queued craft gets provisioned
and finishes. The arc: find and fix the zero-walkable root cause, replace the single
camera-tracked navmesh with a multi-region area that follows colonists plus the viewport, make
the nav mesh the single runtime walkability authority, then make crafting work like construction
(haul materials into the station's store) and chase down every reliability blocker that kept a
queued axe from completing.

The decisions matter more than the diffs here, so this entry leads with them. Engine + geometry
suites green throughout (840 then 850 tests by the end); in-game verified end-to-end (clear
spawn, navigate, craft a Primitive Axe from both cut sources and loose ground stock, with
pan/zoom no longer freezing the colonist).

## Details

### Navmesh zero-walkable: the formal debug session and root cause

Symptom: on the quickstart world the nav mesh built fine but tagged all 1848 triangles as
common-knowledge-terrain blockers. `walkable=0 floor=0`, so no colonist could path anywhere and
Bob sat in "Waiting for the area to settle" forever. The input was correct (`walkableBorders=1`
plus 638 blocked rings); the output was 100% wrong.

Ran the `/debug` protocol from `docs/technical/debugging-strategy.md`. Three parallel scouts
(data-flow, coordinate-math, edge-cases), all grounded in `NavMeshRealRings.test.h` (the real
water/border rings dumped from a live quickstart build, not hand-built rings, because hand-built
cases were exactly what gave a prior fix false confidence). Nine raw hypotheses, four after dedup.

- **H1 (water ring's even-odd interior is the land):** 3/3 scout consensus, then INVALIDATED by a
  build-free data check. The water ring is a correct thin CCW loop (~6% of area); every sampled
  grass point tests outside it, only the confluence center is inside. The classification would
  correctly leave grass as floor. Not a water mistag.
- **H3 (land-face-with-holes triangulation failure):** CONFIRMED. On a clean baseline the
  real-confluence test reproduced the exact in-game numbers (`floor=0, terrain=1848/1848`). The
  big grass face carries the river plus ~637 tree colliders as holes; `mergeHoles`' Eberly
  +x-ray hole-bridge picks a target that, once several holes have merged, sits off the +x ray, so
  the slanted bridge slices through a hole and yields a self-crossing, non-simple merged loop. The
  land face then triangulates to zero triangles, and the 1848 emitted tris are blocker interiors
  alone.

Bisection corrected the original "it's the river" instinct: border only (OK), border + water (1
non-convex hole, OK), border + 637 trees (FAILED `floor=0`). The trigger is the **many tree
holes**, not the river. Further bisection by position showed interior trees far from any border
fail just like border-band trees, so it's hole **count/density**, not edge or chunk position. A
few hundred holes anywhere overwhelm the bridge picker. Broken bridging is also ~4x slower
(bad bridges retried).

**Fix (`Triangulation.cpp`):** validate each candidate bridge (`bridgeIsClear` /
`bridgeHitsHole`) and fall back to the nearest loop vertex reachable without crossing the loop,
this hole, or any unmerged hole. In-game after the fix: `walkable=4367 floor=4367`, Bob wanders
freely. 23 NavMesh tests including 3 bisection cases; `NavMeshRealRings.test.h` is the durable
reproduction.

### Multi-region nav (Phase 1): the sim area follows colonists plus the viewport

Replaced the single camera-tracked navmesh with a per-region collection. `NavigationSystem` now
owns the area. Each tick it reads colonist `Position`s and the viewport rect (pushed by
`GameScene` as plain data, no longer driving the mesh), builds a square region per driver,
**clusters overlapping squares into merged regions**, and self-gates rebuilds off the render
clock: a region recenters only when one of its drivers nears the rect edge or its obstacle inputs
change. A stationary colonist with the camera held still triggers zero rebuilds; panning far
spawns a separate viewport region instead of dragging the colonist's region with it.

Queries dispatch by position: `requestPath` / `isReachable` / `isOnMesh` / `nearestPathable` /
`inSimArea` select the region containing the point. `requestPath` holds when the endpoints fall
in different regions or outside all regions; long-range routing across unsimulated space is
Phase 2. RRA* caches are keyed by region id plus goal triangle.

This is the **two-tier plan**. Phase 1 (shipped) is the per-driver dynamic mesh. Phase 2 (planned)
is a STATIC coarse global **geography mesh**: big impassables only (rivers as polylines, no
assets), built once per chunk and never recalculated, for long-range routing across unsimulated
space, plus skip-nav-for-stationary agents. A position outside every active region's mesh is
simply not interactively placeable yet; Phase 2 extends that.

Recovery now fires only for a colonist inside a region but off a face; a colonist no region
covers is left where he stands. That fixes the camera-pan river-snap freeze (panning used to drag
the mesh off the colonist, who was then "off-mesh" and got snapped into the river).

### Off-mesh recovery, clear landing, durable origin, beeline removal

- Off-mesh recovery is no longer suppressed by a fresh route. A colonist spawning a few cm
  off-mesh used to freeze forever; now it snaps onto the mesh whenever off-mesh.
- **All beeline movement removed.** Every move is a navmesh A* path or an explicit error-snap to
  valid ground. The path graph already excludes blocked faces, so path-or-snap is the only model.
- **Guaranteed-clear on-mesh landing:** clear entities in a radius and spawn at the walkable face
  center, so a colonist never lands inside an obstacle.
- **Durable colony origin:** an `ecs::Colony` origin stored in `GameWorldState`, used as the
  last-resort recovery snap. The origin now lives in the right place to be persisted when
  session save/load lands.

### `isValidPosition`: one runtime validity predicate, and the world-data-vs-runtime boundary

`NavigationSystem::isValidPosition` is a thin wrapper over `isOnMesh`: a point is valid IFF it
sits on a walkable nav face inside an active sim region. This is the **one canonical predicate**
every world-positioning path respects (One Path Rule). Wired into dev spawn / give-loose /
colonist / teleport and into build-placement preview (the ghost reads validity from the nav mesh
each frame, goes red off-mesh, and blocks the click). All the ad-hoc checks were deleted, along
with the "all positions valid for now" placement stub.

The load-bearing **architectural boundary** (owner, emphatic): the 3D world/planet data exists
ONLY to spawn/create chunk data at initial load and when generating new chunks. It is NOT read at
runtime for anything else. Runtime walkability / "is this a valid world position" is a nav-mesh
query, never a world-data query. Do NOT reach back into `terrainTraversable` / `isWaterAt` to
answer validity; the nav mesh already bakes in water, obstacles, clearance, and connectivity, so
it is the single runtime authority. A first attempt that read world data was reset to `fbf912e`
and rebuilt on the mesh. The **active-mesh-only** constraint (a point outside every active region
is not placeable yet, Phase 2 extends it) is spelled out in a code comment at the predicate.

This is also a **vector, not tile/grid** model: runtime positions are continuous and walkability
is polygonal nav faces. No "tile" or grid math for runtime positioning/validity. Entity
occupancy/collision will be represented IN the mesh in future (agents take space in the mesh, so
the mesh encodes "occupied" as not-walkable); placement and drops are not designed around
occupancy, they just find a valid nearby mesh position.

### `findValidPositionNear`: designed, not yet built

The companion to `isValidPosition`, designed this session and queued as a follow-up. Returns the
nearest valid nav-mesh position at least `minDist` from the origin. Signature:
`findValidPositionNear(origin, minDist = 1m, maxDist = unbounded)`. `minDist` defaults to 1m so a
drop never lands exactly on the origin; `maxDist` is unbounded so a drop never fails to place (it
self-bounds in practice to the active mesh, since the dropper is already on it). Homed beside
`NavigationSystem::nearestPathablePoint` (a drop-spot search, not a recovery snap). It becomes the
single "where does a dropped thing land" primitive for inventory overflow, generic ground-drops,
cancelling a foundation that still holds materials, deconstructing a station that holds contents,
and completing a craft with a full pack. Build it WITH its consumers, not speculatively.

Algorithm (three tiers, not a ring sweep; `minDist` is a provable lower bound on the answer, so
once the `minDist` circle touches walkable mesh a point on it is optimal):

1. `locateTriangle(origin)`.
2. One-query shortcut: test the point at `minDist` in the preferred direction with
   `isValidPosition`; if it misses, intersect the `minDist` circle with the origin face plus
   adjacency neighbors and pick a point on a walkable arc (exact, distance == `minDist`).
3. Rare fallback for an origin pinned in a sub-`minDist` pocket: best-first over mesh adjacency,
   faces popped by nearest distance, each yielding its closest point at least `minDist` out
   (closest-point-on-triangle pushed onto the circle), first hit wins, stop when the frontier
   lower bound passes it.

Exact (continuous circle-vs-triangle, no angular stepping), near-constant in the common case,
reuses locate plus the adjacency graph plus closest-point-on-triangle. Direction pick is a
per-consumer parameter (a deterministic seed so drops fan out, plus optional aim: overflow
spreads, craft output toward the colonist).

### Craft-as-construction: haul materials into the station store

The big crafting model change. Crafting now works like construction. Colonists haul each recipe
input INTO the crafting station, and the Craft action consumes the inputs FROM the station,
replacing the old model where staged materials stayed in the hauler's pack and the craft consumed
them from his inventory.

- A `CraftingSpot` spawns with an `Inventory` store alongside its `WorkQueue`, the way a build
  site holds its delivered manifest. `PlacementSystem` attaches it.
- The deposit effect's craft-station branch moves the carried material from the colonist into the
  station store (pack empties), mirroring the build-site branch, and credits the parent Craft goal
  by what physically landed.
- `applyCraftingEffect` / `startCraftAction` consume and validate against the station store, not
  the colonist. The Craft option gates on the station holding every input.
- `CraftingGoalSystem` is store-aware: materials already in the store count as delivered (only the
  shortfall is provisioned), and an input a colonist already carries resolves to a Haul (deliver
  it) rather than a fresh Harvest.
- Removed the keep-in-pack deposit path and its idempotent-credit bookkeeping (One Path Rule).

**The metered-deposit / no-leftover invariant** (owner, emphatic): a crafting station NEVER holds
more than the current recipe needs. The colonist deposits only what the recipe requires, exactly,
never more; surplus stays on him. Deposits are metered to the recipe's remaining need, the
crafting analogue of construction's `StructureBlueprint::remaining()`. This is ONE shared
mechanism with construction, not a craft-specific fork. Do NOT build handling for "extra/leftover
in a station" (return-excess, station overflow, draining surplus); that case does not exist by
design.

**The pickup-quantity-by-source rule** (intended): a HARVEST (cut/gather) takes the whole yield
up to carry capacity, overflow drops as a pile, so freshly cut resources aren't stranded at a far
harvest spot. A PICKUP from a ground pile or storage takes EXACTLY the remaining need (pile of 7,
need 2, take 2, leave 5). Deposit into a station or build site is metered to the recipe. So:
harvest = take all you can carry; pile/storage pickup = take only what's needed.

**The `giveItemToColonist` cascade:** crafted output is routed through a canonical
`giveItemToColonist` in `InventoryMass.h`: empty hand, then belt slot, then backpack, then ground
drop, weight-respecting at every step. Overflow drops a loose pile at the colonist. The old path
force-added output to the backpack, skipping hands/belt and ignoring carry weight.

### Craft reliability fixes (the "colonist won't reliably do the queued job" complaints)

Driving a queued `Recipe_AxePrimitive` to consistently provision and finish surfaced a stack of
blockers, fixed across several commits:

- **`colonyCarriesStock` cross-station bug:** it viewed every `Inventory`, so leftover material in
  a DIFFERENT station's store counted as "carried" and the input resolved to a fetch Haul that
  could never source it, stranding a second craft. Scoped it to entities with a `Colonist` tag
  (the only deliverable carriers).
- **Provision-vs-wander priority floor:** a craft-provisioning Haul/Harvest priority collapsed
  below idle Wander once its material source was far (distance penalty down to -50), so a colonist
  abandoned a half-provisioned craft and wandered. Added `servesActiveWorkOrder` plus a priority
  floor that keeps provisioning for an active work order above Wander (AI arbitration).
- **Available-haul re-resolve:** `reresolveUnsourcedChildren` only rescued `NoSource` Hauls. A
  fetch Haul born `Available` from known loose stock that was then consumed/forgotten produced no
  AI option and stranded the craft. Now re-resolve `Available` craft Hauls too: when no stock is
  fetchable but a harvestable source is known, swap to a Harvest.
- **Carry-state-first haul phase:** `startHaulAction` chose Pickup vs Deposit purely by position
  (`atSource` / `atTarget`, 0.5 m tolerance). When a loose pile sits within tolerance of the
  station, the colonist is `atSource` AND `atTarget` at once, skips Pickup, runs an empty Deposit
  ("not in inventory"), and the AI re-emits the fetch forever. Phase is now carry-state-first:
  pick up until actually carrying, deposit only once carrying. Position still decides where.
- **Harvest carry gate:** `evaluateHarvestOptions` lacked a carry check, so an over-weight or full
  colonist picked a harvest, collected 0 (the take clamps to carry weight and slots), re-evaluated,
  and picked the same harvest again. Looked like "stuck harvesting beside a tree/river." Gated:
  skip emitting the harvest when no unit of the yield fits.
- **Craft-fetch carry gate:** the symmetric fetch case, killing the infinite "fetch, collect 0,
  fetch" loop at/over the carry cap.

Result by `716e540`: 4/4 scenarios reliable 2/2 each, BOM metered exactly across 6 in-game crafts,
850 engine tests.

### Provisioning model lineage (one path)

Earlier in the session the craft-provisioning path was reworked so the global task list shows
what's actually obtainable, not a speculative chain: **lazy haul** (a harvestable input creates
only a Harvest goal; the haul that carries cut material to the station is created on harvest
completion via `createHaulForCompletedHarvest`), **availability-resolved** inputs (known stock to
Haul, known harvestable source to Harvest, neither to a `NoSource` "waiting (none found)" need that
re-resolves once a source is discovered, adding `GoalStatus::NoSource`), and **fetch from stock**
(a remembered loose pile can be hauled to the station instead of re-harvested). The legacy
`TaskType::Gather` path and the dead `WaitingForItems` / `dependsOnGoalId` / `notifyGoalCompleted`
harvest-to-haul dependency mechanism were both deleted for the One Path Rule. Food gathering
(`FulfillNeed`) is untouched.

### Supporting fixes

- **Log-stream per-connection cursor** (`DebugServer.cpp`, `LockFreeRingBuffer.h`): each dev-tools
  log viewer gets its own read cursor, so multiple connected viewers no longer starve each other's
  log feed.
- **Asset-staging mirror** (`cmake/mirror-assets.cmake`): `cmake -E copy_directory` only adds, it
  never removes, so a deleted source asset (e.g. `flora/GrassBlade`, replaced by `groundcover/Grass`)
  persisted in the build dir and kept loading. Replaced the copy with a `cmake -P` script that runs
  `robocopy /MIR` per source subdir on Windows (`rsync --delete` elsewhere), per-subdir so
  separately-staged dirs like `assets/planets/` survive. Deleted the stale staged `GrassBlade`.
- **`origin/main` merge** folded in mid-session (one conflict in `EntityRenderer.h`).
- **Multi-agent code review** at the end; findings tracked in `docs/known-issues-and-followups.md`.

### Files

Core: `libs/geometry/triangulation/Triangulation.cpp` (zero-walkable fix);
`libs/engine/ecs/systems/NavigationSystem.{h,cpp}` (multi-region area, isValidPosition, isOnMesh,
nearestPathablePoint); `libs/engine/ecs/systems/AIDecisionSystem.{h,cpp}` (recovery, priority floor,
re-resolve, provisioning); `libs/engine/ecs/systems/CraftingGoalSystem.cpp` (store-aware
provisioning, colonyCarriesStock scope); `libs/engine/ecs/systems/action/{HaulActions,HarvestActions,
CraftActions}.cpp` (carry-state phase, carry gates, metered deposit, store consume);
`libs/engine/ecs/InventoryMass.h` (giveItemToColonist cascade); `libs/engine/ecs/components/Colony.h`
(durable origin); `libs/engine/ecs/GoalTaskRegistry.{h,cpp}` (NoSource, lazy haul).
App: `apps/world-sim/GameWorldState.h`, `apps/world-sim/scenes/game/GameScene.cpp` (viewport as data),
`.../dev/DevCommandHandler.cpp` (validity gate, craft work-order verb), `.../world/placement/*`
(placement reads mesh validity), `.../world/nav/NavOverlay.cpp` (walkable/blocked coloring).
Build: `cmake/mirror-assets.cmake`. Infra: `libs/foundation/debug/DebugServer.cpp`,
`libs/foundation/debug/LockFreeRingBuffer.h`.
Tests: `NavMesh.test.cpp`, `NavMeshRealRings.test.h`, `NavigationSystem.test.cpp`,
`AIDecisionSystem.test.cpp`, `ActionSystem.test.cpp`, `CraftingGoalSystem.test.cpp`,
`InventoryMass.test.cpp`, `NavInputBuilder.test.cpp`.

## Related Documentation

- `docs/debug-navmesh-zero-walkable.md` — the formal debug session log (sessions 1-4).
- `docs/known-issues-and-followups.md` — fixed list, in-flight work, and open follow-ups
  (prefer-nearest-source provisioning, consolidate item-add sites, `findValidPositionNear`, Phase 2
  nav, navmesh build perf, session save/load).
- `docs/design/multi-colonist-crafting.md` — deferred spec; single-colonist is this session's scope.
- `docs/technical/pathfinding-architecture.md` — nav home; updated with the multi-region model,
  the runtime-validity predicate, the world-data boundary, and `findValidPositionNear`'s design.
- `docs/technical/crafting-provisioning-architecture.md` — new; craft-as-construction, the station
  store, the metered-deposit/no-leftover invariant, the pickup-by-source rule, the
  `giveItemToColonist` cascade.
- `docs/technical/building-construction-architecture.md` and
  `docs/technical/task-generation-architecture.md` — cross-reference the shared provisioning model.

## Next Steps

- Strip `[NavBuild]` / `[NavDiag]` debug instrumentation once the fixes are user-confirmed, run the
  full engine + geometry suites clean, do the final in-game end-to-end pass, then flip #240 to ready.
- Build `findValidPositionNear` with its first consumer (craft-with-full-pack drop).
- Phase 2 nav: the static coarse geography mesh for long-range routing, plus skip-nav-for-stationary.
- Prefer-nearest-source provisioning (efficiency), consolidate all item-add sites onto
  `giveItemToColonist`, and the multi-colonist crafting spec planning round.
