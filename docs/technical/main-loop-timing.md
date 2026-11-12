# Main loop timing

Status: Reference, checked against the code on 2026-11-11
Created: 2026-11-11

What runs in one world-sim frame, in what order, how often each periodic job runs and on which clock, which frame a write becomes visible to its reader, and where the main thread waits. Use it before changing a rate, a priority, or the place a job runs.

The outer loop is salal's `app::App` (`libs/app/application/App.cpp` in the salal checkout pinned by the root CMakeLists.txt, fetched into `build/_deps/salal-src`). The game's part is `GameScene` (`apps/world-sim/scenes/game/GameScene.cpp`) and the ECS systems in `libs/sim/ecs/systems/`. File and function names are given rather than line numbers. Costs are quoted only where code or a doc records a measurement; nothing here has one per system.

Navigation is one persistent mesh, never rebuilt: each nav update gathers what movers want meshed and isn't as patches under per-tick count budgets, `endTick` hands them to the patch worker, and the next nav update, or the first nav query before it, joins the job. Route searches run on a route worker, one slice of fixed work a tick, alongside the rest of the frame; the patch job waits for the slice before it writes. Every mover anchors the chunks round it, each by its own range. [Nav mesh at variable resolution](./nav-mesh-variable-resolution.md) has the design.

## One frame

`main` (`apps/world-sim/Main.cpp`) creates the App with `frameCapHz = 120`, metrics on, and the dev server on port 8081 wherever it is compiled in (Debug and RelWithDebInfo), then calls `App::run`, which loops `tick()` (one frame, `App::runFrame`) and `pace()`.

| # | Step | Where | Runs |
|---|---|---|---|
| 1 | Frame delta: time since the last frame start, clamped to `[0, maxFrameDelta]` (0.25 s, `FrameConfig` in `AppConfig.h`); the first frame's delta is 0. The previous frame's metrics close here, so its time includes its pacing | `App::beginFrame` | every frame |
| 2 | Platform events into the input queue; resize | `App::pollEvents` | every frame |
| 3 | Dev server, pre-frame: every parked control (`exit`, `pause`, `resume`, `step`, `vsync`, `scene`, `reload`, `capture`, and the game's `camera` and `gpuhold`) and every `/api/dev/<verb>` route runs here on the main thread, then injected input is taken unless the App is paused. An `exit` ends the frame here | `App::devServerPreFrame`, `DevServer::servePreFrame` | every frame; work only when something is parked |
| 3a | The frame's input state taken from the accumulator | `App::readFrameInput` | every frame |
| 4 | Input routed: each event goes to the debug layers first; one they don't take goes, unless the App is paused, to the top scene's ui, then to `GameScene::handleEvent` | `App::dispatchEvents` | every frame |
| 5 | Scene switches applied (not while paused) | `App::applySceneChanges` | every frame |
| 6 | Fixed ticks at `fixedStep` (1/60 s), at most 8 a frame. `GameScene` has no `fixedUpdate`, so in worldsim these only move the accumulator | `App::runFixedTicks` | every frame, no effect |
| 7 | The top scene's `update(FrameContext{dt})`, then its ui's update, then debug layers. Skipped while the App is paused | `App::updateScenes` | every frame unless the App is paused |
| 7a | The drawing tool's or placement tool's preview follows the pointer when the pointer is over the world | `GameScene::update` | every frame a tool is active |
| 7b | Move-marker fade, held camera keys, a parked dev `camera` command, camera move, scroll zoom | `GameScene::update` | every frame |
| 7c | Camera update | `WorldCamera::update` | every frame |
| 7d | Chunk streaming around the camera and the anchors (`NavigationSystem::appendAnchors`: every mover, `Position` + `AgentRadius`, with its range R, its sight distance plus 16 m): retire finished generation futures, load every chunk in the camera's 5x5 (`m_loadRadius` 2) and every chunk within `anchorLoadRange(R)` (the larger of 128 m and R) of an anchor, unload a chunk that isn't generating, is beyond the camera's `m_unloadRadius` 4, and is farther than `anchorKeepRange(R)` (the larger of 256 m and 2R) from every anchor. The camera's chunks are drawn, never meshed | `ChunkManager::update` | every frame |
| 7f | Placement poll: integrate finished placement tasks (`PlacementExecutor::storeChunkResult`, into `m_processedChunks`), queue finished mesh bakes for upload, launch a task for each loaded chunk not placed or in flight | `GameScene::processNewChunks`, `AsyncChunkProcessor` | every frame |
| 7g | Placement data of unloaded chunks dropped | `GameScene::cleanupUnloadedChunks` | every frame |
| 7h | `ecs::World::update(dt)`: every system's `update` in priority order (below), the deferred-edit drains among them | `World::update` (`libs/sim/ecs/World.h`) | every frame |
| 7i | Every system's `endTick`, in the same order: `NavigationSystem`'s hands the patches it gathered this tick to the patch worker, which waits for this tick's route slice before it writes; no other system has one | `World::update` | every frame; nav work only when something was gathered |
| 7j | A selected pile that is gone clears the selection; the selected room goes to the room overlay | `GameScene::update` | every frame |
| 7k | `GameUI::update` (see the periodic table) | `GameUI::update` | every frame |
| 7l | Drawing status to the config strip; `m_lastUpdateMs` covers 7c to here | `GameScene::update` | every frame |
| 8 | UI layout, cursor | `SceneStack::layoutUi`, `App::showCursor` | every frame, paused or not |
| 9 | Render: `Primitives::beginFrame`, clear, the scene stack, debug layers, `endFrame` | `App::render`, `GameScene::render` | every frame, paused or not |
| 9a | Ground tiles, stale chunk re-upload capped at `kStaleReuploadBudgetBytes` (4 MiB, `ChunkRenderer.h`) a frame | `ChunkRenderer::render` | every frame |
| 9b | Committed construction, flush | `DrawingSystem::renderCommitted` | every frame |
| 9c | Entities: worker-baked chunk meshes uploaded under `kUploadBudgetBytesPerFrame` (2 MiB, `BakedChunkRenderer.h`), a synchronous re-bake on the main thread for a processed chunk whose bake was evicted and is in view again, groundcover, then the depth-sorted upright stream, which tells `TemplateLevelBuilder` which levels it wants | `EntityRenderer::render` | every frame |
| 9d | Drawing preview, then the terrain-polygon, room, nav, and vision overlays (each draws nothing unless toggled) | `GameScene::render` | every frame |
| 9e | Selection indicator, move marker, placement ghost, flush | `GameScene::render` | every frame |
| 9f | GPU hold (perf captures only); metrics: tile ms, entity ms, `m_lastUpdateMs`, counts, this frame's ECS per-system timings, GPU times | `GameScene::render` | every frame |
| 10 | Dev server, post-render: publish the frame's metrics, take parked screenshots, answer parked `/api/state` views and `/api/ui/tree` and `/api/ui/lint` | `App::devServerPostRender`, `DevServer::servePostRender` | every frame; work only when something is parked |
| 11 | Present: buffer swap, which waits for the display while vsync is on (on by default, `Platform.h`; the `video.vsync` setting overrides it) | `App::present` | every frame |
| 12 | A resume asked for inside the frame takes effect; debug layers added in the frame join | `App::finishFrame` | every frame |
| 13 | Pace to the 120 Hz cap: sleep to 2 ms before the deadline, then yield-spin; sleep 10 ms while minimized | `App::pace` | every frame |

### ECS systems in execution order

`World::update` sorts the systems by `priority()` alone (`std::sort`, on the first update after a registration; every priority is distinct) and, with `ECS_ENABLE_SYSTEM_TIMING` (on by default), times each `update`. Registration order in `GameScene::initializeECS` doesn't matter: `NavigationSystem` is registered after `RoomDetectionSystem` and still runs fourth.

| Order | Priority | System | What it does each frame |
|---|---|---|---|
| 1 | 10 | `TimeSystem` | Advances the game clock by `dt * baseTimeScale * speed`; returns at once when paused |
| 2 | 45 | `VisionSystem` | Every 5th frame: rebuilds the wall occluders if stale, then every observer's sight pass writes memory |
| 3 | 50 | `NeedsDecaySystem` | Decays every need by game minutes; nothing at game pause |
| 4 | 51 | `NavigationSystem` | Joins the patch job `endTick` launched last frame (waiting for it if it is still running, unless a query already joined it) and publishes it; hands every patch report since the last route slice to the route queue, admits (finished answers become takeable, new requests join), drops answers untaken for `kUntakenAnswerTicks`, decides whether far cells are held, and starts this tick's slice on the route worker; marks stale the routes a newly meshed placed obstacle crosses and those whose movers strayed more than `kOffRouteMm` (2 m) off their leg; steps movers standing in a placed obstacle forward along their route or out to clear ground; then gathers the next patches: each mover's newly wanted cells and chunks, removed placed obstacles, walls and doors when the construction version moved, ground for wanted chunks a mover holds (up to `kGroundVerticesPerTick`, 256 ring vertices; a chunk with water alone), and placed obstacles in wanted cells, the cells round each mover first (up to `kDetailCellsPerTick` 2 cells, `kDetailRingsPerTick` 32 adds, `kDetailRemovalsPerTick` 128 removals, `kDetailScansPerTick` 16 scans) |
| 5 | 55 | `StorageGoalSystem` | Every 60th frame: storage goals |
| 6 | 56 | `CraftingGoalSystem` | Every 60th frame: crafting goals |
| 7 | 57 | `BuildGoalSystem` | Every 30th frame: place goals for packaged items |
| 8 | 58 | `ConstructionSystem` | Every 30th frame: blueprint lifecycle and its goals |
| 9 | 59 | `RoomDetectionSystem` | Rebuilds rooms when the construction version moved |
| 10 | 60 | `AIDecisionSystem` | Route upkeep for every colonist, then a decision for each colonist whose trigger fires (see Triggers) |
| 11 | 100 | `MovementSystem` | Steers along routes; sets Arrived at the last waypoint |
| 12 | 200 | `PhysicsSystem` | Integrates velocity by game-scaled dt |
| 13 | 250 | `CollisionSystem` | Pushes agents apart, two relaxation passes |
| 14 | 260 | `WallCollisionSystem` | Pushes agents out of built walls |
| 15 | 270 | `StaticRectCollisionSystem` | Pushes agents out of placed obstacles' rects |
| 16 | 350 | `ActionSystem` | Arrival within 0.4 m, then starts, advances, and completes actions; queues removals and drops |
| 17 | 800 | `DeferredEditsSystem` (`apps/world-sim/scenes/game/world/`) | Drains deferred entity removals (`destroyWorldEntity`), then deferred spawns (packaged furniture, loose piles through `dropResourcePiles`); work only when queued |
| 18 | 900 | `DynamicEntityRenderSystem` | Collects render data for dynamic entities |

The drains run inside the tick, after `ActionSystem` (the last system that queues) and before the render collect, so the render data shows a removal or a drop the same frame. They run there rather than after `World::update` because a drop's ground probe is a nav query: after the last `update`, `endTick` has launched the next patch job and the probe would wait for it.

Inside `AIDecisionSystem::update`, in order: add raw `deltaTime` to `m_elapsedTime`; read `simulationSeconds` if regrowth is wired; re-read the base tiers if `PriorityConfig`'s generation moved; invalidate the per-update caches (colony storage, remaining need, box rooms, shared work, food boxes) and mark the claim index unbuilt; true up every suspended record. Then, per colonist: off-mesh recovery (on meshed ground but off the nav mesh's walkable faces: snap to the nearest pathable point and clear the task); take the answer to a route ticket that is out (`takeRouteAnswer`; a ticket whose task stopped Moving is cancelled); ask for a route for a Moving task with no route and none out (a chain leg, or a colonist the snap held, once it lets go or the generation moves); ask again, walking on meanwhile, when `Memory::beliefVersion` or `NavigationSystem::generation()` differs from the walked route's stamp; ask again for a route parked in `AwaitingMesh` when the nav generation moved; count the ticks a route has been out (`Task::routeWaitTicks`); skip a player-controlled colonist (its timer is reset each frame); the `shouldReEvaluate` gate; build the claim index if this is the update's first decider; build the decision trace; decide whether to switch; switch. After the loop, development builds check the claim index, only if it was built this update.

## Periodic work

Clocks: **F** frame count; **R** real seconds (the frame delta, unscaled, still running at game pause); **G** game-scaled seconds (`dt * effectiveTimeScale()`, zero at game pause); **W** wall time on a worker thread. Game pause is `TimeSystem` at `Paused` (Space, the pause menu, the dev `time` verb). App pause is the dev server's `pause` control, which stops scene updates altogether.

| Work | Where | Rate | Constant (value) | Clock | At game pause |
|---|---|---|---|---|---|
| Game clock | `TimeSystem::update` | every frame | `kSpeedMultipliers` {0, 1, 3, 10}, `baseTimeScale` 1 game minute per real second (`TimeSystem.h`) | G | stops |
| Vision | `VisionSystem::update` | every 5th frame, every observer in that frame; the counter starts at 5, so frames 1, 6, 11, ... | `m_updateInterval` 5 (`VisionSystem.h`) | F | runs |
| Needs decay | `NeedsDecaySystem::update` | every frame | per-need `decayRate`, 0.01 to 0.12 % per game minute (`NeedsComponent::createDefault`, table below) | G | stops |
| Nav join and gather | `NavigationSystem::update` | every frame; gathers only what movers want and isn't meshed, under per-tick count and vertex budgets (never a time budget), so a backlog drains over several frames; nothing is ever taken out but what leaves the world | `kGroundVerticesPerTick` 256, `kDetailCellsPerTick` 2, `kDetailRingsPerTick` 32, `kDetailRemovalsPerTick` 128, `kDetailScansPerTick` 16 (`NavigationSystem.h`) | F | runs |
| Nav patch job | launched by `NavigationSystem::endTick`, joined by the next `update` or query | every frame something was gathered; one job on the patch worker: it waits for the tick's route slice, applies the patches in order (labels kept per patch), then the door edits, then one width refresh | `kJobBudgetMs` 2 ms, the target the budgets are set for, not enforced | W | runs |
| Route slice | started by `NavigationSystem::update` on the route worker, after the join and the admit | every frame some route search is running; up to `kRouteWorkPerSlice` 18000 expansions shared by every running search, at most `kRouteQuantum` 2000 a turn (`RouteQueue.h`); a search has no limit of its own and carries on next slice | the counts come from 8 ms of route worker a tick at an estimated 0.45 us an expansion, not yet measured | W | runs |
| Route request | `NavigationSystem::requestRoute`, on the main thread: copies the belief, locates both ends, compares labels; returns a ticket | per AI route request (route upkeep, a decision) | answered at once with no search when the start is off the mesh or no goal shares its label; otherwise queued | edge, per request | runs |
| Hold on far cells | `NavigationSystem::update`, before the gather | while some search is inside its window: the window opens the first update the search is still running after a slice it had a turn in, and lasts its whole life once | `kMaxHeldAdditionTicks` 240 updates (half the 8 s a 16 m buffer gives a mover at 2 m/s); only placed obstacles of cells outside the near lane wait; ground, the near lane, removals, walls, doors and water never | F | runs |
| Untaken answers | `NavigationSystem::update` | every frame | `kUntakenAnswerTicks` 600: an answer not taken that long after it became takeable is dropped with its request | F | runs |
| Storage goals | `StorageGoalSystem::update` | every 60th frame | `updateFrameInterval` 60 (`StorageGoalSystem.h`) | F | runs |
| Crafting goals | `CraftingGoalSystem::update` | every 60th frame, the same frame as storage | `updateFrameInterval` 60 (`CraftingGoalSystem.h`) | F | runs |
| Place goals | `BuildGoalSystem::update` | every 30th frame | `updateFrameInterval` 30 (`BuildGoalSystem.h`) | F | runs |
| Construction | `ConstructionSystem::update` | every 30th frame, the same frame as place goals | `kUpdateFrameInterval` 30 (`ConstructionSystem.h`) | F | runs |
| Room detection | `RoomDetectionSystem::update` | polls every frame, works when `ConstructionWorld::version()` moved | none | F | runs |
| AI decision | `AIDecisionSystem::shouldReEvaluate` | per colonist, on an event (see Triggers), in three states every frame, else every 0.5 s | `kReEvalInterval` 0.5 (`AIDecisionSystem.h`) | R | runs: timers advance and decisions happen |
| AI route upkeep | `AIDecisionSystem::update`, before the gate | every frame per colonist; takes the answer of a ticket out; asks for a route only on a version change, a nav generation move under `AwaitingMesh`, a chain leg with no route, or the snap letting go | none | F | runs |
| Finding a way / Re-routing label | `Task::routeWaitTicks` | counts up once per AI update while a route is out; standing he shows "Finding a way", walking his old route "Re-routing", from 10 updates on | `Task::kFindingWayTicks` 10 | F | runs |
| Claim index rebuild | `AIDecisionSystem::ensureClaimIndex` | at the first decider of each update | none | F | runs |
| Movement | `MovementSystem::update` | every frame | arrival 0.1 m, waypoint advance 0.15 m, speed 2 m/s (`Movement.h`) | G | runs; nothing moves |
| Physics | `PhysicsSystem::update` | every frame | none | G | integrates zero |
| Agent collision | `CollisionSystem::update` | every frame, two passes | `kMaxAgentRadius` 0.5 m | F (a positional push per frame, no dt) | runs |
| Wall and placed-obstacle collision | `WallCollisionSystem`, `StaticRectCollisionSystem` | every frame | none | F | runs |
| Actions | `ActionSystem::update` | every frame | `kActionReachMeters` 0.4 m (`ActionSystem.cpp`) | G for progress, F for the arrival check | arrival check runs, progress stops |
| Removal and spawn drains | `DeferredEditsSystem::update` at 800 | every frame, work when queued | none | F | runs |
| Render data collect | `DynamicEntityRenderSystem::update` | every frame | none | F | runs |
| Chunk generation | `ChunkManager::loadChunk` | per chunk entering the camera's 5x5 or an anchor's load range | load radius 2, unload radius 4 chunks; anchor load the larger of 128 m and R, keep the larger of 256 m and 2R (`ChunkManager.h`) | W | runs |
| Placement and bake | `AsyncChunkProcessor::launchTask` | per loaded chunk not yet placed | none | W | runs |
| Ground tiles | `ChunkRenderer::render` | every frame | `kStaleReuploadBudgetBytes` 4 MiB | F | runs |
| Entity bake upload | `BakedChunkRenderer::processPendingUploads` | every frame | `kUploadBudgetBytesPerFrame` 2 MiB | F | runs |
| Template levels | `TemplateLevelBuilder` | when the view wants a level it lacks; uploads capped at `kUploadVertexBudget` (131072 vertices, `InstancedEntityRenderer.h`) a frame | none | W | runs |
| Colonist list, time, top bar, minimap, info panel, open dialogs | `GameUI::update` | every frame | none | F | runs |
| Resources panel | `ResourcesModel::refresh` | every 0.5 s | `kRefreshInterval` 0.5 (`ResourcesModel.h`) | R | runs |
| Global task list | `GlobalTaskListModel::refresh` | every 0.2 s | `kRefreshInterval` 0.2 (`GlobalTaskListModel.h`) | R | runs |
| Selected colonist's task list | `TaskListView::update` | when the colonist's `DecisionTrace::lastEvaluationTime` moved | none | edge, on a decision | runs |
| ECS timings | `World::update` | every frame; `/api/metrics` carries the last frame's only | `ECS_ENABLE_SYSTEM_TIMING` 1 | F | runs |
| Metrics | `App::beginFrame`, `GameScene::render`, published at step 10 | every frame; frame time averaged over the last 60 frames | none | F | runs |
| Metrics stream | `/stream/metrics` | 10 a second per connection, checked every 10 ms | `kStreamInterval` 100 ms, `kStreamPoll` 10 ms (salal `DevServerHost.cpp`) | W | runs |
| Log stream | `/stream/logs` | sends what was logged since the last send, 10 times a second, from a 1000-entry ring | same | W | runs |

## Triggers

### When a colonist decides

`AIDecisionSystem::shouldReEvaluate` is called once per colonist per AI update, after route upkeep, and not for a player-controlled colonist. While it returns false the colonist's `timeSinceEvaluation` grows by raw `deltaTime`; every path that evaluates resets it, and `ActionSystem` resets it when it clears a task. Another system asks for a decision at the AI's next update through `requestDecision(Task&)` (`AIDecisionSystem.h`), which sets it to the float maximum so condition 4 passes: `ActionSystem` after a landed cut and after a pickup by a colonist already carrying goods from earlier legs of the gathering. A colonist awaiting a route stands with his movement off and his task Moving, so no condition fires for him every frame: a route answer adds no decision, and a "no known way" answer sets `CantFindWayTo`, which is condition 3. [Colonist AI Behavior](../design/game-systems/colonists/ai-behavior.md#when-a-colonist-decides) gives the player-facing version.

Conditions, in order:

| # | Condition | Kind | Every frame while |
|---|---|---|---|
| 1 | no active task | level | the colonist has no task, normally one frame: the decision gives it one |
| 2 | task Arrived | level | the task is Arrived, which lasts the whole of every action (eat, sleep, harvest, build, craft, a haul at its source); `ActionSystem` keeps Arrived until the action completes or the task clears |
| 3 | `NavState::CantFindWayTo` and not `Task::routeFailureDecided` | edge | never: the decision that follows sets `routeFailureDecided` when the task it leaves is still a failed route, so a pick that fails again waits for the interval |
| 4 | `timeSinceEvaluation >= kReEvalInterval` | timer | never |
| 5 | a need critical now that wasn't at the last decision (`Task::criticalNeedsAtDecision`, one bit per need), unless the task serves a need that is itself critical | edge | never: the decision records the mask, so a need that stays critical waits for the interval |
| 6 | a Wander that is Moving with its movement target active | level | the colonist walks a wander leg; a held Wander (movement off: the no-option hold, or a denied or deferred route) does not fire |

So a colonist decides every frame with no task (one frame), for the length of every action, and on a walking wander. Everything else runs on the 0.5 s timer: walking to work, a critical need with nothing reachable to serve it, a route that keeps failing, a held wander.

What a decision can change while an action runs: only an interruption. `isSameTask` normally keeps the task; a non-interruptable action (eat, drink, toilet) refuses any challenger, a lower tier is refused, and a higher tier, or a same-tier winner that beat the hysteresis margin, switches. A switch leaves the action incomplete.

The decision's readers: the `DecisionTrace` component, read the same frame by `TaskListView` (after the ECS), which rebuilds the selected colonist's panel whenever `lastEvaluationTime` moved, so it rebuilds every frame while that colonist acts; the claim index, read later in the update by other deciders and by `ActionSystem::carryHarvestIn` at 350; the task and movement target, read the same frame by Movement and `ActionSystem`. The dev `colonists` view reads the task, not the trace. No UI reads the claim index.

### Other triggers

| Place | Kind | Fires | Can it fire every frame indefinitely? |
|---|---|---|---|
| AI replan on discovery | edge on version | valid route, Moving, no ticket out, and the belief version or nav generation differs from the route's stamp | no: one request, and the route it answers with is stamped with both; the nav generation moves once per joined job in which ground arrived, water was re-derived, walls changed, or a door was added or removed |
| AI deferred-route retry | edge on generation | Moving, `AwaitingMesh`, nav generation differs from `deferredNavGeneration` | no: once per generation move |
| AI route answer | level | Moving with a ticket out | yes while the ticket is out, by design: a lookup a frame; a ticket nav no longer holds (`routeTicketLive`) is asked again, an answer for a goal the task has left is dropped and asked again |
| AI chain-leg route | level | Moving, no valid route, no ticket out, and movement active or awaiting a route | no: a request returns a ticket at once |
| Route ticket answered | edge, per slice | a search found its goal or ran out of ground in a slice, and no patch before the next admit changed what it read | no: the answer is takeable once, from the update after its slice; a restart sends it back to search |
| Off-mesh recovery | level | on meshed ground and off the nav mesh's walkable faces | only if a collision push keeps moving the colonist off the mesh; the snap clears the wall band to stop the wall-trap loop |
| Vision | frame count | counter reaches 5 | runs every 5th frame forever, paused or not |
| Goal polls | frame count | counter reaches 60 or 30 | the same |
| Room detection | edge on version | construction version moved | no |
| Nav gather | level, per queue | something should be meshed that isn't, or the reverse | yes while a backlog drains, by design, at most its budgets a frame; nothing once the meshes match |
| Stale route marks | edge, per join | a newly meshed placed obstacle crosses a route's rest; a mover more than 2 m off its leg | no: a marked route is asked for again once |
| Chunk load | level | a chunk in the camera's 5x5 or within an anchor's load range (the larger of 128 m and R) isn't loaded | no: the load is recorded at once |
| Chunk unload | level | outside the camera's radius 4, outside every anchor's keep range (the larger of 256 m and 2R), and not generating; it does nothing to the mesh | checked every frame by design |
| Placement launch | level | chunk ready, not placed, not in flight | the check runs for every loaded chunk every frame |
| Placement store | edge | a future is ready | no |
| Drains | level on queue | removals or spawns queued | no |

## Hand-offs across systems and frames

| What | Writer | Reader | Seen |
|---|---|---|---|
| Need decay | `NeedsDecaySystem` at 50 | AI at 60 | the same frame, so a need crossing critical is decided on the frame it crosses |
| Vision memory | Vision at 45, every 5th frame | AI at 60 | the same frame on vision frames, else up to 4 frames old; `ActionSystem`'s forgets at 350 next frame |
| Nav mesh | the patches gathered in frame N's nav update, applied by the job launched at frame N's `endTick` after frame N's route slice | AI at 60, every later query | frame N+1: its nav update at 51 joins the job, waiting for it if it is still running; a query before that (an earlier system, a dev route, the build tool) joins it instead and waits the same way, for the slice as well when a search is running. No query reads the mesh while it is patched |
| Nav generation | the join of a job in which ground arrived, water was re-derived, walls changed, or a door was added or removed; placed obstacles never move it | AI replan and deferred retry | the same frame (nav before AI) |
| Connectivity epoch | the label upkeep inside each patch, and a door added | the AI's verdict memo (`SourceReachMemo`) | the frame the job is joined |
| Goals | goal systems at 55 to 58 on their poll frames | AI at 60 | the same frame on poll frames; deposits and completions at 350 reach a goal at its system's next poll (30 or 60 frames); `GoalTaskRegistry::remainingNeed` reads destinations live |
| Claim index | AI, rebuilt at the update's first decider, appended by each acceptance, retired on switch and recovery | later deciders in the update; `ActionSystem::carryHarvestIn` at 350 | the same frame if anyone decided this frame, otherwise as of the last frame someone did; `ActionSystem`'s own changes reach it at the next rebuild |
| Per-update AI caches (remaining need, box rooms, shared work, colony storage, food boxes) | AI | AI | invalidated at the update's start, and per box when a colonist puts goods in it inside the update |
| Task and movement target | AI at 60 | Movement 100, Physics 200, `ActionSystem` 350 | the same frame: a target at the colonist's own position is Arrived at once and its action starts the same frame |
| Arrival | Movement (0.1 m from the last waypoint); `ActionSystem` (0.4 m from the target, after the collision pushes) | AI condition 2, `ActionSystem` | `ActionSystem` the same frame; AI the next frame |
| Route requests | AI at 60 (`NavigationSystem::requestRoute`, a ticket) | the route queue, at the next nav update's admit | searched from frame N+1's slice; a short search's answer is takeable at frame N+2's nav update and taken by the AI at 60 that frame; a long one takes as many slices as its work needs. Answered at once (start off the mesh, or no goal sharing the start's label) is takeable the same frame |
| Route answers | `AIDecisionSystem::takeRouteAnswer`: Routed attaches the route (Traveling), `StartOffMesh` parks the task in `AwaitingMesh` with the generation, `NoKnownWay` sets `CantFindWayTo` (or waits while the snap holds him) | Movement; the info panel (`GameUI::update`, after the ECS in the same `GameScene::update`); the AI | Movement and the panel the same frame; the AI's decision on `CantFindWayTo` the same frame or its next update |
| Placed-obstacle removals | `ecs::takeWorldSource` in `ActionSystem` removes the entity from the placement index at once and queues the removal (`PlacementExecutor::takeRemovals`) | the nav gather | the next frame's nav update gathers it and its `endTick` launches it; queries see it the frame after |
| Removals and spawns | `ActionSystem` callbacks (350); the structure callbacks wired on `ActionSystem` and `ConstructionSystem` (58), the salvage drop among them; `enqueueResourceDrop`, wired on `ActionSystem`, `ConstructionSystem`, and `AIDecisionSystem` (60) | `DeferredEditsSystem` at 800 | the same frame, before the render collect; the AI sees a drop next frame, a colonist's memory only after a vision pass. Not everything is deferred: the dev `kill` route calls `ecs::destroyWorldEntity` directly at step 3 |
| Packaged spawn target | the craft-completion callback calls `NavigationSystem::findValidPositionNear` at 350; spawn at the drain | `BuildGoalSystem` | at its next poll, up to 30 frames later |
| Regrowth clock | `takeWorldSource` stamps `simulationSeconds` at 350 | AI, which reads it once at its update's start | next frame |
| Chunk generation | a `std::async` worker generates the tiles | `ChunkManager::update` retires the future; placement launch checks `isReady` | the frame the poll sees it, before the ECS |
| Placement results | placement worker | `pollCompleted` before the ECS; Vision, Nav, Construction, and the rect collider read the index the same frame | the poll's frame |
| Dev routes | step 3 | the ECS | the same frame's update |
| Dev state views | step 10 | the HTTP client | after this frame's render |

## Clocks and units

| Clock | Source | At game pause | At 3x and 10x | At App pause |
|---|---|---|---|---|
| Frame delta | `App::beginFrame`, clamped to 0.25 s, first frame 0; `frame.dt` to `GameScene::update` and on to `World::update` unchanged | runs | unchanged | `update` isn't called |
| Game clock (day, time of day, season) | `TimeSystem::advanceTime`, `gameMinutes = dt * baseTimeScale * speed` | stops | 3x, 10x | stops |
| `simulationSeconds` | the same game minutes, summed (`TimeSystem::simulationSeconds`) | stops | 3x, 10x | stops |
| `effectiveTimeScale()` | `baseTimeScale * speed`; Movement, Physics, Needs, and Actions multiply `dt` by it | 0 | 3, 10 | n/a |
| AI `m_elapsedTime`, `timeSinceEvaluation` | raw dt | runs | unchanged | stops |
| UI model timers, move-marker fade, camera | raw dt | run | unchanged | stop |
| Frame counters | Vision 5, storage and crafting 60, place goals and construction 30, the Finding a way / Re-routing label 10 AI updates, route slices, the hold window 240 and untaken answers 600 nav updates | run | unchanged | stop |
| Collision pushes | per frame | run | unchanged | stop |
| Worker wall time | chunk generation, placement and bake, nav patch jobs, route slices, template levels | run | unchanged | run; a job in flight finishes |

One simulation second is one game minute: `baseTimeScale` is 1 game minute per real second, and `simulationSeconds` sums the same scaled `dt` the day clock reads as minutes.

Frame rate is at most 120 (the cap), and with vsync on (the default) 60 on a 60 Hz display. A frame longer than 0.25 s advances everything as if it were 0.25 s.

What changes with frame rate: vision, the goal polls, construction, the Rerouting hold, collision pushes per second, and every per-frame AI state (decisions per second double from 60 to 120 fps). What changes with game speed: every R and F rate, measured in game time; G rates are fixed in game time.

Colonists get `NeedsComponent::createDefault()` (`Needs.h`); the `Need` struct's own default of 1 % per game minute is used by nothing in the game. Rates are % per game minute; a need is sought below its seek threshold and critical below its critical one. Only the first five are actionable (`kActionableNeeds`), and the AI's critical tier uses a fixed 10 (`kCriticalNeedThreshold`), so hygiene's 15 matters only where `Need::isCritical` is read.

| Need | Decay | Seek | Critical |
|---|---|---|---|
| Hunger | 0.08 | 50 | 10 |
| Thirst | 0.12 | 50 | 10 |
| Energy | 0.05 | 30 | 10 |
| Bladder | 0.03 | 30 | 10 |
| Digestion | 0.02 | 30 | 10 |
| Hygiene | 0.015 | 40 | 15 |
| Recreation | 0.01 | 30 | 10 |
| Temperature | 0 | 40 | 15 |

Worked examples, at the default walking speed (2 m/s) and the fastest decay (thirst, 0.12 % per game minute):

| AI interval, 0.5 s real | 1x | 3x | 10x |
|---|---|---|---|
| simulation seconds (game minutes) | 0.5 | 1.5 | 5 |
| on the day clock | 30 s | 90 s | 5 min |
| metres walked | 1 | 3 | 10 |
| thirst lost | 0.06 % | 0.18 % | 0.6 % |

| Vision, every 5 frames | 60 fps | 120 fps |
|---|---|---|
| real interval | 83 ms, 12 passes a second | 42 ms, 24 a second |
| simulation seconds at 1x / 3x / 10x | 0.083 / 0.25 / 0.83 | 0.042 / 0.125 / 0.42 |
| metres walked between passes at 10x | 1.7 | 0.83 |

| Goal poll | real at 60 fps | real at 120 fps | simulation seconds at 1x / 3x / 10x, 60 fps | at 120 fps |
|---|---|---|---|---|
| storage, crafting (60 frames) | 1.0 s | 0.5 s | 1 / 3 / 10 | 0.5 / 1.5 / 5 |
| place goals, construction (30 frames) | 0.5 s | 0.25 s | 0.5 / 1.5 / 5 | 0.25 / 0.75 / 2.5 |

## Threads

| Thread or pool | Count | Owner | Does | When the main thread waits for it |
|---|---|---|---|---|
| Main | 1 | process | platform, input, dev-server jobs, scene update, ECS, UI, GL, present, pacing | n/a |
| Dev server listener | 1 | salal `DevServerHost` | cpp-httplib accept loop | never; joined at shutdown |
| Dev server handlers | cpp-httplib's default pool, `max(8, hardware_concurrency - 1)` | cpp-httplib | parse a request, park it for the main thread, wait for the answer (503 after `requestTimeout`, 10 s); the SSE streams | never; they wait for the main thread |
| Nav patch worker | 1 persistent thread | `NavigationSystem` | waits for the tick's route slice, applies the tick's patches (labels kept per patch), the door edits, then one width refresh | at the next nav update's join, or at the first nav query before it, when the job is still running; `finishPendingBuilds` waits for everything (tests, headless hosts) |
| Nav route worker | 1 persistent thread | `NavigationSystem` (`nav::RouteQueue`) | one route slice a tick: up to `kRouteWorkPerSlice` expansions over the running searches, reading the mesh const | through the patch job, which waits for the slice; with no job that tick, the next nav update waits for it |
| Chunk generation | one `std::async` thread per chunk generating, unbounded | `ChunkManager` | the chunk's tiles and terrain polygons | not per frame; `finishPendingGeneration` (loading) waits |
| Placement and bake | one `std::async` thread per chunk in flight | `AsyncChunkProcessor` | placement, then the CPU mesh bake | not per frame; `waitAll` and `clear` wait |
| Template level builder | 1 `std::jthread` with its own `foundation::TaskPool` (`hardware_concurrency - 1`, at least 1); one builder, owned by `EntityRenderer`'s `InstancedEntityRenderer` | renderer | mesh levels for the zoom | not per frame |
| Ground tile bake | a `TaskPool` while baking | `GroundTileBank` | ground tiles | loading only |

During a pan, chunk generation threads, placement threads, the two nav workers (patch and route), and the level builder's pool can all be runnable at once, more threads than cores; nothing caps the async launches.

## Hazards

Ranked by how likely a rate or order change breaks something.

1. The AI runs on real time and keeps deciding at game pause. `m_elapsedTime` and `timeSinceEvaluation` advance by raw `deltaTime`; the task-age score term (`PriorityConfig::calculateTaskAgeBonus`) reads `m_elapsedTime`, so it is in real minutes too. Vision, the goal polls, construction, and the collision pushes run at pause as well. A decision at pause can switch tasks, take and retire claims, and drop goods (chain interruption) while nothing moves. At 10x the 0.5 s interval is 5 game minutes and 10 m of walking. Decide which clock a rate belongs on before changing it.
2. Per-frame decisions carry behaviour. A colonist decides every frame while acting (the trace stays current for the panel; interruption needs a higher tier), for a frame when idle, and on a walking wander (the first non-idle option is taken the frame it appears). Those decisions double from 60 to 120 fps, so decision cost isn't proportional to colonists.
3. Same-frame order inside the ECS. NeedsDecay 50 before AI 60 (a critical crossing is seen the frame it happens); Nav 51 before AI (a landed mesh and its generation are seen before the AI plans); Vision 45 before AI (fresh memory on vision frames); goal systems 55 to 58 before AI; AI before Movement 100 (a new target moves the same frame); the collision systems 250 to 270 before `ActionSystem` 350 (arrival is judged after the push apart, so two colonists bound for one point can both arrive). `World::update` sorts by priority alone; a new system's priority must not fall between one of these writers and its reader.
4. The claim index is only as fresh as the last frame someone decided. `ActionSystem::carryHarvestIn` reads whatever is there, and the development check runs only on frames that built the index. The design accepts over-claiming for a frame ([Task Claims](./task-claims.md)); fewer per-frame deciders means more frames on an older index.
5. Periodic work lands on the same frames. Vision does every observer in one frame. Storage and crafting share frames, and place goals and construction coincide with them every 60th frame (all four counters start at 0); vision fires on frames 1 mod 5, so it never shares a frame with them. AI timers reset on the frame a colonist decides, so a landing party, and colonists that decide on one event, re-decide on the same frame every 0.5 s. Read frame-time spikes with `ecsSystems` in `/api/metrics` rather than the average.
6. Nav costs land on the main thread outside the workers. The gather (diffing, building the rings) runs in the nav update, and the join waits for the workers when a job outlasts the rest of the frame; a query from earlier in the frame (Vision at 45, a dev route at step 3) does that waiting instead. A mesh change (a wall, a felled tree, a chunk's ground) reaches queries one frame after the update that gathered it, later when a budget holds it back, so routes plan on the old mesh for those frames. Route searches run on the route worker; on the main thread a request costs a belief copy, two locates and a label compare, and a decision's reach checks are label compares (`connected`).
7. One-frame and longer delays that change outcomes. A completed action leaves the colonist idle for the rest of its frame. Movement sets Arrived one frame and the AI sees it the next. A drop reaches memory only after a vision pass; the dev `kill` route skips the drain, so code that assumes every removal is deferred is wrong. A crafted package waits up to 30 frames for its place goal. A route answer comes two frames after its request at the soonest, and the colonist stands meanwhile unless he is replanning a route he walks. The Finding a way / Re-routing label waits 10 AI updates, 0.17 s at 60 fps and 0.08 s at 120.
8. Frame-rate dependence of physical behaviour. The collision systems push positions per frame with no dt, so separation is twice as fast in real time at 120 fps and ten times weaker relative to walking at 10x. `MovementSystem` caps fast-forward steps so a waypoint isn't skipped, but `ActionSystem`'s 0.4 m arrival is checked after the push from a step that can be 0.33 m (10x at 60 fps).

## What cannot be observed

What exists: `/api/metrics` with frame time over 60 frames, the main loop's section times, draw counts, and `ecsSystems` (per-system milliseconds of the last frame only, no window and no count of work done); the `/api/state` views; `[AI]` INFO per task switch; `[Action]` INFO per action start and completion; `[Nav]` DEBUG per route answer dropped untaken; `[NavBuild]` DEBUG per joined job (job ms, widths ms, the main thread's wait, slots and patch count; each patch's time when the job is over budget) and per ground chunk; chunk load DEBUG with its latency. `NavigationSystem::navQueryStats` counts patches and the ground, water and wall patches among them, and `NavigationSystem::routeStats` counts requests, answers, restarts, drops, slices with their expansions and time, and ticks held, but no view exposes either.

Nothing counts or times:

- decisions per second by cause (the six conditions above), and how many colonists sit in each every-frame state;
- decisions that kept the task against those that switched (only switches log);
- decision-trace builds and `TaskListView` rebuilds;
- claim index rebuilds, their size and time, frames with no decider;
- path requests by outcome (routed, waiting, blocked) and A* time;
- nav gather time per frame and the main thread's wait at the join, outside the debug log;
- vision passes and observers per pass, separated from the frames it skips;
- goal polls and the goals they create or retire;
- chunk generation and placement tasks in flight (`pendingCount()` exists, unexposed);
- removals and spawns drained per frame;
- the entity upload backlog, evicted-chunk re-bakes, and template levels pending (the `uprights` view has `levelRequestsPending`);
- dev route and state-view time on the main thread;
- frames in which the game was paused while the ECS ran.

## Not verified

- Whether menu-scene thread pools (the globe view, the decorative planet, planet generation) and the asset load thread outlive their scenes.
- Whether the replan on a nav generation move can recur every frame for one mover; nothing found makes it, and nothing rules it out.

## Related documents

- [Colonist AI Behavior](../design/game-systems/colonists/ai-behavior.md#when-a-colonist-decides): when a colonist decides, for players and designers
- [Task Claims](./task-claims.md): the claim index and its freshness rule
- [Task Generation Architecture](./task-generation-architecture.md): goal systems and their polls
- [Nav mesh at variable resolution](./nav-mesh-variable-resolution.md): the patched mesh, its budgets, anchors, and routes by ticket
- [Pathfinding Architecture](./pathfinding-architecture.md): the path query
- [Vision Architecture](./vision-architecture.md): the vision pass
- [Observability](./observability/INDEX.md): the dev server, metrics, and logs
- [sim-cli](./sim-cli.md): a headless host for the same tick
