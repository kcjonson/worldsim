# sim-cli

Status: Spec. Phase 1 steps 1 to 5 (groundwork in the sim and test code) are built; steps 6 to 10,
including `apps/sim-cli` itself, and phase 2 are not.
Created: 2026-11-09
Epic: WOR-603 (takes the non-Rust parts of WOR-471: headless mode, fixed timestep in the host, one
seeded RNG path)

A headless command-line tool that runs the colonist simulation without the game: no window, no GPU,
no planet file, one fixed timestep, one seed. An engineer or an agent drives it one command at a
time or from a script, reads state back as JSON, and asks why a colonist chose what it chose. Behavior
testing today means the windowed game (a GPU, the 1.3 GiB quickstart planet, one instance at a time,
real time) poked over a dev HTTP API that answers "ok" to most refusals and cannot show a decision.

The plan was to build the tool before the [task claims](./task-claims.md) implementation and test
claims against it. That order did not hold: claims are built (tested by the `libs/sim` unit and flow
tests) and the tool is not. Phase 1 is the smallest tool that supports multi-colonist testing.

**MVP Status:** See [MVP Scope](../design/mvp-scope.md). Developer tooling; no player-facing feature.

---

## Goals and non-goals

Goals:

- Run the real sim systems, the real nav mesh, and the real asset and work configs, through the same
  setup and tick code the game runs. Nothing in sim-cli reimplements a system.
- Same script, same result twice, on one machine and one build.
- A flat synthetic map with a water edge, several colonists, trees, bushes, piles, a storage goal,
  and a craft, set up in a few lines.
- Answer "why did Ada do that": the current decision trace with every option's status and score, and
  a bounded history of decisions with their causes.
- Scenario files that run as ctest entries and fail with a readable reason.
- A JSON summary per run that a Node sweep script can aggregate (phase 2).

Non-goals:

- Rendering, UI, input, the camera, audio. sim-cli links no renderer context and no ui.
- Modelling nav mesh build latency or chunk streaming timing (see [Settling](#settling)).
- Cross-compiler or cross-platform determinism.
- ECS snapshots or save files. Replaying the script is the snapshot.
- Loading a real planet region in phase 1.
- Changing the game's behavior or per-frame cost. The refactor moves code; it does not add work to
  the game's frame.
- Fixing the sim's use of raw frame time for AI timers (a separate ticket; see
  [Known sim issues the tool exposes](#known-sim-issues-the-tool-exposes)).

---

## Who uses it and how

An engineer runs `sim-cli repl`, types `world seed=1 speed=3`, adds a pond, two colonists, and a
berry bush, sets one colonist's hunger to 5, and steps until it eats, reading `state what=trace` at
each decision. When the behavior is right the same lines go into a scenario file.

An agent does the same through stdin and stdout: one command per line in, one JSON line per reply out,
no prompts, no ANSI, no interleaved log lines (logs go to stderr). Every reply is self-describing, so
the agent never parses prose.

ctest runs `sim-cli run <file>` for each scenario file. A file is the repl's commands plus assertions;
the first failed command or assertion ends the run with a nonzero exit and prints the line, the reply,
and the values the assertion read.

---

## Architecture

### One core, two front ends

```
libs/world            SyntheticWorldSampler
libs/sim/session      SimSession, EntityFactory, SimCommands (verbs + views)
apps/world-sim        GameScene: camera, UI, input, render; DevCommandHandler as an adapter
apps/sim-cli          repl and run front ends, expression evaluator, script runner, summary
```

`SimSession` owns the simulation and its setup. The game and sim-cli both construct one and both
call its `step(dt)`, so there is one setup path and one tick path (CLAUDE.md one-path rule). There
are no mode flags inside systems and no "headless" branch anywhere in libs/sim. Differences between
the hosts are things a host does or doesn't do around the session: the game pushes a camera
viewport and registers its render system; sim-cli pushes a fixed viewport and calls `settle()`.

Registries (assets, recipes, work types, task chains, priority config, construction, and
`GoalTaskRegistry`) are process singletons, so there is one session per process at a time. A session
asserts that no other is alive, and constructing one clears `GoalTaskRegistry`. Sessions can follow
one another in a process (the game's return to menu and new game; the in-process determinism test),
so no state that outlives a session may affect the next one; see
[Process statics](#process-statics).

The game holds an untaken session only inside `GameWorldState`'s pending slot
(GameWorldState.cpp:8). The loading scene discards any pending state with `Take()` before it
constructs a session, and cancelling a load destroys the state it built, so a stale session never
trips the one-session assert.

### SimSession ownership

| Owned | Today in |
|---|---|
| `ecs::World` with the 16 sim systems registered in priority order, and their callback wiring | `GameScene::initializeECS` (GameScene.cpp:1192-1370) |
| `world::ChunkManager` built from an `IWorldSampler` the host passes in | GameLoadingScene, GameWorldState |
| `assets::PlacementExecutor`, `AsyncChunkProcessor`, the processed-chunk set | GameLoadingScene, GameScene |
| `construction::ConstructionWorld` (ownership only in phase 1) | `DrawingSystem` (GameScene.cpp:347) |
| Pending entity removals and pending spawns, and their drains | GameScene.cpp:865-907 |
| Colony origin and the landing clearing | GameScene.cpp:1380-1400, 1444-1518 |
| The packaged-spawn fan-out counter | GameScene.cpp:1295, 2110 |
| `EntityFactory` | `GameScene::spawnColonist`, `PlacementSystem::spawnEntity`, `GameScene::dropResourcePiles` |
| The session seed and the per-system seeds derived from it | `GameScene::initializeECS` (GameScene.cpp:1228-1231, `ecs::deriveSystemSeeds` in SystemSeeds.h) |
| An event ring (see [Views](#views)) and per-system timing accumulators | none |

Inputs a host sets before a step: the chunk focus (a world position the chunk manager loads around)
and the viewport rect (the nav sim area's camera contribution). Outputs a host drains after a step:
events, entity-removed notices, changed-chunk notices, and chunk mesh bakes when the host supplied a
bake stage.

Changed-chunk notices come only from the edits that release render caches today: the landing
clearing (GameScene.cpp:1506). A felled tree or a harvested bush leaves its chunk's baked mesh alone,
as it does today; making those edits re-bake is out of scope, and emitting a notice for them would
add re-bake work to the game's frame.

Code that runs between steps or inside system callbacks and needs a deferred change calls the
session: `queueRemoval(entity)` puts an entity on the removal drain, and `factory()` gives the
`EntityFactory`, whose `dropResourcePiles` is the one ground-pile path. The phase-1 structure
callbacks that stay in GameScene use exactly these: the extension merge (GameScene.cpp:434), opening
teardown (:496), and blueprint teardown (:518) call `queueRemoval`, and the salvage drop (:471) calls
`factory().dropResourcePiles`.

### pumpStreaming() and step(dt)

`pumpStreaming()` is the streaming half of a tick, with no sim time:

1. `chunkManager.update(focus)`.
2. Process new chunks: integrate finished placement tasks, hand any finished bakes to the output
   queue, launch placement for newly loaded chunks (today `processNewChunks`, GameScene.cpp:853,
   :1568).
3. Unload placement data for unloaded chunks (today `cleanupUnloadedChunks`, :856).

The loading scene calls `pumpStreaming()` each frame and drains bakes for upload, which is what it
does by hand today (GameLoadingScene.cpp:406-445); it never ticks the world. `step(dt)` is
`pumpStreaming()` followed by the rest of the game's update sequence (GameScene.cpp:831-907) with the
camera and render parts removed:

1. Push the viewport rect into `NavigationSystem::setViewportRect`. Today this happens between the
   chunk update and chunk processing (:849); the rect is plain data that nav reads only in its own
   `update`, so pushing it before `pumpStreaming()` changes nothing.
2. `pumpStreaming()`.
3. `world.update(dt)` (:859). The render system, if the host registered one, runs here at priority
   900 after every sim system.
4. Drain entity removals, emitting an entity-removed notice for each.
5. Drain pending spawns through `EntityFactory`.

The game keeps passing its real frame dt. sim-cli passes a constant `dt = 1 / hz`.

`AsyncChunkProcessor` bakes entity meshes on its worker today (AsyncChunkProcessor.h:133-143). The
bake becomes a stage the host supplies when constructing the session: the game passes it, sim-cli
passes none, and nothing else in the processor changes.

### Settling

`settle()` brings every asynchronous producer to rest:

1. `ChunkManager::finishPendingGeneration()`.
2. Launch placement for every ready chunk not yet processed, `AsyncChunkProcessor::waitAll()`, then
   integrate.
3. `NavigationSystem::finishPendingBuilds()` (NavigationSystem.cpp:727): evaluate the region rebuild
   gates against current inputs, launch what they call for, wait for every in-flight build (they run
   on `std::async`), swap each in through the same path `update()` uses to drain finished
   builds, and repeat until no gate fires. It advances nothing else; `isBuilding()` is false when it
   returns.

sim-cli calls `settle()` after every tick and after every command that changes the world. The game
never calls it. The trade is deliberate: repeatable results matter more here than modelling latency.

What settling hides, so nobody mistakes a passing scenario for coverage of it:

- The pre-mesh window. In the game the first decisions happen before any region mesh exists and
  colonists hold or recover; in sim-cli a mesh is always present.
- Mesh staleness after a change. A felled tree or a dropped pile leaves the old mesh serving queries
  for several frames in the game; in sim-cli the next tick sees the rebuilt mesh. Deferred-route
  retries keyed on mesh generation, and replans after a mid-route rebuild, behave differently.
- Chunk streaming and placement pop-in, and anything ordered by which worker finished first.
- Frame-time variance.

Those stay covered by unit tests (which can step without settling) and by the game's manual
scenarios.

### What the game keeps

GameScene keeps the camera, input, `GameUI` and toasts, `SelectionSystem`, the placement and drawing
tools (`PlacementSystem`, `DrawingSystem`), overlays, renderers, the GPU hold, player control, and the
dev server adapter. It registers `DynamicEntityRenderSystem` on `session.world()` after construction.
Each frame it updates the camera, sets the session's focus and viewport, calls `step(dt)`, then drains
events into toasts, drains removal notices (dropping selection and control of a removed colonist,
today GameScene.cpp:869-873), releases baked render caches for changed chunks, and renders.

In phase 1 the structure callbacks stay in GameScene, wired onto the session's `ActionSystem`,
`ConstructionSystem`, and `RoomDetectionSystem` against the session-owned `ConstructionWorld`
(GameScene.cpp:364-535), because they call into `DrawingSystem` (extension merges) and toast. They
reach session state only through `queueRemoval` and `factory()`, above. Phase 2 moves them into the
session with the construction commit paths.

GameLoadingScene constructs the session (after config load, with the planet's
`GeneratedWorldSampler`) and drives loading through `pumpStreaming()`. `GameWorldState` carries the
session instead of the chunk manager, placement executor, and processed set. GameScene's
no-preloaded-state fallback (GameScene.cpp:144-181) constructs a session over `MockWorldSampler`.

### EntityFactory

One place that builds every kind of entity the sim creates at runtime, used by the session's drains,
the landing, the shared commands, and the game's placement tool:

- `createColonist(position, name, skills)`: today's `GameScene::spawnColonist` (GameScene.cpp:1521-1564),
  including the entity-id-seeded attribute roll.
- `createWorldEntity(def, position)`: today's `PlacementSystem::spawnEntity` (station `WorkQueue` and
  store, storage `Inventory` and `StorageConfiguration`), minus its harvestable-pool branch (see
  below).
- `placeStatic(def, position, rotation, resources)` (new): insert a static entity into a processed
  chunk's placement index, the way world generation does, and seed its resource count (rolled from
  the def's range by position, as `spawnEntity` rolls it today, unless `resources` is given).
- `dropResourcePiles(def, position, quantity)`: today's `GameScene::dropResourcePiles` (:1726), the one
  ground-pile path.
- `createPackaged(def, position, target)`: the packaged branch of the spawn drain.

`PlacementExecutor` gains the insert, and its removal epoch becomes an edit epoch bumped by inserts as
well, so nav regions rebuild over an inserted obstacle (NavigationSystem.cpp:396-401, :484-486). An
insert into a chunk that has not finished placement is rejected: integration replaces the chunk's
whole index (`m_chunkIndices[coord] = ...`, PlacementExecutor.cpp:688), so the inserted entity would
vanish. Inserted statics live in the chunk's index like generated ones, so unloading the chunk drops
them, and reloading it regenerates only what placement rules produce. In sim-cli the focus never
moves, so nothing unloads; in the game a dev-spawned tree is lost when its chunk streams out.

### Config loading

Asset definitions and recipes already load without GL through `assets::initializeAssetSystem`
(AssetBootstrap.cpp); `asset-cli` opens GL only for its `render` command. The work configs load
through `assets::loadWorkConfig(configRoot)` (libs/assets/assets/WorkConfig.cpp, done in build step 2):
action types, task chains, work types, priority tuning with the required-tier check, construction, and
cross-validation, returning the errors rather than logging them. The loading scene calls it
(GameLoadingScene.cpp:327) and clears `GoalTaskRegistry` itself; that one sim-side line moves to
session construction. sim-cli takes `--config-root <dir>` (default: the staged
`assets/config`) so a sweep can point it at a patched copy.

### Process statics

An audit of mutable statics in libs/sim, libs/world, and libs/assets (function-local and
namespace-scope, excluding `const` and `constexpr`), with a verdict on each:

| Static | Verdict |
|---|---|
| Chain id counter | Done (build step 3): the three per-system `nextChainId` statics became one `GoalTaskRegistry` counter; WOR-46 later deleted chain ids, which nothing read. The registry's claim sequence counter, which `clear()` resets at session construction, is the one left. |
| `GoalTaskRegistry` instance | Cleared at session construction. |
| Work-config registries (`ActionTypeRegistry`, `TaskChainRegistry`, `WorkTypeRegistry`, `PriorityConfig`, `ConstructionRegistry`) and `ConfigValidator::errors` | Cleared and reloaded by `loadWorkConfig`. |
| `AssetRegistry`, `RecipeRegistry`, `GeneratorRegistry` | Loaded once per process; definitions don't change between sessions. The synthetic `Terrain_Shore` def is registered idempotently (`AssetRegistry::registerSyntheticDefinition`, called from VisionSystem.cpp:87), so ids match across sessions. |
| The edible item list | No static: `AssetRegistry::edibleItemNames()` is built with the definitions and cleared with them. |
| `GameWorldState::s_pending` (app) | Handled by the loading scene, above. |

The audit is a build step, so it is redone against the code of the day, and the in-process
two-session digest test is the check that it found everything.

### Shared command dispatcher

`SimCommands` in libs/sim/session takes a command (a verb and an ordered list of key/value params)
and a session, and returns a result: status `ok` or `rejected`, a reason code, a detail string, and a
JSON payload. Views are the same shape with a view name. JSON is built with nlohmann_json (already a
vcpkg dependency); its sorted object keys make output canonical, which the digest relies on.

The game's `DevCommandHandler` becomes an adapter. It turns an HTTP request into a command, calls the
dispatcher, returns the result as the response body, and pushes one toast from the result (info on
`ok`, warning with the reason on `rejected`). It keeps the verbs and views that are UI-only or not yet
moved: `select`, `tunable`, `landing`, `tunables`, `gpuhold`, and in phase 1 the construction verbs
(`freebuild`, `construction`, `complete`, `foundation`, `foundation-edit`, `walls`, `opening`) and the
`construction` view. The dev API's response shape stays (`{"status":"ok"|"rejected","verb":...}`, as
`foundation-edit` already answers), but a refusal now says so.

### What moves and what is deleted

| Today | Becomes | Deleted |
|---|---|---|
| `GameScene::initializeECS` registration and sim callback wiring | `SimSession` construction | the GameScene code, and its toast lambdas (events replace them) |
| `processNewChunks`, `cleanupUnloadedChunks`, both drains in `GameScene::update` | `SimSession::pumpStreaming` and `step` | the GameScene functions, `m_pendingSpawns`, `m_pendingEntityRemoval`, `PendingSpawn` |
| Loading scene's chunk polling, placement launch, and bake hand-off | `SimSession::pumpStreaming` | the loading scene's `asyncProcessor` and its loop |
| `prepareLandingClearing`, `clearLandingArea` | `SimSession::land(rawSpawn, crew)`, returning spawn points, colonist ids, and changed chunks | the GameScene functions |
| `spawnColonist`, `PlacementSystem::spawnEntity`, `dropResourcePiles` | `EntityFactory` | all three |
| The ECS-entity scans for spawned harvestables in the take-source callback (GameScene.cpp:1371-1384) and in `ConstructionSystem::isFootprintClear`, and the harvestable-pool branch of `spawnEntity` (PlacementSystem.cpp:253-270) | nothing: every harvestable def is static (below) | all three |
| `DevCommandHandler` verb bodies, `nearestColonist` and friends, `parsePoint`, `jsonEscape` | `SimCommands` | the moved bodies and helpers |
| Off-area and unreachable options relabeled `NoSource` | `OffArea` and `Unreachable` statuses | the relabels |

Done in build steps 1 to 4: the `std::optional` RNG seeds and their `random_device` fallback became
required seed arguments; the three `nextChainId` statics became the `GoalTaskRegistry` chain id
counter; `GameLoadingScene::loadWorkConfigs` became `assets::loadWorkConfig`; and the `pumpUntil` and
`pumpUntilMesh` test helpers gave way to `NavigationSystem::finishPendingBuilds()`.

---

## The synthetic world

### Sampler and map parameters

A `SyntheticWorldSampler` in libs/world implements `IWorldSampler` from a small parameter set. The
test suites already carry several hand-written samplers of this kind (`OpenLandSampler` in
AIDecisionSystem.test.cpp:3163, `LandSampler` and `WaterPatchSampler` in NavigationSystem.test.cpp,
`LakeSampler` in VisionSystem.test.cpp); suites that only need flat land or one pond move to it when
touched.

| Parameter | Default | Meaning |
|---|---|---|
| `seed` | required | the session seed; also the sampler's world seed |
| `speed` | required | game speed 0 to 3, as the dev `time` verb (multipliers 0, 1, 3, 10; TimeSystem.h:103) |
| `biome` | `TemperateGrassland` | every tile's biome |
| `water` | `none` | `none` or `pond` |
| `pond` | `40,0,15` | pond center x, y and radius in meters, when `water=pond` |
| `flora` | `none` | `none` places nothing at chunk processing; `generated` runs the biome's placement rules |
| `origin` | `0,0` | colony origin and chunk focus |
| `area` | `60` | half-extent in meters of the fixed viewport square at the origin (nav clamps it to 64) |
| `chunks` | `1` | chunk load radius around the origin (512 m chunks); 1 to 2, and 0 is rejected because nav clamps a region's half-extent to the load radius and 0 clamps it to nothing (NavigationSystem.cpp:356-362) |
| `hz` | `60` | ticks per real second; `dt = 1 / hz` |
| `clock` | `06:00` | starting time of day |

`flora=none` is a world content choice, not a system flag: chunks still go through placement and
join the processed set, with an empty spawn list, so nav, vision, and collision see every chunk as
ready.

`chunks=1` generates nine 512 m chunks on every run; that is the first thing the
[performance measurements](#performance-measurements) look at.

### Water and shore

Water comes from a pond blob (`ChunkSampleResult::pondBlobs`, ChunkSampleResult.h:133), the same
primitive the nav and vision tests use for exact water. The sampler adds the pond to every chunk its
rim can touch, as a world sampler's gather would. A half-plane lake through biome blending
(LakeShoreTestWorld.h) gives a shore bent by edge noise and interpolated across 512 m chunk corners,
which is realistic and useless for placing a colonist 4 m from the water.

Drinking needs no new code. `VisionSystem` pass 3 remembers `Terrain_Shore` points sampled along each
chunk's water rings (`terrainPolygons().shorePoints`, VisionSystem.cpp:403-436) as Drinkable. The
`shore` anchor (below) is the realized shore point nearest the origin, read from those same points, so
a script and the colonist agree on where the water is.

### Static entities and ECS entities

Two stores hold world things, and which one a thing lives in decides what sees it:

- The placement index (`PlacementExecutor`'s per-chunk `SpatialIndex`) holds static world entities.
  Nav builds obstacles only from the index (`buildInput`, NavInputBuilder.h:152, flora via
  `floraRingFor`), and `StaticRectCollisionSystem` reads only the index. Vision pass 1 reads it,
  harvest decrements and removes through it.
- ECS entities with `Appearance` hold runtime things: stations, storage, piles, packaged items,
  colonists. Vision pass 2 reads them.

A static def is a def with biome placement rules (`placement.biomes` non-empty), the same test that
puts a def into the placement spawn order (PlacementExecutor.cpp:104-106). Today that is every flora
def, the rocks, and `SmallStone`; stations, storage, and items without placement rules
(`CraftingSpot`, `BasicBox`, `Wood`, `Berry`) are not static. Every def with a harvestable capability
is static.

`spawn` routes by that rule, in both front ends: a static def goes into the index through
`EntityFactory::placeStatic`, anything else becomes an ECS entity through `createWorldEntity`. There
is one kind of tree: a spawned oak blocks nav and is collided with, as a generated one does. Because
no harvestable remains an ECS entity, the ECS scan in the harvest removal callback and the
harvestable-pool branch of `spawnEntity` are deleted. Manual scenario 01's spawned bushes, reeds, and
stones become placement-index entities; the scenario's steps don't change.

### Validity

A position is valid when it is on a walkable face of an active nav region
(`NavigationSystem::isValidPosition`; [Pathfinding Architecture](./pathfinding-architecture.md)).
Every verb that takes a position checks it after settling and rejects with `off-mesh`. The active area
is the fixed viewport square around the origin plus each colonist's own 30 m square, so positions far
from both are invalid even on dry land. Scattered placements snap each point with
`findValidPositionNear(point, 0)` as the dev verb does today and report how many landed.

---

## Command language

### Lines and tokens

One command per line: a verb, then `key=value` params separated by spaces. A value containing spaces
is double-quoted. `#` starts a comment only at the start of a line or after whitespace, so `[#14]` in
an expression is not a comment. Blank lines are ignored. Verb and parameter names are the dev API's
where the verb exists there. Unknown parameters are rejected (`bad-param`) in both front ends, so a
typo fails instead of falling back to a default.

A word (a name, a def, an enum value, a bare string in an expression) is letters, digits, `_`, and
`-`, starting with a letter. Names given with `name=` are words; `origin` and `shore` are reserved
and rejected as names.

`${name}` is replaced before parsing from `--var name=value` on the command line, so one file can run
with 2 or 50 colonists.

Positions: `x,y`, or an anchor, or an anchor plus an offset: `shore+(-4,0)`. Anchors are `origin`,
`shore`, and any entity name. Where a verb acts on the entity nearest a position (`storage`, `craft`,
`give where=storage|station`), a bare name selects that entity itself, not whatever is nearest to its
position. Durations are game time: `30m`, `2h`, `1d`, and a bare number is minutes (as `time skip`
reads it today). Colonist selectors (`colonist=`) take an entity id or a name; a name that matches
more than one live colonist is rejected as `ambiguous`.

The session keeps a name table mapping each name to an ECS id or to an index entity's key (position
and def); a name is dropped when its entity is removed. With `n` greater than 1, names get a 1-based
suffix, as `colonist` does today.

### Replies

Every command answers one JSON line:

```
{"status":"ok","verb":"colonist","ids":[14],"names":["Ada"]}
{"status":"rejected","verb":"spawn","reason":"off-mesh","detail":"41.0,0.0 is not on a walkable nav face"}
```

Reason codes are stable and documented here; the detail is prose for people. sim-cli writes nothing
else to stdout.

| Reason | When |
|---|---|
| `parse` | the line doesn't tokenize |
| `unknown-verb` | no such verb in this front end |
| `bad-param` | unknown parameter, a value that doesn't parse, or a parameter that doesn't apply (`resources=` on a def with no harvestable pool) |
| `missing-param` | a required parameter is absent |
| `no-world` | a command that needs a session before `world` |
| `world-exists` | a second `world` in one process |
| `unknown-def`, `unknown-recipe`, `unknown-need` | name not in the registries |
| `not-found` | no colonist, entity, or name matches |
| `ambiguous` | a name matches more than one colonist |
| `off-mesh` | a position fails the validity rule |
| `chunk-not-ready` | a static spawn into a chunk that has not finished placement |
| `no-station`, `no-storage` | nothing to queue at, fill, or configure |
| `name-taken` | `name=` already in use, or reserved |
| `paused` | `wait` while speed is 0 (game time cannot advance) |
| `timeout` | `wait until` reached its timeout |
| `expect-failed`, `invariant-failed` | an assertion failed |
| `not-available` | a view whose data does not exist yet |

### Verbs

Verbs marked CLI exist only in sim-cli; the game's frame loop owns time and its world comes from the
planet. The rest are in the shared dispatcher and reach the game's dev API too. Required parameters are
in the second column; optional ones, with their defaults, in the third. Where the dev API defaults a
parameter that picks what to act on (`give`'s `material=Wood` and `n=100`, DevCommandHandler.cpp:146-147;
`craft`'s `recipe=Recipe_AxePrimitive`), the shared verb requires it instead.

| Verb | Required | Optional | Reply on ok | Rejections |
|---|---|---|---|---|
| `world` (CLI) | `seed`, `speed` | the other map parameters | `seed`, `origin`, `shore` (when water), `area`, `regions` | `world-exists`, `bad-param`, `missing-param` |
| `colonist` | `at` | `n=1`, `name=Dev` | `ids`, `names` | `off-mesh`, `name-taken` |
| `spawn` | `def`, `at` | `n=1`, `scatter=0`, `rotation=0`, `name`, `resources` | `store` (`static` or `ecs`), `requested`, `placed`, `ids` (ECS) or `positions` (static) | `unknown-def`, `off-mesh` (none placed), `chunk-not-ready`, `name-taken`, `bad-param` |
| `give` | `material`, `n`, `where=site\|loose\|pile\|colonist\|storage\|station` | `at`, `colonist`, `slot=pack` | `added` (may be less than `n`; partial is not a refusal) | `unknown-def`, `not-found`, `no-storage`, `no-station`, `off-mesh`, `missing-param` |
| `need` | `colonist`, `need`, `value` | | `need`, `value` | `not-found`, `unknown-need` |
| `time` | one of `speed`, `set`, `skip` | | the `time` view | `bad-param`, `missing-param` |
| `teleport` | `colonist`, `to` | | `x`, `y` | `not-found`, `off-mesh` |
| `kill` | `colonist` | | `id` | `not-found` |
| `craft` | `recipe`, `at` | `n=1` | `station`, `queued` | `unknown-recipe`, `no-station` |
| `storage` | `at` | `item=*`, `category=RawMaterial`, `priority=Medium`, `min=0`, `max=0` | `storage`, `rule` | `no-storage`, `bad-param` |
| `remember` (new) | `colonist`, and one of `def` with `at`, `name`, or `radius` | | `added` | `not-found`, `unknown-def`, `missing-param` |
| `discover` (new) | `colonist` (or `all`), and one of `def` or `recipe` | | `learned`, `unlocked` | `not-found`, `unknown-def`, `unknown-recipe` |
| `state` | `what` | view filters | `what`, `data` | `bad-param`, `not-found`, `not-available` |
| `step` (CLI) | | `n=1` | `ticks`, `time` | `invariant-failed` |
| `wait` (CLI) | `until="<expr>"` with `timeout`, or `for` | | `ticks`, `minutes` | `timeout` (with the values the expression read), `paused`, `invariant-failed` |
| `expect` (CLI) | the rest of the line is an expression | | `values` | `expect-failed` (with `values`) |
| `always` (CLI) | an expression, or `clear` | | `count` (invariants registered) | `bad-param` |

Notes on the new and changed verbs:

- `spawn` routes by the static rule above. `resources=` sets a static harvestable's remaining pool (for
  "the last berries" cases) instead of the position roll.
- `give where=pile` drops `ResourceStack` piles through `EntityFactory::dropResourcePiles`, split at the
  item's stack size. `where=loose` keeps making packaged items. `where=station` adds to a crafting
  station's material store (its `Inventory`), which nothing can fill today (`give where=storage` finds
  only entities with `StorageConfiguration`, DevCommandHandler.cpp:765), so a scenario can stage a
  recipe one unit short. `colonist=` selects the target colonist directly instead of the nearest to
  `at`.
- `kill` queues the removal and drains it at once (commands run between ticks), so the removal path and
  its notices are the same as a system's removal.
- `remember` writes Memory as vision would: index entities and shore points by world-entity key, ECS
  entities as dynamic entries. With `def` and `at` it does not check that the thing exists, which is how
  a scenario seeds stale memory. With `radius` it remembers every index entity, ECS entity with
  `Appearance`, and shore point within that distance of the colonist, ignoring occlusion.
- `discover recipe=` learns every input def of the recipe in the colonist's `Knowledge`, which is what
  unlocks a non-innate recipe today (VisionSystem.cpp:40-71).
- `always` registers an invariant checked after every tick (after settling) until `always clear`. A
  failure ends the current `step` or `wait` with `invariant-failed`, naming the tick.
- `wait until` evaluates the expression after each tick and settle; `timeout` is game time. `wait for`
  steps for a game duration. Both reject at speed 0. At speed 3 (10x) and 60 Hz a tick is 10 game
  seconds, so `timeout=6h` is at most 2160 ticks.

### Views

`state what=<view>` returns the view under `data`. Lists are sorted by entity id; per-colonist views
are lists over colonists and take `colonist=` to narrow to one. Item maps (`inventory`, `store`,
`delivered`, `required`) are sparse, and a missing item reads as 0 in expressions.

| View | Contents |
|---|---|
| `colonists` | today's shape: `id`, `name`, `x`, `y`, `needs{}`, `action`, `inventory{}`, `hands{}`, `belt[]`, `cargoKg`, `carryCapacityKg` |
| `task` | per colonist: `type`, `state`, `need`, `target{x, y, def, entity, name}`, `tier`, `score`, `chainStep`, `navState`, `reason` |
| `trace` | per colonist: `evaluatedAt`, `cause`, `options[]`, and `history[]`, newest first, `history=` entries (default 8) |
| `memory` | per colonist: `count`, `entries[]` (`def`, `x`, `y`, `name`), `knows[]` defs, `recipes[]` unlocked; `def=` filters, `n=` caps entries |
| `goals` | `GoalTaskRegistry` contents: `id`, `type`, `owner`, `parent`, `item`, `quantity`, `remaining`, `target{}`, `status` |
| `claims` | the claim index of [task claims](./task-claims.md#the-claim-index): per target its claims and their top holder, and per colonist the claims on its task and suspended record (read from the tasks; the index keeps no per-colonist list or claimant list) |
| `stations`, `storage` | today's shapes, plus `name` |
| `entities` | named entities: `name`, `def`, `x`, `y`, `exists`, `store` (`static` or `ecs`), `resources` |
| `events` | the event ring: `tick`, `minutes`, `kind`, `colonist`, `entity`, `detail`; `since=<tick>` |
| `time` | `day`, `hour`, `minute`, `minutes` (since session start), `tick`, `speed`, `paused`, `season` |
| `timings` | per system: `meanUs`, `p99Us`, `maxUs` over ticks since the last reset, from `World::getSystemTimings`; `stepUs` and `settleUs` likewise; `reset=1` |
| `world` | the world parameters, `shore`, nav `regions` and `generation` |
| `summary` | `colonists`, `entities` by def, `chunks`, `ticks` |
| `digest` | `tick` and a 64-bit hash of the canonical state (see [Determinism](#determinism)) |

`trace.options[]` fields: `task`, `need`, `status`, `code` (the reason code), `reason` (prose), `tier`,
`score`, `breakdown{distance, skill, age, hysteresis, storageBias}`, `target{x, y, def, entity, name}`,
`goal`, `station`, `item`, `quantity`, and `claimedBy` (a Reserved option's holder).

`target.name` resolves through the task's or option's id fields, not its position, because the
position is snapped to an adjacent nav point for harvest and build targets. Each kind uses its own
field: `harvestTargetEntityId` (a Memory world-entity key, the hash of position and def, not an ECS
id; the name table keeps index entities by the same key), `targetStationId`, `haulSourceStorageId`,
`haulTargetStorageId`, `placePackagedEntityId`, and `buildBlueprintEntityId` (Task.h:70-99).

Phase-1 event kinds: `colonist-spawned`, `entity-spawned`, `entity-removed`, `task-started`,
`task-ended` (with outcome), `need-seek` and `need-critical` (threshold crossings, both directions),
`item-crafted`, `recipe-unlocked`. The ring holds the last 4096 and reports how many it dropped.

### Expressions and paths

`wait until`, `expect`, and `always` take one expression. The grammar is deliberately small:

```
expr      := conj ( "or" conj )*
conj      := cmp ( "and" cmp )*
cmp       := operand op operand
op        := "==" | "!=" | "<" | "<=" | ">" | ">=" | "has"
operand   := path | number | string | word | "true" | "false" | "null" | func
func      := "len" "(" path ")" | "count" "(" path op operand ")"
path      := view [ selector ] ( "." key [ selector ] )*
selector  := "[" ( name | "#" id | index | "*" ) "]"
```

- `and` binds tighter than `or`; there are no parentheses.
- A path's first segment is a view name. The view is computed fresh for each evaluation.
- `[Ada]` selects the element whose `name` is Ada (colonists by name, other entities by `name=`),
  `[#14]` by id, `[0]` by position in id order. Names start with a letter, so digits are always an index.
- `[*]` in a comparison holds when every element compares true and there is at least one element.
  In `count()` it counts the elements for which the comparison holds.
- `len(path)` is an array's length or an object's key count.
- A path that doesn't resolve is `null`. `null` equals only `null`; ordering comparisons with `null` are
  false. A missing key in an item map is 0.
- A bare word is a string (`FulfillNeed`, `Thirst`, `Terrain_Shore`). Numbers compare as doubles. A
  string never equals a number.
- `has` is true when the left array contains the right value or the left object has it as a key.

Examples: `colonists[Ada].needs.Thirst > 90`, `task[Ada].target.def == Terrain_Shore`,
`colonists[Ada].belt has AxePrimitive`, `count(task[*].target.name == oak) <= 1`,
`storage[box].inventory.Wood >= 40 or time.minutes > 600`.

A failed assertion reports every path it read with its value, so the failure line explains itself.

---

## Decision introspection

The trace answers "what was Ada choosing between just now"; the history answers "why has Ada been
doing that". Both live in libs/sim and both front ends read them; the game's decision-trace UI
([Decision Trace](../design/game-systems/colonists/decision-trace.md)) can use the same data.
`DecisionHistory` ships in release builds.

`DecisionTrace` today records every evaluated option with tier, score breakdown, target, and a prose
reason, with statuses `Selected`, `Available`, `NoSource`, and `Satisfied`, and nothing older than the
latest evaluation. This epic adds:

- Two statuses for options selection drops after scoring. Today selection relabels an option
  `NoSource` in two cases: its target is outside every active sim region, and the nav reachability
  pre-filter proves it unreachable. "Never seen one", "it's outside the simulated area", and "I can't
  get to the one I know" read the same. They become `OffArea` and `Unreachable`. Need source
  selection is being reworked on another branch (WOR-605/607: walk the candidates for a reachable one
  and carry a proven-reachable fact on options); whatever shape lands, each place that drops an option
  for area or reachability sets the matching status instead of `NoSource`.
- A reason code on every option that is not actionable (`no-source`, `off-area`, `unreachable`,
  `satisfied`, `undiscovered-recipe`, `missing-tool`, `no-room`, and the claims codes), next to the
  prose reason. Scripts match codes; the UI shows prose.
- The cause of each evaluation, one per trigger in `AIDecisionSystem::shouldReEvaluate`: `no-task`
  (no active task: never had one, or the last one ended or was cleared), `arrived` (task reached its
  target), `cant-find-way` (nav stopped the colonist), `timer` (the 0.5 s re-evaluation interval,
  `kReEvalInterval`, AIDecisionSystem.h:200), `critical-need` (a need below its critical threshold
  while the colonist isn't already handling a critical need), and `wander` (a moving Wander
  re-evaluates every tick). Crossing a need's seek threshold is not a trigger; a non-critical need is
  noticed at the next timer evaluation. Later triggers (`bumped`, once claims bumping is built; a player order) add
  causes as they add branches.

The task claims work added the `Reserved` status with its holder ([player-facing
output](./task-claims.md#player-facing-output)); the trace view shows it. The bumped marker comes with
bumping, which is planned.

A `DecisionHistory` component on each colonist keeps a fixed ring of the last 32 decision records. A
record is written when the selected task changes or a task ends, never for an evaluation that keeps
the same task (those only bump a counter on the latest record). A record holds `tick`, `minutes`,
`cause`, and `kept` (the counter); `from`, the previous task (`type`, `need`, `target`, `tier`) with
`ended` (`completed`, `preempted`, `failed`, `bumped`, `cancelled`), `endReason` (a reason code), and
`bumpedBy`; `to`, the new task (`type`, `need`, `target`, `tier`, `score`); and `runnerUp`, the best
option not taken (`type`, `target`, `tier`, `score`, `status`). Every place that clears a task today
supplies an end reason; enumerating them is part of the work, and a clear site without one is a bug the
debug build asserts on.

---

## Determinism

The contract: the same sim-cli binary, on the same machine, given the same script, flags, and config
files, produces the same reply stream (ignoring `timings` and wall-clock fields) and the same digest
sequence. Different compilers, platforms, or builds are out of scope.

What makes it hold:

- Required seeds (done). `AIDecisionSystem` and `ActionSystem` take their seed as a required
  constructor argument. Each system's seed is derived from the session seed with a fixed mix
  (splitmix64 of the seed and a per-system constant, `ecs::deriveSystemSeeds`). The game draws one
  session seed at new game, logs it at info level, and passes it; sim-cli takes it from `world seed=`. Other RNG in the sim is already seeded by position or entity
  id (PlacementSystem.cpp:249-259, GameScene.cpp:1534-1539) and stays so.
- Settling after every tick removes thread timing from chunk generation, placement, and nav builds.
- A constant `dt` and a pinned speed.
- No camera: the viewport and chunk focus are fixed at the origin.
- No state carried between sessions ([Process statics](#process-statics)).

What can still break it, and is what the test is for: iteration over an unordered container whose
insertion history differs between runs, hashing a pointer, reading uninitialized fields, and any
wall-clock read inside a system. `EvaluatedOption::tiebreakId` already guards option order against
hash order (DecisionTrace.h:118-124).

The digest is a 64-bit FNV-1a over a canonical serialization in which every float is written as the
hex of its bits, never as a decimal JSON number. It covers:

- per colonist: position; every need's value; `Task` fields `type`, `state`, `needToFulfill`,
  `targetPosition`, `harvestTargetEntityId`, `targetStationId`, `craftRecipeDefName`,
  `haulItemDefName`, `haulQuantity`, `haulSourceStorageId`, `haulTargetStorageId`, `haulFromInventory`,
  `placePackagedEntityId`, `buildBlueprintEntityId`, `chainStep`, `priorityTier`, `navState`; `Action`
  type; inventory (hands, belt, pack); Memory entry count; `Knowledge` defs;
- every ECS entity's def and position, and each pile's `ResourceStack` quantity;
- every storage and station inventory and work queue;
- the placement index's entity count, edit epoch, and every static entity's resource count;
- the goal registry and its claim sequence counter;
- the clock and tick.

sim-cli `run --digest-every=N --digest-out=<file>` appends one digest line (tick and hash) per N ticks.

The test uses ctest fixtures, so each scenario costs one extra run and a compare:

- `scenario.<name>` runs the file with `--digest-every=60 --digest-out=<a>`; it is the scenario test and
  also a fixture setup.
- `scenario.<name>.rerun` runs it again into `<b>`; also a fixture setup.
- `scenario.<name>.determinism` requires both fixtures and runs `cmake -E compare_files <a> <b>`.

`compare_files` says only that the files differ; a person diffs the two digest files to find the first
tick that diverges. A sim-tests unit test runs two sessions in sequence in one process with the same
seed and commands and compares final digests, which catches state leaking between sessions.

---

## Scenarios

### Format and registration

Scenario files are command scripts with the `.sim` extension under `apps/sim-cli/scenarios/` (scripts
don't go in docs). A file starts with comments saying what it checks, then `world`, setup, and
alternating `wait` and `expect`, with `always` for invariants. Assertions check outcomes and invariants,
never tick counts, and timeouts are generous, so tuning changes don't break them for the wrong reason.

`add_sim_scenario(<name>)` in apps/sim-cli/CMakeLists.txt registers the three entries above with label
`scenario`. Scenarios cover flows across systems (needs, AI, actions, nav, vision, goals); logic inside
one system stays in that library's unit tests. No golden images or screenshot diffs, anywhere.

### Worked example: drink at shore

```
# A thirsty colonist finds the pond, walks to the shore, and drinks.
world seed=1 speed=3 water=pond pond=40,0,15
colonist at=shore+(-4,0) name=Ada
need colonist=Ada need=Thirst value=20
wait until="task[Ada].need == Thirst" timeout=30m
expect task[Ada].target.def == Terrain_Shore
expect trace[Ada].history[0].to.need == Thirst
wait until="colonists[Ada].needs.Thirst > 90" timeout=2h
expect colonists[Ada].needs.Bladder < 100
```

Thirst 20 is below the seek threshold (50) and above critical (10), so the drink is chosen at an
ordinary evaluation, not a critical interrupt; the script doesn't assert which trigger it was.

### Worked example: two colonists, one tree, harvest goal of 1

This is the claims epic's acceptance case. Claims are built, so the two colonists no longer cut the
same tree; the scenario lands with the tool, to show it covers the case.

```
# One tree, one unit of wood wanted: exactly one colonist cuts it.
world seed=7 speed=3
spawn def=Flora_TreeOak at=origin+(12,0) name=oak
spawn def=BasicBox at=origin+(-4,0) name=box
storage at=box item=Wood category=RawMaterial priority=High min=1
colonist at=origin+(0,2) name=Ada
colonist at=origin+(0,-2) name=Ben
give material=AxePrimitive n=1 where=colonist colonist=Ada slot=hands
give material=AxePrimitive n=1 where=colonist colonist=Ben slot=hands
remember colonist=Ada name=oak
remember colonist=Ben name=oak
always count(task[*].target.name == oak) <= 1
wait until="storage[box].inventory.Wood >= 1" timeout=6h
expect count(task[*].type == Harvest) == 0
```

### Worked example: critical hunger preempts a haul

```
# A colonist hauling wood drops what it is doing for food when hunger turns critical, then resumes.
world seed=3 speed=3
spawn def=BasicBox at=origin+(10,0) name=box
storage at=box item=Wood category=RawMaterial priority=High min=30
give material=Wood n=30 where=pile at=origin+(-10,0)
spawn def=Flora_BerryBush at=origin+(0,8) name=bush
colonist at=origin name=Ada
remember colonist=Ada radius=20
wait until="task[Ada].type == Haul" timeout=30m
wait until="colonists[Ada].cargoKg > 0" timeout=30m
need colonist=Ada need=Hunger value=5
wait until="task[Ada].need == Hunger" timeout=10m
expect trace[Ada].history[0].cause == critical-need
expect trace[Ada].history[0].from.type == Haul
expect trace[Ada].history[0].from.ended == preempted
wait until="colonists[Ada].needs.Hunger > 50" timeout=2h
wait until="storage[box].inventory.Wood >= 30" timeout=6h
```

Stowing or dropping a load on interruption happens inside the chain-interruption handler, not as a
task, so it adds no history record and the record after the haul is the hunger task. Today the colonist
eats while still holding a two-hand armful (WOR-609); this script deliberately asserts nothing about
the load, so it holds before and after that fix.

### Manual scenarios and the survival matrix

| Case | Script setup | Checks | Phase |
|---|---|---|---|
| 01 craft axe and box | station, bushes, reeds, stones spawned; both recipes queued | station store reaches each BOM exactly, never over; axe in a colonist's hands or belt; a `BasicBox` storage entity exists | 1 |
| 02 stock box with wood | box with a Wood rule, oaks spawned, axe given | box reaches the rule's min; axe on the belt after a fell | 1 |
| 03 build wood foundation | foundation blueprint over spawned trees | phases Clearing, AwaitingMaterials, UnderConstruction, Built | 2 |
| 04 build walls | built foundation, wall blueprints | all segments Built | 2 |
| Eat from pack | Berry in pack, hunger 30 | eats without moving to a source; pack Berry drops | 1 |
| Forage | bush remembered, hunger 30 | harvests and eats; hunger rises | 1 |
| Drink | pond (worked example) | drinks at a shore point | 1 |
| Sleep | energy 10 | sleeps where it stands or at a bed if one exists; energy rises | 1 |
| Toilet | bladder or digestion high | relieves at a toilet location; the need falls | 1 |
| Combined relief | hunger, thirst, and bladder low together | each is relieved, in tier order, none starved by thrash | 1 |
| Critical preemption | worked example | preempted haul resumes | 1 |
| Hands full | two-hand Wood armful, thirst 20 | relieves thirst and later recovers its load or leaves a claimable pile, per [task chains](../design/game-systems/colonists/task-chains.md) | 1 |
| Undiscovered source | bush beyond sight, nothing remembered, hunger 30 | trace shows `no-source`; the colonist never paths to the bush unseen | 1 |
| Unreachable nearest source | near bush walled off, far bush open | trace shows `unreachable` for the near one; eats from the far one | 2 (needs walls; the synthetic sampler has no islands) |
| Inventory full | pack filled with stone, harvest goal | no pickup beyond capacity; the colonist hauls or drops instead of looping | 1 |

The manual scenario docs stay as the game's regression pass. 01 and 02 gain `.sim` versions in phase
1; 03 and 04 in phase 2. The claims work adds its multi-colonist scenarios
([task claims testing](./task-claims.md#testing)); `give where=station` and `spawn resources=` exist
so they can stage "the last ingredient unit" and "the last berries".

---

## Experiments

Experiments sweep parameters and compare outcomes, for example haul tiers in
`assets/config/work/priority-tuning.xml` against idle time across seeds. They are phase 2.

`sim-cli run <file> --json-summary` prints one summary object as its last stdout line:

- run: seed, vars, ticks, game minutes, wall seconds, ticks per second;
- per colonist and colony-wide, per need: minimum, game minutes below seek, game minutes below
  critical;
- idle fraction: share of ticks with no task or a Wander task;
- tasks completed per game day, by task type, from the decision history's ended records;
- task failures by reason code;
- starvation events, once health exists (until then the field is absent, not zero);
- per system: mean and p99 microseconds per tick, plus step and settle.

The sweep tool is `tools/sim-sweep.mjs` (Node ESM, no dependencies). It copies the config root to a
temp directory per variant, patches the named values, and runs sim-cli once per variant and seed:

```
node tools/sim-sweep.mjs --scenario apps/sim-cli/scenarios/colony-day.sim
    --set "work/priority-tuning.xml:TaskTiers/Haul@tier=4,5,6"
    --seeds 1-10 --var colonists=8 --var days=3
    --out sweep.jsonl
```

`--set` takes a config file relative to the root, an element path, an attribute, and a list of
values; several `--set` flags form a grid. Each output line is one run's summary plus its variant and
seed. `--jobs N` runs variants in parallel processes, which the one-session-per-process rule allows.

---

## Performance measurements

Phase 1 measures and records these in this spec, in RelWithDebInfo on the owner's machine. They are
things to know, not promises.

- Startup to the first `world` reply, first and in detail, since every scenario pays it three times:
  config and asset load, chunk generation (nine chunks at `chunks=1`), placement, and the first nav
  build.
- Ticks per second at speed 3 and 60 Hz for 2 colonists and for 50, with settle on, and the share of
  each tick spent in settle.
- Per-system timing at 50 colonists from the `timings` view. Because the view is shared, the game
  answers `state what=timings` on its dev API too, so the same systems can be compared in a running
  game.
- The cost of `wait until` evaluating its views every tick, against stepping the same ticks bare.

The SimSession refactor itself must not slow the game: the perf capture against `main` is part of
each of its steps' regression pass.

---

## Build steps

Each step builds and passes the tests on its own, and lands as its own PR. Steps 3 to 6 are
independent of each other.

### Phase 1

1. Done. Required RNG seeds. `AIDecisionSystem` and `ActionSystem` take a required `uint32_t` seed; delete the
   optional parameters and the `random_device` fallbacks. GameScene draws, logs, and passes a session
   seed with derived per-system seeds. Every test construction passes a fixed seed. Tests: existing
   suites, updated.
2. Done. Work config loading in libs/assets. New `assets::loadWorkConfig` (libs/assets/assets/WorkConfig.{h,cpp})
   with the order and checks of `GameLoadingScene::loadWorkConfigs`, which is deleted; the scene calls
   the new function and clears `GoalTaskRegistry` itself until step 7b. Test: an assets-tests case that
   loads the real config folder and reports no errors, unless ConfigValidator's tests already cover it.
3. Done. Process statics. Move the three `nextChainId` statics into a `GoalTaskRegistry` counter reset by
   `clear()`; redo the [audit](#process-statics) against the code and fix anything else it finds.
   Test: a GoalTaskRegistry case that its counter restarts after `clear()` (now the claim sequence).
4. Done. `NavigationSystem::finishPendingBuilds()`. Add it and convert the 58 `pumpUntil`/`pumpUntilMesh`
   call sites; delete those helpers and their sleeps, and update the sim-tests TIMEOUT comment that
   refers to them. Checks that a rebuild does not happen keep their fixed frame counts.
5. Done. `SyntheticWorldSampler` in libs/world with the map parameters; move the flat-land and one-pond test
   samplers onto it where touched. Test: world-tests cases that a pond's shore points exist where the
   blob says.
6. EntityFactory. libs/sim/session/EntityFactory.{h,cpp} with its operations, the `PlacementExecutor`
   insert (rejecting unprocessed chunks) and edit epoch. Delete `GameScene::spawnColonist`,
   `PlacementSystem::spawnEntity`, and `GameScene::dropResourcePiles`; their callers use the factory. The
   dev `spawn` verb routes static defs to `placeStatic`, and the ECS harvestable scans and the harvestable-pool
   branch are deleted. Tests: sim-tests cases for `placeStatic` (the inserted tree is a nav obstacle after
   a rebuild; an insert into an unprocessed chunk is refused) and pile splitting, checking for
   duplicates first. Regression: manual scenario 01 in the game.
7. SimSession, in four parts, each buildable alone with its own regression pass. Together they are the
   large, risky change: they rewire the live game's world setup, loading, landing, streaming, and frame
   loop. Every part's regression pass is manual scenarios 01-04 in the game and the perf capture against
   `main`, plus what the part lists.
   - 7a. libs/sim/session/SimSession.{h,cpp} with the ECS world, system registration, callback wiring,
     the drains, `queueRemoval`, `factory()`, and `step` (with the session's world and the GameScene's
     existing chunk manager and processor passed in). Only GameScene moves onto it. Also: scenario 07
     (pause menu drives `TimeSystem`). Test: a session smoke test on `SyntheticWorldSampler`.
   - 7b. Chunk manager, processor, and placement ownership move into the session, with `pumpStreaming`,
     `settle`, and the bake stage; GameLoadingScene and `GameWorldState` move onto it; the session clears
     `GoalTaskRegistry` and asserts one live session. Also: panning far enough to stream and unload
     chunks; return to menu and start a second game; the direct-scene fallback (GameScene.cpp:144-181).
     Test: the in-process two-sessions-same-digest test (with a minimal digest).
   - 7c. `land(rawSpawn, crew)` with changed-chunk notices. Also: a fresh Quick Start landing and a
     custom-party landing (the skills override path, GameScene.cpp:1408-1417).
   - 7d. Events replace the toast lambdas (recipe unlocked, item crafted). Also: craft one item and
     unlock one recipe in the game and see both toasts.
8. Shared commands. libs/sim/session/SimCommands.{h,cpp} and SimViews.{h,cpp}: the moved verbs with real
   rejections and required parameters, `remember`, `discover`, `give where=pile|station` and `colonist=`,
   `spawn resources=`, name and anchor resolution, the views that need no new sim data, the full digest.
   DevCommandHandler becomes the adapter. Tests: dispatcher cases for each rejection reason.
9. Decision introspection. `OffArea` and `Unreachable` statuses, reason codes, evaluation causes, the
   `DecisionHistory` component, the `trace` and `task` views, the event kinds that come from history.
   Tests: AIDecisionSystem cases for each new status and for history records on preemption and
   completion.
10. apps/sim-cli. Main, repl and run front ends, the expression evaluator, `wait`, `always`, `--var`,
    `--config-root`, `--digest-every`, `--digest-out`, exit codes (0 pass, 1 failure, 2 usage or parse
    error, 3 config load failure), the asset copy step `asset-cli` uses, `add_sim_scenario` with its
    fixtures, and the first scenarios: worked examples 1 and 3, and 01 and 02. Record the performance
    measurements here.

### Phase 2

1. Construction commit paths out of `DrawingSystem` into libs/sim (foundation, extension merge, wall
   chain, opening), and the structure callbacks into the session; `DrawingSystem` keeps the tool and
   ghost and calls the shared commit.
2. Construction verbs and the `construction` view in the shared dispatcher; `.sim` versions of 03 and 04;
   the walled-off survival cases.
3. Events log: construction and room events, `--events-out <file>` for a run's full event stream.
4. `--json-summary` and `tools/sim-sweep.mjs`.
5. Real planet regions: `world planet=<path> lat= lon=` through `GeneratedWorldSampler`, behind the same
   validity and settle rules.

---

## Known sim issues the tool exposes

These are out of scope here and need their own tickets; each changes what a scenario can assert.

- AI timers advance on raw `deltaTime`, not game-scaled time (`m_elapsedTime` at AIDecisionSystem.cpp:1077,
  `timeSinceEvaluation` at :1239), while needs, actions, and movement scale by game speed. A colonist
  re-evaluates ten times less often per game minute at speed 3 than at speed 1, so results depend on
  speed. Scenarios pin a speed in `world` until this is fixed (decided as a separate ticket).
- Vision and the goal systems throttle by frame count (VisionSystem.h:134-135 every 5 frames; frame
  counters in StorageGoalSystem, CraftingGoalSystem, BuildGoalSystem, ConstructionSystem), so the tick
  rate matters as well as the speed. Scenarios keep the default 60 Hz.
- A static entity spawned at runtime lives in its chunk's placement index, so it is gone when that
  chunk unloads and does not come back on reload. Only the game streams chunks, so only dev-spawned
  things in the game are affected; persisting runtime statics belongs with save and load.

---

## Changes docs/testing/README.md will need

When sim-cli exists (not before):

- A sim-cli row in the strategy table: scenario scripts, ctest label `scenario`, every push.
- A section on `.sim` scenarios: where they live, how to run one, `add_sim_scenario`, what belongs in a
  script versus a unit test, how to diff two digest files when a determinism entry fails.
- The scenarios table marks 01 and 02 (then 03 and 04) as also covered by a script; the manual versions
  stay as the game's pass.
- The dev API reference: verbs now answer `rejected` with a reason; required parameters on `give` and
  `craft`; `spawn` puts static defs in the placement index; the new verbs `remember` and `discover`;
  `give where=pile|station` and `colonist=`; `spawn resources=`; the new views.
- Hygiene: scripts pin `speed`, and why.

---

## Risks

- The SimSession step changes how the live game loads, lands, streams, and ticks. A mistake shows up as
  a stranded crew, missing trees, or a frame-time regression rather than a test failure. It lands in four
  parts, each with scenarios 01-04, the perf capture, and its own extra checks as its gate.
- Settling hides latency bugs, and a green scenario suite can be read as more coverage than it is. The
  [Settling](#settling) list says what it does not cover.
- Determinism holes nobody knows about yet. The determinism entries ship with the first scenario, and the
  per-60-tick digest localizes the first divergence.
- Debug CI time. Each scenario runs twice, thousands of ticks at speed 3 plus startup; if the fast bucket
  slows, slow scenarios move to the heavy label rather than getting shorter timeouts.

---

## Related documents

- [Task Claims](./task-claims.md): the claims this tool tests, the claim index the `claims` view
  shows, and the trace statuses it adds.
- [Colonist Task Arbitration](./colonist-task-arbitration.md): tiers and scores the trace reports.
- [Decision Trace](../design/game-systems/colonists/decision-trace.md): the player-facing trace.
- [Pathfinding Architecture](./pathfinding-architecture.md): sim regions, the validity predicate,
  `findValidPositionNear`.
- [Vision Architecture](./vision-architecture.md): discovery, shore points.
- [Entity Placement](./entity-placement-system.md): the placement index.
- [Testing](../testing/README.md): the project's testing strategy and the manual scenarios.
