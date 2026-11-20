# Behavior data migration

## Goal

Move everything a colonist does on purpose from hand-written C++ to behaviors, needs, actions, queries,
chains, and work entries in XML, run on the kept engine, as [Queries and chains](./queries-and-chains.md)
specifies. Move one path at a time, each path's existing tests kept as the contract, and delete the
old code for a path in the same change that moves it.

## What exists now

All of it is on `main` and on this branch.

- Needs: `NeedType` (eight values), `kNeedLabels`, `NeedsComponent` with a fixed array and
  `kActionableNeeds` (six, Wash included), decay rates as literals in
  `libs/sim/ecs/components/Needs.cpp`, thresholds in `assets/config/colonists/needs/needs.xml` read by
  `NeedLevels`, mood weights in `assets/config/colonists/mood.xml` (`<NeedWeights>`, an average). PR 506
  (WOR-1055) replaces the average with summed costs and ships each cost on the human's need entries in
  `needs.xml`, not in a `<NeedCosts>` block; the config-move ticket carries `needs.xml` into the human's
  folder, and the need-entries ticket turns each cost into a `<moodCost>` row of the human's definition.
- The human: the asset folder `assets/world/colonists/Colonist/` (`Colonist.xml` with the `Colonist`,
  `Colonist_up` and the other facing definitions, `biped.motion.xml`, the SVGs), and its config in
  `assets/config/colonists/` (`mood.xml`, `memory.xml`, `traits/`, `attributes/`, `needs/`). Code names
  the definition `"Colonist"` in `GameScene.cpp` (the spawned `Appearance`), in `kinds.xml`
  (`<Kind name="colonist" mover="Colonist"/>`), and in the facing variants.
- Options: `AIDecisionSystem.cpp` (about 4,000 lines). The needs loop with its hunger special case
  (about lines 2,560 to 2,956, with the gather-food option built inside it), `needToCapability`,
  `getFirstActionDefName`, `taskFirstActionNeedsHands`, `findToiletLocation`
  (`components/ToiletLocationFinder.cpp`, a 3 m grid out to 25 m, with no unit test),
  `evaluateHaulOptions`, `evaluateCarryInOptions`, `evaluateHarvestOptions`, `evaluateBuildOptions`,
  `evaluateDeconstructOptions`, `evaluatePlacePackagedOptions`, the craft station options, wander,
  `classifyTier`, `isSameTask`, `selectTaskFromTrace`, `handleChainInterruption`, `acceptClaims`,
  `putDownUnwantedArmful`, `setDownUnplacedFurniture`.
- Actions: `components/Action.h` (`ActionType`, the factories with their durations, the `ActionEffect`
  variant), `systems/ActionSystem.cpp` and `systems/action/{NeedActions,HarvestActions,HaulActions,CraftActions,BuildActions}.cpp`
  (the start routines, `headToDeposit`, `headTo`, `carryHarvestIn`, `makeRoomToEat`, the `apply*Effect`
  functions). Every action but Sleep is non-interruptable; Sleep pays at its end.
- Claims: `systems/ChainClaims.{h,cpp}` (`recordChainClaims` per chain shape), `ClaimIndex.{h,cpp}`,
  `Task.h` (claims, `SuspendedWork`, the per-type fields, `chainStep`, `deliveryChosen`).
- Goals: `GoalTaskRegistry.{h,cpp}` (`GoalTask::type` is a `TaskType`), `StorageGoalSystem`,
  `CraftingGoalSystem`, `BuildGoalSystem`, `ConstructionSystem`, `ColonyGoalWork.cpp` for the panel.
- Thought sources: `ThoughtTypes.cpp` resolves `actionStarts` and `actionFinishes` through
  `actionTypeFromName`, and `ThoughtSourceSystem` holds an `ActionType`. The thought data names
  actions and needs by their old names: `events.xml` and `work.xml` use `action="Eat"`, `"Sleep"`,
  `"Craft"`, `"Build"`, and `needs.xml` uses `need="Hunger"` and the rest.
- Colony membership does not exist: `kinds.xml` maps `colonist` to every mover of the definition.
- Config: `assets/config/actions/action-types.xml` (`ActionTypeRegistry`; only `needsHands` of a
  task's first action is read; Withdraw, PickupPackaged, and PlacePackaged have no entry),
  `assets/config/work/task-chains.xml` (`TaskChainRegistry`, read by nothing but `ConfigValidator`),
  `assets/config/work/work-types.xml` (`WorkTypeRegistry`, same), `assets/config/work/priority-tuning.xml`
  (`<TaskTiers>` read; `<WorkCategoryOrder>` parsed and unused).
- `TaskType` is used in 22 non-test files (`AIDecisionSystem.cpp` 117 references, `GlobalTaskAdapter`
  40, `ColonyGoalWork` 26, `ChainClaims` 18, `ColonistDetailsModel` 15, the conditions 12,
  `DecisionTrace` 11), and the condition vocabulary's `<TaskGroup name="work">` lists task type names.
  `NeedType::` appears 484 times in 9 test files and 121 times in 21 other files.
- Tests: `ChainClaims.test.cpp` (about 100 flow tests of claims, gathering, hunger, hands),
  `AIDecisionSystem.test.cpp`, `ActionSystem.test.cpp`, `BuildAction.test.cpp`, `ClaimIndex.test.cpp`,
  `GoalTaskRegistry.test.cpp`, `StoreOrDrop.test.cpp`, the goal-system suites, and the benches in
  `AIDecision.bench.cpp`. The tests hold about 310 `== TaskType::X` asserts and 104 hand assignments of
  `task.type`. `BM_WorkDecision` sets every need to 100, so no bench covers the needs path.
- Scenarios: none covers sleep, relieving (and the bio pile), or the box-to-box pull. Scenario 19
  names `colonists/needs/needs.xml` and its error text; scenario 21 expects Recreation and Temperature
  dimmed under Comfort.

## Rules for the move

### Values carried over

Every shipped entry reproduces the hand-written behavior and its values, as the spec's shipped tables
give them: eating one unit a decision until hunger is above seek, carried food eaten at once, the
strict free-before-held food rule, relief at 2, 4, and 5 s with the toilet finder's grid and weights
and its ground fallback, Wash and Eat needing free hands, Sleep paying at its end with any bed taken
before the ground, Craft fixed-duration and Craft, Build, and Deconstruct non-interruptable, no urgent
rung and a decision at once only on critical, and wander pinned below gathering. Two things differ from
the start, and both are in the spec's [Changes to behavior](./queries-and-chains.md#changes-to-behavior):
relief decides at selection whether it covers both needs, which the code decides on arrival (no
entry without logic can wait for arrival); and enjoying nature, which is new, owner-requested as
WOR-1054.

Every other section of Changes to behavior is its own ticket after its path has moved. It lands only
with the owner's sign-off and lists the test expectations it changes.

Until the colony-membership ticket lands, the condition kind `colonist` and every rule the spec gives
to colony members mean every Human.

### Seams

During the move both paths run, on disjoint ground that phase 7 deletes:

- Needs: a need is owned by the data path if and only if it has a `<satisfiedBy>`. `kActionableNeeds`
  is the old loop's list. Each need's move adds the row and removes the need from the list in one
  change, and the loader fails if a need is in both.
- Idle: the human's `<idle>` rows against the old wander and gather-food builders.
- Work: per goal type and source, by which `evaluate*Options` call remains against which `<work>`
  entries exist.
- Starts: `ActionSystem::startAction` sends `TaskType::Chain` to the one chain start path and every
  other type to the old routines until they are deleted.
- `TaskType::Chain` is the one task type every chain runs under until phase 7. Its consumers are
  handled in the chains-in-the-decision ticket: `isOptionCurrentTask` (by the spec's identity rule),
  `classifyTier`, `verdictKeyOf`, the trace names, the claim holder's task type, `current_task` and the
  task groups (which list both task type names and chain ids while both exist, each path editing the
  `work` group), the UI adapters, and the dev `what=colonists` JSON.
- `NeedType` stays as the old loop's handle, resolved to a need id at load, until Eat deletes its last
  case, so the tests change once.
- No cargo handoff spans two runners: gather food moves with Eat, and the box-to-box pull moves after
  the gathering.

### Porting tests

- Assertions on observable results stay textually unchanged: need values, inventory, claim lists,
  positions, event counts, action names, durations.
- `task.type == TaskType::X` becomes a helper per path (`isRunning(task, "human/wash_at_water")`), with
  the mapping in the PR. Narrowing to the exact chain is allowed; weakening (to `isActive()`, say) is
  not.
- Setup goes through real selection, or through one `startChainAt(colonist, chain, step, target)`
  helper. No test writes chain-state fields directly. A test that builds an `Action` by factory builds
  it from the shipped entry instead and asserts the same values.
- `scripts/test-assertions.mjs` compares the EXPECT and ASSERT count of every test between base and
  head and fails on any drop. It runs on every path PR.
- A port that needs an expectation changed stops the path and goes to the owner.

## Phases

Ordered by risk, least first: the engine is proven on new, solo behavior before anything stable moves,
needs move before work, and the claims-heavy work moves last. Every phase's exit includes the full
suite (all nine test executables) and the assertion-count script.

### Phase 0: preparation

On the old code, with no behavior change except the owner-approved mood rule: the spec fixes in the
other documents, the scenarios and the relief-spot test that guard paths with none, the benches the
needs path lacks, the assertion guard, summed mood costs, `GoalType`, and the human's asset folder and
config moved to their homes.

Scenarios: the three new ones (sleep, relieving, box-to-box pull), verified on `main`; 12, 18, 19, 21
after the folder moves.

### Phase 1: the engine

The loaders for the behavior library, the shared folders, and the creature block, with their load
errors on the loading scene; the named ladder; action entries as the one source of action values,
with thought sources matched by entry id; facts and queries with the definition cache and the
per-definition spatial index; the primitive effects; the chain runner on `Task`; chains in the
decision.

Proves the 22 runner requirements on synthetic chains in `Chains.test.cpp`, and query evaluation at
10,000 remembered items.

Scenarios: 18, and 19 with new steps for the new folders' errors.

### Phase 2: needs as data

The human's needs load from behaviors with their levels and drains (no change of shape); then enjoy
nature (WOR-1054, paused until this phase), the engine's first need end to end and the first
`<satisfiedBy>`; then `NeedsComponent` as the creature's own list, and every system that iterates
needs over it.

Scenarios: 12, 18, 19 updated, 21 updated (Recreation no longer dimmed), and a new enjoy-nature
scenario.

### Phase 3: the existing needs, one at a time

Wash, Drink, relieving, Sleep, then Eat with gather food. Each moves its chain, queries, action, and
`<satisfiedBy>`, and deletes its cases from the needs loop, `needToCapability`,
`getFirstActionDefName`, `startNeedAction`, and the factories, in one change. The food services are
first extracted from the old hunger code as a refactor, so the Eat change is smaller. After their paths
have moved, the need-side behavior changes follow, each signed off by the owner: the urgent-need rung,
the sleep curve, eating to full at a place to eat, food preference, and the sampled relief spots.

Scenarios: 21 (Wash, Drink), the sleep and relieving scenarios, 12 (Eat and gathering food).

### Phase 4: idle and orders

Wander and the idle put-down; orders with a dev verb; colony membership. Direct control stays engine.

Scenarios: 13, with a wander step.

### Phase 5: colony work, one path at a time

Place packaged, deconstruct, build, the site clear, the gathering (cut, pile haul, carry-in together),
the box-to-box pull, then the craft, then interruptable crafts as a behavior change signed off by the
owner. The work services are first extracted from the old builders as a refactor.

Scenarios: 02 step 10 and 03 (place packaged), 03 step 10 (deconstruct), 03, 04, 15 (build), 02
(gathering), the pull scenario, 01 (craft).

### Phase 6: low-mood behaviors

WOR-951 (behavior definitions as data) under WOR-919 (Colonist mood: low-mood behaviors, refusal, and
colony failure): behaviors with `<lowMood>` entries, the `low_mood` rung, weighted targets, `count_of`
and `per`, `mood_source`, and the log-only chains.

Scenarios: 18.

### Phase 7: retire what is left

Delete the closed lists and bring the docs to the code.

Scenarios: 01, 02, 03, 04, 12, 13, 15, 18, 19, 21, enjoy nature, sleep, relieving, and the pull.

## Tickets

45 tickets. "Suite" means all nine test executables and the assertion-count script pass. A ticket
marked "behavior change" lands only with the owner's sign-off.

### Phase 0

1. Fixes in the other documents.
   - Built: `interruptable-crafts.md`'s ladder with the `low_mood` rung and the bumping rule by rung
     name; `INDEX.md`'s ladder text; `ai-behavior.md`'s rung table and its "only critical needs
     preempt" sentence; `mood.md` (needs from the creature's behaviors and tasks from chain ids,
     low-mood behaviors as `<lowMood>` entries in behaviors, the do-step rule for modders, leaving the
     colony as `<leave_colony/>`, the "Leaving the colony" list without bold lead-ins, colonists as
     humans with membership); `thoughts.md` (the bubble peak at 3.5, the colony-wide amount decaying
     by half-life); `needs.md` (the mood cost given by the creature that has mood, in the human's
     definition, in place of "Each need's cost is on its own entry" and the old `mood.xml` path;
     Recreation drains steadily and only enjoying nature restores it; the Temperature status line
     out); `player-control.md` (no direct control or orders during an episode).
   - Deleted: none.
   - Must pass: review.
   - Depends on: none.
2. Scenarios for sleep, relieving self, and the box-to-box pull; a relief-spot rule test.
   - Built: three scenarios verified on `main`; a `findToiletLocation` test pinning 1.5 m off water,
     the proximity sums toward waste and away from food, a latrine before the ground, and the ground
     fallback.
   - Deleted: none.
   - Must pass: the scenarios, sim-tests.
   - Depends on: none.
3. Need, idle, and vision benches.
   - Built: `BM_NeedDecision` (hungry and thirsty colonists, 9,500 edible and 500 water memories, the
     memory's 10,000 cap, 1 and 8 colonists), `BM_IdleDecision` (wandering colonists deciding every frame), a vision remember and
     forget bench, a visit count in `FindsANeedSourceInALargeMemory`.
   - Deleted: none.
   - Must pass: the benches; baselines recorded only with both machine checks `idle`.
   - Depends on: none.
4. Test assertion guard.
   - Built: `scripts/test-assertions.mjs`.
   - Deleted: none.
   - Must pass: the script reports no drop on a no-op change and catches a deleted assertion.
   - Depends on: none.
5. Mood as summed need costs (WOR-1055, PR 506).
   - Built: the sum rule per `needs.md`, each cost on the human's need entry in `needs.xml`, with
     Temperature's cost 60.
   - Deleted: the averaging, `<NeedWeights>`.
   - Must pass: `MoodSystem` tests with their owner-approved expectation changes, scenario 18.
   - Depends on: none.
6. `GoalType` on goals.
   - Built: `GoalTask::type` becomes `GoalType`, mechanically, with `clear` its own type.
   - Deleted: `TaskType` on goals.
   - Must pass: suite.
   - Depends on: none. Not in parallel with ticket 10 (both touch `AIDecisionSystem.cpp`).
7. The human's asset folder.
   - Built: `assets/world/colonists/Colonist/` moved to `assets/world/creatures/Human/` with
     `Human.xml` and the definitions renamed `Human`, `Human_up`, and the rest; `GameScene.cpp`'s
     `Appearance`, `kinds.xml`'s mover, and every other use renamed.
   - Deleted: the old folder; the staged copy beside the exe pruned.
   - Must pass: suite, scenarios 12, 13, 18.
   - Depends on: none.
8. The human's config into its folder.
   - Built: `mood.xml`, `memory.xml`, `traits.xml`, `attributes.xml`, and `needs.xml` (with its costs)
     read from `assets/world/creatures/Human/` by root element; `WorkConfig.cpp`, `ColonistRules`, the
     loaders' comments, and scenario 19's paths pointed there.
   - Deleted: `assets/config/colonists/`.
   - Must pass: suite, scenarios 18, 19.
   - Depends on: 5, 7.

### Phase 1

9. Entry loaders.
   - Built: the `assets/config/behaviors/` loader (one behavior a file, ids from paths unique per entry
     type, short ids inside a folder, the shadowing warning, duplicate full ids an error, so a mod
     adds and never replaces); the
     `actions`, `work`, `orders`, `facts`, and `arbitration` loaders; the creature loader as the spec's
     Loading a creature states; the reserved `script` attribute; errors naming file, entry, element,
     and attribute on the loading scene. In the same change `priority-tuning.xml` moves to
     `arbitration/`, and `action-types.xml` and `work-types.xml` move to `assets/config/legacy/` with
     their registries pointed there, so no owned folder holds a root it does not own.
   - Deleted: `task-chains.xml`, `TaskChainRegistry`, their `ConfigValidator` checks.
   - Must pass: assets-tests loader errors, scenario 19 with new steps.
   - Depends on: 1, 8.
10. Named ladder.
    - Built: `<ladder>`; `classifyTier` maps to rungs by name (the existing five only).
    - Deleted: `<TaskTiers>`, `kTier*`, `readBaseTiers`.
    - Must pass: suite unchanged.
    - Depends on: 6, 9.
11. Action entries are the one source of action values.
    - Built: every shipped action as an entry with the factories' values; the factories read
      duration, interruptability, hands, and amounts from the entries; thought sources matched by
      action entry id, with `events.xml` and `work.xml` renamed to the entry ids (`human/eat`,
      `human/sleep`, `craft`, `build`).
    - Deleted: `legacy/action-types.xml`, `ActionTypeRegistry`.
    - Must pass: suite unchanged, scenarios 18 and 21.
    - Depends on: 9. Parallel with 10.
12. Facts and queries.
    - Built: the fact registry with scopes, ranges, and tuning from `facts/`; the definition cache; the
      per-definition spatial index in `Memory`; the nearest-first walk with horizons; rows including
      `prefer`; `fallback` rows; `pick`; `place="around"` and `place="grid"`; the claims pass-over;
      Reserved rows; the wait when everything is passed over.
    - Deleted: none.
    - Must pass: new `Queries.test.cpp` (10,000 memories, no allocation warm, determinism), the vision
      bench within noise.
    - Depends on: 9. Parallel with 10 and 11.
13. Primitive effects.
    - Built: the `apply*Effect` functions take values from entry effect rows; `pays` and `curve`;
      `upTo`; the `<craft>` completion service; the construction guards.
    - Deleted: none (the factories go with their paths).
    - Must pass: effect unit tests, suite unchanged.
    - Depends on: 11.
14. Chain runner.
    - Built: `TaskType::Chain`; chain fields on `Task` with their `clear()` rules; the one `do` start
      path; `go` through `headTo`; step end events; `have`; `run`, `think`, `need`, `drop`, and
      `leave_colony` steps.
    - Deleted: none.
    - Must pass: `Chains.test.cpp`, the 22 runner requirements on synthetic chains.
    - Depends on: 12, 13.
15. Chains in the decision.
    - Built: chain options; identity in `isOptionCurrentTask`; claims on steps sized at selection; one
      option for a chain several needs name, ranked by the needs it serves; the hands key; trace rows
      naming entries; every `TaskType::Chain` consumer listed under Seams.
    - Deleted: none.
    - Must pass: `Chains.test.cpp` decision cases, the validator on, `BM_WorkDecision` and
      `BM_ClaimIndexRebuild` within noise.
    - Depends on: 10, 14.

### Phase 2

16. Need entries load.
    - Built: the human behaviors' `<need>` entries without `<satisfiedBy>`; the human's includes;
      levels and drains read from entries; the costs from `needs.xml` as `<moodCost>` rows; the fixed
      array kept; `assets/config/thoughts/needs.xml` renamed to need ids (`hunger`).
    - Deleted: `needs.xml`, `NeedLevels`' fixed list, the decay literals.
    - Must pass: suite unchanged, scenario 19 rewritten.
    - Depends on: 9.
17. Enjoy nature (WOR-1054, owner-requested new behavior).
    - Built: `human/enjoying_nature`; need options from `<satisfiedBy>` for needs outside
      `kActionableNeeds`; the both-owners load check; the needs tab grouping by `<satisfiedBy>`.
    - Deleted: none.
    - Must pass: new tests (a bored colonist goes and gains; hunger pulls it away and it keeps the
      gain), scenario 21 updated, the new enjoy-nature scenario.
    - Depends on: 15, 16.
18. Needs as the creature's list.
    - Built: `NeedsComponent` as a list of capacity 16 with ids; mood, thoughts, conditions, the UI,
      the dev handler, and the developer-client dropdown over the creature's needs; masks per slot;
      `roundWidening` per query; `NeedType` kept as the old loop's handle.
    - Deleted: `kNeedLabels`, `needLabel`, the fixed array.
    - Must pass: a test of a mod folder adding a need to humans, suite unchanged.
    - Depends on: 16, 17 (both touch the needs loop).

### Phase 3

19. Wash.
    - Built: `human/washing`'s chain and action, its `<satisfiedBy>`.
    - Deleted: the Hygiene cases, `Action::Wash`.
    - Must pass: `GrubbyColonistWithNoKnownWaterDoesNothingAboutIt`,
      `GrubbyColonistWalksToKnownWaterAndWashes`, `ThirstyAndGrubbyColonistDrinksThenWashes`,
      `EquallyLowThirstAndHygieneAtOneShoreTieBreakToThirst`,
      `CriticalHygieneOnTheWayToDrinkWashesFirstAtTheSameShore`; scenario 21.
    - Depends on: 18.
20. Drink.
    - Built: `common/drinking_at_water`, with the Bladder effect.
    - Deleted: the Thirst cases, `Action::Drink`.
    - Must pass: `ThreeThirstyColonistsAllDrinkAtOneShorePoint`, `StartsDrinkActionOnArrival`,
      `DrinkFillsBladder`, `HungerClaimsTheBushItWalksToAndThirstClaimsNothing`.
    - Depends on: 19.
21. Relieving.
    - Built: `human/relieving` with the relief query (latrines, the grid, the fallback, `is_place`, the
      proximity sums), `seeking(need)`, the three actions, and `spawn`.
    - Deleted: the Bladder and Digestion cases, `Action::Toilet`, `findToiletLocation`.
    - Must pass: the `ToiletActionCreation_*` tests ported to the entries, `StartsToiletActionOnArrival`,
      `ToiletFullyRelievesBladder`, `PoopingSpawnsBioPile`, `PeeingDoesNotSpawnBioPile`,
      `BladderNeedUsesGroundFallback`, `UnreachableBedAndToiletFallBackToGround`, the relief-spot
      test, the relieving scenario. A test that depends on the on-arrival choice of both needs is
      ported to the choice at setting off, as the spec's Relief decided before setting off states, and
      its changed expectation is listed in the PR.
    - Depends on: 2, 20.
22. Sleep (pays at its end).
    - Built: `human/sleeping` with the bed-before-ground prefer row, `sleep_quality`, the `asleep`
      property, `ClaimIndex` reading it.
    - Deleted: the Energy cases, `Action::Sleep`, `ClaimIndex`'s Energy check.
    - Must pass: `StartsSleepActionOnArrival`, `SleepRestorationAffectedByQuality`,
      `WokenSleeperCarriesOutTheNewTask`, `EnergyNeedUsesGroundFallback`,
      `AWokenSleeperWhoseGoodsWereFetchedByAnotherDropsHisRecord`, the sleep scenario.
    - Depends on: 21.
23. Food services extracted (refactor).
    - Built: the meal amount, the free-before-held line, the nearest-of request among survivors, and
      `makeRoomToEat` as services the old hunger code calls.
    - Deleted: the inline copies of those services in the hunger code.
    - Must pass: suite unchanged, `BM_NeedDecision` within noise.
    - Depends on: 18. Parallel with 19 to 22 while it stays out of their switch cases.
24. Eat and gather food.
    - Built: the `take` group, `human/eating` (`eat_from_pack`, `fetch_food`, `food_to_eat`,
      `gather_food` as an idle row), the condition term `carried_food`, the meal resized at each
      decision.
    - Deleted: the needs loop, `needToCapability`, `getFirstActionDefName` for needs,
      `startNeedAction`, `kActionableNeeds`, `NeedType`, `Task::need*`, `NeedEffect` and
      `ConsumptionEffect`, the gather option and its sentinel in `classifyTier`.
    - Must pass: every hunger test in `ChainClaims.test.cpp`, `FoodSkipsEveryUnreachableNearerSource`,
      `AGonePileIsForgottenAndOtherFoodFound`, `EatWhoseFoodVanishedFinishesNothing`,
      `EatStartsOnArrivalAndFinishesOnceFed`, `HungerHarvestsTheSourceTheDecisionNamed`,
      `AMealGrowsWhenHungerTurnsCriticalOnTheWay`, the food-box and food-wait tests,
      `FindsANeedSourceInALargeMemory`, `GatheringFoodNeedsRoomForTheYield`,
      `AColonistGatheringKeepsItsBushWithAStoredBoxNearer`, `TurningHungryOnTheWayToGatherEatsTheNearerFood`,
      `GatherFoodRanksAboveWanderInIdleTier`; scenario 12; the need bench.
    - Depends on: 22, 23.
25. Urgent-need rung (behavior change).
    - Built: the `urgent_need` rung and the decision at once on urgent.
    - Deleted: none.
    - Must pass: new tests; the changed expectations of `TierDominanceWorkOrderBeatsNearerNeed` and its
      family and of the decide-at-once tests, listed in the PR.
    - Depends on: 24.
26. Sleep curve (behavior change).
    - Built: `pays="along" curve="2"` on `human/sleep`, the sleep factors in `sleep_quality`.
    - Deleted: none.
    - Must pass: a new test; `WokenSleeperCarriesOutTheNewTask`'s changed need values.
    - Depends on: 22.
27. Eating to full, at a place to eat (behavior change).
    - Built: open-ended `human/eat` to 95, `eat_a_meal`, `place_to_eat`.
    - Deleted: `eat_from_pack` and `fetch_food` as Hunger's rows.
    - Must pass: new tests; `EatStartsOnArrivalAndFinishesOnceFed`'s changed expectations; scenario 12
      updated.
    - Depends on: 24.
28. Food preference (behavior change).
    - Built: the food query's preference weights and `against_diet`, weighed and never excluded on, so
      a starving vegetarian who knows only meat eats it.
    - Deleted: none.
    - Must pass: a test of the stew example; `TheNearestReachableFoodWinsWhateverItsKind`'s changed
      expectations.
    - Depends on: 24.
29. Relief spots sampled (behavior change).
    - Built: `place="around"` sampling and nearest-proximity facts in `human/place_to_relieve`.
    - Deleted: `place="grid"` if nothing else uses it.
    - Must pass: the relief-spot test's changed expectations.
    - Depends on: 21.

### Phase 4

30. Wander and the idle put-down.
    - Built: `common/wandering` (the ring, the disc fit, every-frame decisions while walking, the hold
      with no option, no wander without a mesh, idle rows ranked in order); `putDownUnwantedArmful` as
      the idle hands rule. The wander in a world with no nav system is carried over, so
      `BM_IdleDecision` still runs.
    - Deleted: the wander option, `generateWanderTarget`, `TaskType::Wander`.
    - Must pass: `StoppedWanderRepicksFreshTarget`, `WanderingColonistDoesNotRepickAFailedRoute`,
      `WanderSequenceFollowsSeed`, `WanderPointsFitHisDisc`, `WanderTargetWithinRadius`,
      `WanderYieldsImmediatelyToActionableNeed`, `IdleHoldTakesLiveWanderOnceMeshLands`,
      `AnArmfulNobodyWantsIsPutDown`, `AnArmfulNobodyWantsIsPutDownToGatherFood`; scenario 13;
      `BM_IdleDecision`.
    - Depends on: 24.
31. Orders.
    - Built: `<order>` entries and a dev verb that issues one. There is no "move here" order; moving
      a colonist is direct control, which stays as it is.
    - Deleted: none.
    - Must pass: new tests for an order's chain and rung.
    - Depends on: 15, 21 (`relieve_here` performs `human/urinate`).
32. Colony membership.
    - Built: membership as engine state, granted when the colony spawns a human and removed by
      `<leave_colony/>`; the condition kind `colonist` as a human who is a member; colony work, the
      colonist UI, and sight counted for members. A human who is not a member keeps its mood,
      thoughts, and traits; only what the player sees follows membership.
    - Deleted: `kinds.xml`'s mover mapping for `colonist`.
    - Must pass: suite, scenario 18, a test that a human who is not a member is offered no colony work.
    - Depends on: 7, 15.

### Phase 5

33. Place packaged.
    - Built: the work entry, `install`, the set-down hands rule, `have` met by a packaged thing at its
      target.
    - Deleted: `evaluatePlacePackagedOptions`, `startPlacePackagedAction`, `setDownUnplacedFurniture`,
      PlacePackaged's `chainStep`, the `place*` fields.
    - Must pass: `PlacingAPackagedItemUsesItUntilPickedUp`,
      `FurnitureWhosePlacementIsCancelledIsSetDownPackaged`, `FurnitureSetDownOnASiteGoesToGroundOffIt`,
      `ReselectingAPlacementWhileCarryingGoesStraightToTheSpot`,
      `CarryingOnePackagedItemDoesNotMakeAnotherOneDelivering`, `PlacePackagedWithoutPickupKeepsHeldTool`,
      `PickupPackagedOfABoxAlreadyCarriedTakesNothing`, the `PlacePackaged*` and `PickupPackaged*` tests;
      scenarios 02 step 10 and 03.
    - Depends on: 30.
34. Deconstruct.
    - Built: `<work>` going down, with the guards.
    - Deleted: `evaluateDeconstructOptions`, Deconstruct in `startBuildAction`.
    - Must pass: the deconstruct tests in `BuildAction.test.cpp`, scenario 03 step 10.
    - Depends on: 33.
35. Build.
    - Built: the builder-spot query, slot claims, stand points, the demolish stop.
    - Deleted: `evaluateBuildOptions`, `startBuildAction`, `builderSlot`, `buildBlueprintEntityId`.
    - Must pass: `EachBuilderOfAFoundationHoldsItsOwnSpot`, `ABuilderDecidingAgainKeepsItsSpot`,
      `ANeedReplacingABuildFreesItsSpotForAnother`, `AWallsBuildTakesNoSpotAndNoClaim`,
      `AStoredSpotTheGoalNoLongerHasIsAFreshPick`, `ABuilderIsNotOfferedASiteOrderedDemolished`,
      `AnIdleColonistIsOfferedOnlyTheSiteNotOrderedDemolished`, every test in `BuildAction.test.cpp`,
      `ASwitchToBuildFreesTheHands`; scenarios 03, 04, 15; the bench.
    - Depends on: 34.
36. Work services extracted (refactor).
    - Built: the `for_goal` and `carried` amounts, the gathering rank, and the destination reach test,
      as services the old builders call.
    - Deleted: the inline copies of those services in the builders.
    - Must pass: suite unchanged, the bench.
    - Depends on: 30. Parallel with 33 to 35, in different regions of one file.
37. Work categories.
    - Built: `<category>` entries.
    - Deleted: `legacy/work-types.xml`, `WorkTypeRegistry`, `<WorkCategoryOrder>`.
    - Must pass: assets-tests.
    - Depends on: 9.
38. Site clear.
    - Built: `clear_site`.
    - Deleted: the clear branch in the harvest builder.
    - Must pass: `ABuildSiteClearIsNeverTheNextLegOfAGathering`, the `ClearHarvestTest` and
      `FootprintClearingTest` tests, scenario 03.
    - Depends on: 35, 36.
39. The gathering: cut, pile haul, and carry-in.
    - Built: the entries with `gathers`, the cargo handoff between tasks, the decision at once after a
      landed cut or a gathering pickup.
    - Deleted: `evaluateHarvestOptions`, the pile part of `evaluateHaulOptions`,
      `evaluateCarryInOptions`, `headToDeposit`, `carryHarvestIn`, `startHarvestAction`, the pile part
      of `startHaulAction`, `chainStep`, the haul and harvest fields.
    - Must pass: every gathering, haul, carry-in, provisions, suspended-record, and failed-route test
      in `ChainClaims.test.cpp`, the haul and harvest tests in `ActionSystem.test.cpp`,
      `HaulWhoseDepositIsNotConnectedIsNotPicked`, `OverweightColonistOffersNoHarvest`,
      `InterruptedTwoHandArmfulDropsOnceWithWholeQuantity`, `WorkGatesComeBeforeTheSources`,
      `HaulGoalsOfferEachPileOnce`, `HarvestGoalSeesEveryDefThatYieldsItsItem`; scenario 02; the bench.
    - Depends on: 38.
40. Box-to-box pull.
    - Built: `pull_from_box`, `below_destination`.
    - Deleted: the rest of the haul builder and start.
    - Must pass: the `Pull*` and `TwoHandPull*` tests, `BoxToBoxPullClaimsTheSourceBox`,
      `WithdrawPullsFromSourceBoxThenDepositsIntoDestBox`,
      `WithdrawTwoHandWoodRidesInHandsThenDepositsIntoDestBox`, `NoPullBetweenSamePriorityBoxes`,
      `NoPullFromHigherPriorityBox`; the pull scenario.
    - Depends on: 39.
41. Craft.
    - Built: the craft work entry, the fixed-duration `craft` action with the `<craft>` effect, the
      slot claim.
    - Deleted: the craft options, `startCraftAction`, `CraftingEffect`, the craft fields,
      `recordChainClaims`.
    - Must pass: `AStationsSlotIsHeldByTheCrafterThatSelectedIt`,
      `MultiUnitCraftWithFetchedAndHarvestedInputsCompletes`, `CraftFetchCountsTowardTheCraftRoot`,
      `TheLastUnitOfAnIngredientIsFetchedOnce`, `CraftDepositMetersToRecipeNeedAndKeepsSurplus`, the
      `Craft*` tests, `CraftingGoalSystem.test.cpp`; scenario 01.
    - Depends on: 40.
42. Interruptable crafts (behavior change, the AI and action half of WOR-822, Interruptable crafts:
    saved progress, what interrupts, and the crafter's hold on the station).
    - Built: craft progress on the job as `<work>`; interruptable craft, build, and deconstruct; the
      crafter's hold and the `own_unfinished_work` rung. WOR-822 on the board is re-scoped to what is
      not AI: the job's storage, the hold's UI, the bench's look.
    - Deleted: the fixed-duration `craft` duration.
    - Must pass: the WOR-822 tests; the changed expectations of the craft and build interruption tests,
      listed in the PR; scenario 01.
    - Depends on: 41.

### Phase 6

43. Low-mood behaviors.
    - Built: `<lowMood>` entries, the `low_mood` rung, the target query's weighted draw, the effect
      chain, the roll, the log-only chains, `count_of`, `per`, `mood_source`.
    - Deleted: none.
    - Must pass: the WOR-951 tests, scenario 18.
    - Depends on: 12, 15, 32. It can land beside phase 5: a low-mood behavior interrupts any task
      through the same switch gate.

### Phase 7

44. Delete the closed lists.
    - Built: nothing new.
    - Deleted: `TaskType`, `kTaskTypeNames`, `ActionType`, `kActionTypeNames`, the effect variant, the
      factories, the claim holder's task type, `EvaluatedOption::taskType` and `needType`, the rest of
      `classifyTier`, `ConfigValidator`'s work checks, `assets/config/legacy/`.
    - Must pass: suite, the full scenario list, the perf check.
    - Depends on: 31, 41, 43.
45. Docs to the code, and the development log entry.
    - Built: `config-loading.md`, `main-loop-timing.md`, `task-claims.md`'s function names,
      `colonist-task-arbitration.md`'s ladder, the development log entry.
    - Deleted: none.
    - Must pass: review.
    - Depends on: 44.

### Order

- The main line: 8, 9, 11, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 30, 33, 34, 35, 38, 39, 40,
  41, 44, 45.
- Before the main line, in parallel: 1, 2, 3, 4, 5, 6, 7 (8 waits on 5 and 7).
- Beside the main line:
  - 10 after 6 and 9, before 15; 12 after 9, beside 10 and 11.
  - 23 beside 19 to 22; 36 beside 33 to 35; 37 any time after 9.
  - 31 after 21; 32 after 15, before 43; 43 after 32, beside phase 5.
- Behavior changes, each after its path and the owner's sign-off: 25, 27, and 28 after 24; 26 after
  22; 29 after 21; 42 after 41.

## How regressions are caught

- The tests are the contract, ported by the rules above. A port that would need an expectation
  changed stops the path and goes to the owner.
- The happy-path scenarios in `docs/testing/scenarios/` named for each phase run as a manual regression
  pass before each path is committed, with the cheapest subagent fleet that can, each launch with
  `--background --no-settings`. A path that adds behavior adds its steps.
- The development-build claim validator (`ClaimIndex::findViolation`) runs after every AI update in
  every test, so claims named on steps are checked against a fresh rebuild from the first path moved.
- The decision hot path follows CLAUDE.md's timed-perf rules: `node scripts/machine-load.mjs` before and
  after, a capture scoped with `--zooms` and `--no-scroll` only when it reads `idle` or `mostly-idle`,
  the verdict from `node scripts/perf.mjs compare`, and a `Perf:` line in each PR. A worse number on a
  machine that was not idle is "worse under load, unconfirmed", and the nightly batch settles it.
  `BM_WorkDecision`, `BM_ClaimIndexRebuild`, `BM_NeedDecision`, and `BM_IdleDecision` run on every path
  that touches the decision, and the counted tests (`FindsANeedSourceInALargeMemory`,
  `WorkGatesComeBeforeTheSources`, `EachRemainingNeedIsWorkedOutOncePerUpdate`) keep their counts.

## Risks

- A lesson the old code learned is lost in the move. Guard: each path lists its tests and moves only
  with them passing unchanged; the spec's two requirement lists name the test for each lesson; review
  of each path reads the deleted code against the new chain before merging.
- Queries cost more than the hand-written loops on the decision hot path; an idle colonist walking a
  wander decides every frame and pays the full query set. Guard: the definition cache and nearest-first
  walk are built and benched in phase 1; the need and idle benches exist before anything moves; the
  perf rules above.
- The hunger, food-claims, and gather code share one block of about 400 lines. Guard: the food
  services are extracted first, as a refactor with no behavior change.
- Two runners meet mid-migration. Guard: the seams above; gather food moves with Eat; the gathering
  moves as one change; the pull moves after it; the validator runs throughout.
- Claims named on steps diverge from what `recordChainClaims` recorded for a shape. Guard: the
  `ChainClaims.test.cpp` tests assert the claim lists themselves
  (`PileHaulClaimsTheSourceThenCarriesWhatItLifted` and its siblings), and they run against each moved
  shape.
- Recreation becomes acted on, a 60 s activity on the ordinary-need rung above opportunistic work, so
  colony throughput changes after about five game days. Guard: the enjoy-nature scenario and the owner's
  sign-off of WOR-1054.
- Dev and UI readers of `kNeedLabels` and `needToFulfill` (`DevCommandHandler.cpp`, `SelectionAdapter`,
  `ColonistDetailsModel`, `EntityInfoView`, the developer client's `DevToolsPanel.tsx`) break when the
  fixed list goes. Guard: ticket 18 moves them before any need moves.
- The asset folder rename leaves a stale staged copy beside the exe. Guard: ticket 7 prunes the staged
  assets and checks the old definitions no longer load.
- Data errors in shipped entries reach players. Guard: every loader fails the whole file on any error
  and shows it on the loading scene (scenario 19); the tests load the shipped folders.

## Not in this plan

- Bumping, ownership, beds as slots, and mobile-target claims.
- The command-mode UI and context menus for player orders; phase 4 builds the orders and a dev verb.
- Lua: the `script` attribute stays reserved.
- Animals, the cow, and the care goal system.
- The facts that wait on systems not built (likes, diets, spoilage, relationships, ownership, fire,
  scarcity); they read 0 until their systems answer them.
- Save and load: there is no save system in `libs/sim`.
