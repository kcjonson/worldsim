# Known issues & follow-ups — colonist nav + craft

**Branch:** `debug/navmesh-zero-walkable` (PR #240) · **As of:** 2026-06-28

Running list of bugs found and fixed, work in flight, and open follow-ups from the
navmesh + colonist-navigation + craft-provisioning work. The nav rework's design lives
in the approved plan `~/.claude/plans/ok-some-bugs-related-resilient-treasure.md`.

## Fixed this session (on #240)
- [x] **Navmesh zero walkable faces** — `mergeHoles` Eberly +x-ray hole-bridge fouled on multi-hole merges, so the land face triangulated to nothing. Validate each bridge + fall back to the nearest reachable loop vertex. (`Triangulation.cpp`)
- [x] **Off-mesh recovery freeze** — recovery snap was suppressed by a "fresh route" gate; a colonist spawning 3 cm off-mesh froze forever. Recover whenever off-mesh. (`AIDecisionSystem.cpp`)
- [x] **Loose-ground fetch haul thrash** (3 coupled defects) — deposit-leg movement never re-armed, craft-station credit double-counted, multi-unit fetch deadlocked.
- [x] **Beeline movement removed** — every move is a navmesh A* path or an explicit error-snap to valid ground; the path graph already excludes blocked faces.
- [x] **Guaranteed-clear on-mesh landing** + durable **colony origin** (`ecs::Colony` in `GameWorldState`) + last-resort snap-to-origin recovery.
- [x] **Phase 1 multi-region nav** — sim area follows colonists + viewport as clustered square regions, each with its own mesh; rebuilds self-gated on movement hysteresis (no per-frame camera push). Fixes the camera-drag freeze + the river-snap.
- [x] **Craft-fetch carry-capacity gate** — kills the infinite "hauling" loop when the colonist's pack is full.
- [x] **Crafting works like construction** — haul materials INTO the station's store; the Craft action consumes from the station. Over-weight colonists can now provision.
- [x] **Log-stream per-connection cursor** — multiple dev-tools viewers no longer starve each other's log feed.
- [x] **Asset-staging mirror** — `robocopy /MIR` / `rsync --delete` so assets deleted from source are pruned from the build dir (no more stale `GrassBlade` loading).

## Recently fixed (commits ee67dd4 / fbf912e)
- [x] **Craft never starts / infinite haul loop** — `startHaulAction` chose its Pickup/Deposit phase by POSITION (`atSource`/`atTarget`, 0.5 m tolerance). When a loose pile sits within tolerance of the station, the colonist is `atSource` AND `atTarget` at once, skips Pickup, runs an empty Deposit ("deposit failed"), and the AI re-emits the fetch forever — half-full store, craft never starts. Fixed: phase is now carry-state-first (pick up until actually carrying; deposit only once carrying). (`HaulActions.cpp` + `ActionSystem`.)
- [x] **Harvest carry-loop** ("stuck harvesting by the river") — `evaluateHarvestOptions` lacked a carry gate; an over-weight colonist looped on `Collected 0 (carry-limited)`. Fixed with a carry gate (skip when `cargoUnitsThatFit == 0`). The river was incidental — it was a loop, not nav.

## In progress (fix agent)
- [x] **Reliable end-to-end crafting** (done, commit `716e540`, 850 tests, 4/4 scenarios reliable 2/2 each) — fixed the dominant blockers: `colonyCarriesStock` counted other stations' stores as carryable so a second craft stranded (CraftingGoalSystem.cpp:83, scoped to colonist inventories); provisioning scored below idle Wander when the source was far so the colonist abandoned a half-built craft (added `servesActiveWorkOrder` + a priority floor above Wander, AIDecisionSystem.cpp); a vanished loose-stock fetch never fell back to harvest (re-resolve `Available` craft Hauls). Metered deposit (crafting analogue of construction's `remaining()`) + harvest-takes-all / pile-takes-exact-need pickup also landed; BOM metered exactly across 6 in-game crafts.
- [x] **Craft output respects hand limits** (done, commit `716e540`) — added a canonical `giveItemToColonist` cascade in `InventoryMass.h` (empty hand → belt → backpack → ground drop, weight-respecting at each step) and routed craft output through it; overflow drops a loose pile at the colonist. (One-Path follow-up below: harvest/pickup add sites still bypass it.)
- [x] **Generic `isValidPosition` + block invalid spawning/placement** (done, commit `2d882c6`: `isValidPosition` = thin `NavigationSystem::isOnMesh` wrapper reading only the nav mesh, wired into dev spawn / give / colonist / teleport + build-placement, ad-hoc checks deleted, 840 engine-tests, in-game verified) — ONE canonical predicate every world-positioning path respects (One Path Rule): valid IFF the point is on a walkable NAV FACE in an active sim region — a thin query over `NavigationSystem::isOnMesh`. The nav mesh is the runtime walkability authority; do NOT read world/chunk source data (NOT `terrainTraversable` / `isWaterAt`) — that's load-time-only data (see memory `world-data-vs-runtime-boundary`). A position outside any active mesh is not placeable for now (Phase 2 coarse global mesh extends this later; leave a code comment on the constraint). Wired into dev tools (spawn / give / teleport / move) + build-placement; each refuses + errors on invalid (dev returns an error and creates/moves nothing; preview shows invalid). Surfaced while testing crafting (the test agent kept dropping unreachable resources in the river); a first attempt that read world data was reset to `fbf912e`.

## Open follow-ups / known issues
- [ ] **Prefer-nearest-source provisioning** (efficiency, not correctness; surfaced by `716e540`) — with natural ground-scatter (SmallStone is everywhere), a craft prefers fetching far scattered stock over cutting an adjacent source, so provisioning takes long round-trips. Completion is guaranteed by the priority floor + re-resolve; it's just not efficient. A "prefer nearest source" pass in the haul/harvest resolution would tighten it.
- [ ] **Consolidate item-add sites onto `giveItemToColonist`** (One-Path) — craft output now uses the canonical hand→belt→backpack→drop cascade (`InventoryMass.h`), but harvest-yield and pickup still add via `addItem` / `addArmful` directly. Route them all through the one cascade.
- [ ] **`findValidPositionNear(origin, minDist=1m, maxDist=unbounded)` + unify ground-drops.** Companion to `isValidPosition`: returns the nearest valid nav-mesh position at least `minDist` from the origin (default 1m, so it never drops right on the origin). `maxDist` defaults to unbounded so a drop never fails to place; it self-bounds in practice to the active mesh, since the dropper is already on it. Built on the same nav authority, homed beside `NavigationSystem::nearestPathablePoint` (a drop-spot search, not a recovery snap). The single "where does a dropped thing land" primitive for inventory overflow, generic ground-drops, cancelling a foundation that still holds materials, deconstructing a crafting station that holds contents, and completing a craft with a full pack. No occupancy/collision modeling in the search: entities will take space in the nav mesh itself in future, so the mesh encodes "occupied" as not-walkable and the search just returns a valid nearby position. Unifies the per-system ad-hoc drop logic (e.g. harvest overflow-drop). Build WITH its consumers, not speculatively; the craft-with-full-pack drop is part of "crafting fully working".
  - **Algorithm (not a ring sweep).** `minDist` is a provable lower bound on the answer, so once the circle of radius `minDist` touches walkable mesh, a point on it is optimal and no search is needed. Three tiers, cheap to costly: (1) `locateTriangle(origin)`; (2) one-query shortcut: test the point at `minDist` in the preferred direction with `isValidPosition`, and if it misses, intersect the `minDist` circle with the origin face + adjacency neighbors and pick a point on a walkable arc (exact, distance == `minDist`); (3) rare fallback for an origin pinned in a sub-`minDist` pocket: best-first over mesh adjacency, faces popped by nearest distance, each yielding its closest point at least `minDist` out (closest-point-on-triangle, pushed onto the circle), first hit wins, stop when the frontier lower bound passes it. Exact (continuous circle-vs-triangle, no angular stepping), near-constant in the common case, reuses locate + the adjacency graph + closest-point-on-triangle. Direction pick is a per-consumer parameter: a deterministic seed so drops fan out, plus optional aim (overflow spreads; craft output toward the colonist).
- [ ] **Phase 2 nav** (approved plan): static **coarse geography mesh** — big impassables only (rivers as polylines, NO assets), built once per chunk, never recalculated — for **long-range routing** across unsimulated space; plus **skip-nav-for-stationary** agents.
- [ ] **AI arbitration / "reliably do the queued job"** — a colonist can be pulled toward harvesting or wandering before finishing a queued craft; a colonist who ends up in an unmeshed region won't path to nearby loose stock. The recurring "colonist doesn't reliably do the work" complaint.
- [ ] **Discovery-gating UX** — a queued craft + dropped materials doesn't reliably start a colonist until he *discovers* the materials via vision; no feedback when nothing happens. The original "colonist does nothing" theme.
- [ ] **Navmesh build perf** — `buildNavMesh` is O(n²) (~8–12 s for the full area). Phase 1's smaller per-colonist regions reduce the cost, but the algorithm itself is still O(n²).
- [ ] **Slow world-load placement** — grassland on the high-res planet places a lot of grass; loading takes minutes (not a hang).
- [ ] **Session save/load** — there is no gameplay/colony session save (only the procedural planet is persisted). The colony origin and colony state don't survive save/load. Future epic; the origin is now in the right place to be persisted.
- [ ] **Materials-in-station vs hauler's pack** — resolved (now deposits into the station). Watch for multi-colonist edge cases (one colonist staging, another crafting).

## Code review follow-ups (PR #240, multi-agent review)
10-dimension adversarial review, 54 findings raised / 45 confirmed. Severities below are the **verifier's adjusted** severities, not the finders' originals (the skeptic pass downgraded most of the memory-safety / concurrency / architecture column once dev-only and draft context was accounted for). Two raised-high findings ("Chain-interrupted 2-handed armful loses all but one unit", "Craft-output drop dangles ComponentPool refs") did NOT survive verification and are intentionally omitted.

### Must fix
- [ ] **Multi-unit craft job stalls at 1/N** — `CraftingGoalSystem.cpp:332-341` — after unit 1 consumes the station store, `deliveredAmount` is never decremented, so the same-recipe branch keeps the Craft goal at `deliveredAmount >= targetAmount`, never rebuilds Haul/Harvest children; job hangs forever (reachable via `craft n=3`). Fix: recompute `deliveredAmount` from the live station store each tick (mirror `buildChildHierarchy`'s `alreadyStaged`). **(being fixed now)**

### Should fix
File decomposition (highest-leverage, mechanical):
- [ ] **Extract AIDecisionSystem evaluators** (adj. high) — `AIDecisionSystem.cpp:44-782` — the anon-namespace evaluators (`evaluateHaul/Harvest/Build/Deconstruct/PlacePackagedOptions`, `populatePriorityBonuses`, `isOptionCurrentTask`) take all deps as params, zero private-state coupling; move to `AIOptionEvaluators.cpp/.h`, drops ~740 lines from a 1822-line file.
- [ ] **Split `buildDecisionTrace`** (adj. medium) — `AIDecisionSystem.cpp` ~953-1342 (~390 lines) — break into a need-eval pass + per-task dispatch pass + short orchestrator.
- [ ] **Extract GameScene landing-clearing cluster** (adj. low) — `GameScene.cpp:1140-1260` — `isWaterAt`/`nearestWalkable`/`prepareLandingClearing`/`clearLandingArea` (~120 lines) into a `LandingClearing` helper.
- [ ] **Split oversized test files** (adj. low/nit) — `ActionSystem.test.cpp` (2061 lines, 9 fixtures) and `AIDecisionSystem.test.cpp` (1984 lines, 46/53 in one fixture); do only alongside the source-side split.

Water-predicate duplication / boundary leak:
- [ ] **Extract one `engine::nav::tileIsNavWater(const TileData&)`** (adj. medium) — duplicated literal `tile.surface == Surface::Water || isWater(tile.primaryBiome)` at `NavInputBuilder.cpp:183`, `:524`, and `GameScene::isWaterAt` (~1140); the GameScene copy re-introduces a runtime terrain-source read that `NavigationSystem.h:200` forbids. Call the one helper from all three sites and narrow the header comment to carve out the documented pre-mesh bootstrap exception (the timing justification is real; don't do the heavy synchronous-mesh refactor).

Navmesh build perf:
- [ ] **Hole-nesting per-pair allocation** (adj. high) — `NavMesh.cpp:708-740` — `ringPoints` heap-allocates the outer ring on every (cw-cycle, walkable-face) pair, no AABB prefilter; O(faces×cycles), the likely ~10s-build culprit. Precompute each face's outer ring once before the loop, add an AABB reject.
- [ ] **Face classification step 4b** (adj. low) — `NavMesh.cpp:762-797` — O(faces×blockedRings) point-in-polygon with no spatial pruning; cache each `BlockedRing`'s AABB.

AI hot-path scans:
- [ ] **Craft-fetch scans full 10k memory map** (adj. medium) — `AIDecisionSystem.cpp:374-380` — use the existing `getEntitiesWithCapability(Carryable)` index instead of walking `knownWorldEntities`; same pattern in `evaluateHarvestOptions` (~568).
- [ ] **Stranded colonist re-runs full trace every frame** (adj. medium) — `AIDecisionSystem.cpp:1064-1066` — `CantFindWayTo` bypasses the 0.5s throttle; keep the fast first frame, then fall back to `kReEvalInterval`.

Meter-accounting cluster (all verifier-downgraded; narrow windows):
- [ ] **Haul→Harvest swap conflates two `deliveredAmount` meters** (adj. medium) — `CraftingGoalSystem.cpp:173-174` — on swap reset `deliveredAmount=0`, `targetAmount=old->availableCapacity()`.
- [ ] **Deposit metered against station's current head-job recipe** (adj. low) — `HaulActions.cpp:34-49` — meter against the Haul goal's own remaining need; self-heals via re-resolution today.
- [ ] **Haul advances to deposit when pickup collected nothing** (adj. low) — `ActionSystem.cpp:223-252` — add an `added==0` guard (verifier note: the "source vanished" path actually mints phantom items, not a wasted trip).

Concurrency:
- [ ] **Pass-2 recenter blocks main thread on in-flight future** (adj. medium) — `NavigationSystem.cpp:401` — move-assign over a live `std::async` future blocks in its destructor; guard with `if (!region.future.valid()) launchBuild(region)` like Pass 1.

Test coverage:
- [ ] **Triangulation hole-bridge fix lacks a focused test** (adj. low) — `Triangulation.cpp:281-345` — `bridgeIsClear`/`bridgeHitsHole`/brute-force fallback; gap is narrow since `RotatedConvexHolesInSquare` stress test already covers the geometry.
- [ ] **Off-mesh recovery tier 2 tested via re-implemented lambda** (adj. low) — `AIDecisionSystem.test.cpp:1934-1968` / colony-origin fallback path — add an end-to-end `update()` test (also covers the teleport-loop nit below).

### Nits
- [ ] **Stray double blank line** — `DecisionTrace.h:47-48` — gather-field deletion artifact.
- [ ] **New `m_`-prefixed members vs standard** — `AIDecisionSystem.h:167` (`m_colonyOrigin`), `GameScene.cpp:287` (`m_colony`) — name them `colonyOrigin`/`colony`.
- [ ] **`[NavBuild]` logs at INFO** — `NavigationSystem.cpp:288,296,332,444,453,479` — build-timing/lifecycle telemetry that spams on pan; lower to `LOG_DEBUG` (lower only, never raise existing DEBUG).
- [ ] **Long single functions wanting internal helpers** — `CraftingGoalSystem::update` (~223 lines), `HaulActions.cpp` `applyDepositEffect`/`startHaulAction` (~178/~168), `NavMesh.cpp` width/portal-capacity cluster — no file split needed.
- [ ] **Copy-pasted bounce-leftover block** — `HaulActions.cpp:109-113,151-155,179-183` (4th at 219-223 differs: `removed` not `leftover`) — extract a helper.
- [ ] **Near-duplicate chain-leg handoff blocks** — `ActionSystem.cpp:231-242` vs `301-312` — extract `handOffToNextLeg`.
- [ ] **Harvest item-add bypasses `giveItemToColonist` + dup two-hand dispatch** — `HarvestActions.cpp:110-115,196-234` — refactor nit only; the cargo-to-backpack divergence is intentional (not the high-severity bug it was filed as).
- [ ] **Misleading `terrainTraversable` name** — `NavMesh.h:151` — pure triangle classifier, trips grep-based boundary audits; rename or annotate.
- [ ] **`RealYConfluence_Bisect_*` investigation scaffolding** — `NavMesh.test.cpp:1049-1168` — six "run on the buggy baseline" tests asserting only `floor > 0` on subsets of the main test; collapse or remove.
- [ ] **Tests assert re-implemented lambdas / single tick** — `DevSpawnGuard` (`NavigationSystem.test.cpp:991-1024`), `OverweightColonist*` (`AIDecisionSystem.test.cpp:1757-1900`, one tick not loop-stability).
- [ ] **Dumped fixture not regenerable** — `NavMeshRealRings.test.h:1-755` — no capture script, no load-time size invariant (637 tree rings / 564-vtx water ring).
- [ ] **`representativeOutsideHoles` allocs for hole-free faces** — `NavMesh.cpp:94-104` — add `if (holes.empty()) return fallback;` first.
- [ ] **`representativeOutsideHoles` can return a point inside a hole** — `NavMesh.cpp:108-126` — silent water/floor parity mis-tag on rare degenerate geometry; assert/log on fallback.
- [ ] **NavOverlay per-edge string allocs every frame** — `NavOverlay.cpp:53-77` — debug-only overlay; ~4 allocs/triangle (finder's `ri`/`builtRegions()` evidence was partly hallucinated).
- [ ] **DevCommandHandler null-nav refuses everything with misleading message** — `DevCommandHandler.cpp:201-211` — effectively dead branch today; distinguish "navigation not wired" from "off-mesh".
- [ ] **`LockFreeRingBuffer::peekAt` torn-read race** — `LockFreeRingBuffer.h:55-64` — dev-only debug SSE path, worst case a garbled log line; add seqlock re-validation or document the caller contract. (Same race surfaced under correctness/memory-safety/concurrency; worker-thread-logging variant is temporary `[NavBuild]` instrumentation already slated for stripping.)
- [ ] **`onExit` resets ecsWorld while NavOverlay/DevCommandHandler hold raw nav pointers** — `GameScene.cpp:931-944` — unreachable after `onExit` (implicit dtor order is safe); reset them alongside `m_placementSystem` for consistency.
- [ ] **Off-mesh recovery can teleport-loop on colony origin** — `AIDecisionSystem.cpp:832-847` — guard the origin snap with `isOnMesh(*m_colonyOrigin)`; only bites when the whole local mesh is degenerate.
- [ ] **Off-mesh recovery clears in-flight craft-haul without dropping/re-crediting** — `AIDecisionSystem.cpp:826-847` — self-healing today; document the intentional clear-and-keep vs chain-interruption clear-and-drop asymmetry.

## Before flipping #240 to ready
- [ ] Strip the `[NavBuild]` / `[NavDiag]` debug instrumentation (kept per the debug protocol until the fixes are user-confirmed).
- [ ] Run the full engine + geometry suites on a clean build.
- [ ] Final in-game end-to-end pass: clear on-mesh spawn → navigate → craft an axe from both cut sources and loose materials, with pan/zoom not freezing the colonist.
