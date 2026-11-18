# Behavior data migration

## Goal

Move everything a colonist does on purpose from hand-written C++ to needs, actions, queries, chains,
and work entries in XML, run on the kept engine, as [Queries and chains](./queries-and-chains.md)
specifies. Move one path at a time, each path's existing tests kept as the contract, and delete the
old code for a path in the same change that moves it.

## What exists now

All of it is on `main` (the docs branch `docs/thoughts-spec-decisions` is 14 commits behind and lacks
the Wash merge, PR 502).

- Needs: `NeedType` (eight values), `kNeedLabels`, `NeedsComponent` with a fixed array and
  `kActionableNeeds` (six since Wash), decay rates as literals in `libs/sim/ecs/components/Needs.cpp`,
  thresholds in `assets/config/colonists/needs/needs.xml` read by `NeedLevels`, mood weights in
  `assets/config/colonists/mood.xml` (`<NeedWeights>`, still an average).
- Options: `AIDecisionSystem.cpp` (about 4,000 lines). The needs loop with its hunger special case,
  `needToCapability`, `getFirstActionDefName`, `taskFirstActionNeedsHands`, `findToiletLocation`
  (`components/ToiletLocationFinder.cpp`), `evaluateHaulOptions`, `evaluateCarryInOptions`,
  `evaluateHarvestOptions`, `evaluateBuildOptions`, `evaluateDeconstructOptions`,
  `evaluatePlacePackagedOptions`, the craft station options, wander and gather food, `classifyTier`,
  `selectTaskFromTrace`, `handleChainInterruption`, `acceptClaims`, `putDownUnwantedArmful`,
  `setDownUnplacedFurniture`.
- Actions: `components/Action.h` (`ActionType`, the factories, the `ActionEffect` variant),
  `systems/ActionSystem.cpp` and `systems/action/{NeedActions,HarvestActions,HaulActions,CraftActions,BuildActions}.cpp`
  (the start routines, `headToDeposit`, `headTo`, `carryHarvestIn`, `makeRoomToEat`, the `apply*Effect`
  functions).
- Claims: `systems/ChainClaims.{h,cpp}` (`recordChainClaims` per chain shape), `ClaimIndex.{h,cpp}`,
  `Task.h` (claims, `SuspendedWork`, the per-type fields, `chainStep`, `deliveryChosen`).
- Goals: `GoalTaskRegistry.{h,cpp}` (`GoalTask::type` is a `TaskType`), `StorageGoalSystem`,
  `CraftingGoalSystem`, `BuildGoalSystem`, `ConstructionSystem`, `ColonyGoalWork.cpp` for the panel.
- Config: `assets/config/actions/action-types.xml` (`ActionTypeRegistry`; only `needsHands` of a
  task's first action is read, and Wash, Withdraw, PickupPackaged, and PlacePackaged have no entry),
  `assets/config/work/task-chains.xml` (`TaskChainRegistry`, read by nothing but `ConfigValidator`),
  `assets/config/work/work-types.xml` (`WorkTypeRegistry`, same), `assets/config/work/priority-tuning.xml`
  (`<TaskTiers>` read; `<WorkCategoryOrder>` parsed and unused).
- Tests: `ChainClaims.test.cpp` (about 100 flow tests of claims, gathering, hunger, hands),
  `AIDecisionSystem.test.cpp`, `ActionSystem.test.cpp`, `BuildAction.test.cpp`, `ClaimIndex.test.cpp`,
  `GoalTaskRegistry.test.cpp`, `StoreOrDrop.test.cpp`, the goal-system suites, and the benches in
  `AIDecision.bench.cpp`.

## Phases

Ordered by risk, least first: the engine is proven on new, solo behavior before anything stable moves,
needs move before work, and the claims-heavy work moves last.

### Phase 1: the engine

Built: the query system (candidates, facts, rows, `pick`, the evaluation rules, the definition cache
and per-definition spatial index), the chain runner on `Task` (step index, resolved targets, count,
start time; one start path for `do` steps through `ActionSystem`), action entries and the primitive
effects (reshaping the `apply*Effect` functions to take values from an entry), the loaders for
`assets/config/actions/`, `queries/`, `chains/`, and `creatures/`, with their load errors on the
loading scene, and the named ladder (`<Tiers>` in `priority-tuning.xml`) replacing the bare tier
numbers.

Proves: the 16 runner requirements, each on a synthetic chain in a new `Chains.test.cpp`, and query
evaluation at 10,000 remembered items (the definition cache, the nearest-first walk, no allocation
warm).

Depends on: nothing.

Exit criteria:
- New tests for the runner requirements and the loaders' errors pass.
- Every existing `sim-tests` and `assets-tests` test passes unchanged (nothing has moved yet).
- Scenarios 18 (colonist thoughts) and 19 (load errors on screen) pass in game.
- `BM_WorkDecision` and `BM_ClaimIndexRebuild` are within noise of `main` (they should not move).

Deleted at the end: `action-types.xml` and `ActionTypeRegistry`, with `taskFirstActionNeedsHands`
reading the new action entries' `hands` (one hands source); `task-chains.xml`, `TaskChainRegistry`,
and their `ConfigValidator` checks; the `<TaskTiers>` numbers and the bare tier constants
(`kTierIdle` and the rest) in favour of the named rungs.

The game can now: load and validate actions, queries, and chains from data, and show their errors.

### Phase 2: needs as data, and enjoy nature

Built: need entries in `assets/config/needs/` (levels, `<drains>`, `<setBy>`, `moodCost`,
`<satisfiedBy>`), the colonist creature kind, `NeedsComponent` as the creature's own list of need ids
and values, need ids interned at load, the condition vocabulary's `need` set and the thought triggers
filled from the loaded needs, mood reading each need's `moodCost` with the sum rule. Then the first
user, Recreation through `enjoy_nature` (WOR-1054, enjoy nature, paused until this phase): the
`pleasant_spot` query, a go step, and the interruptable `EnjoyNature` action that pays along.

The old need code keeps running for the six acted-on needs in this phase, addressing them by the id the
loader gave each name, not by `NeedType`; that is a mechanical swap of the index, not a second path.

Proves: needs as data with no change in behavior for the existing needs, and the engine running a
need end to end: decided at the need's tier, interrupted by a higher one, keeping what it earned.

Depends on: phase 1.

Exit criteria:
- Every existing test passes unchanged, the need tests included (`FullNeedFulfillmentCycle`,
  `NeedRestoredOnCompletion`, `DrinkFillsBladder`, the thought trigger tests in `ThoughtSourceSystem.test.cpp`,
  `MoodSystem.test.cpp`). Mood's move from weights to summed costs (needs.md) is its own change with
  its own tests, landed before this phase or as its first commit, so no test here changes what it
  expects.
- New tests: a colonist below Recreation's seek level goes to a spot and gains Recreation; a hungry
  colonist pulls it away and it keeps what it gained; a mod file adding a need loads and costs mood.
- Scenarios 12 (colonists eat), 18, 21 (colonists wash) pass; a new scenario for enjoying nature is
  added to `docs/testing/scenarios/` and passes.

Deleted at the end: `NeedType`, `kNeedLabels`, `needLabel`, the fixed array, the decay literals in
`Needs.cpp`, `assets/config/colonists/needs/needs.xml` and `NeedLevels`' fixed-list check,
`<NeedWeights>` in `mood.xml`.

The game can now: show colonists enjoying nature when bored, and take a new need from a mod file.

### Phase 3: the existing needs, one at a time

Each need moves in its own change: its chain and queries, its action entry, its option at the need's
tier through `<satisfiedBy>`, and in the same change its case is deleted from the needs loop,
`needToCapability`, `getFirstActionDefName`, `startNeedAction`, and the `Action` factories.

1. Wash. The newest and simplest: a shore query, a 3 s action, no claims, no item. It is the template
   the others copy. Tests, from PR 502: `GrubbyColonistWithNoKnownWaterDoesNothingAboutIt`,
   `GrubbyColonistWalksToKnownWaterAndWashes`, `ThirstyAndGrubbyColonistDrinksThenWashes`,
   `EquallyLowThirstAndHygieneAtOneShoreTieBreakToThirst`,
   `CriticalHygieneOnTheWayToDrinkWashesFirstAtTheSameShore`. Scenario 21.
2. Drink. The same water query, the Bladder side effect, many colonists at one point. Tests:
   `ThreeThirstyColonistsAllDrinkAtOneShorePoint`, `StartsDrinkActionOnArrival`, `DrinkFillsBladder`,
   `HungerClaimsTheBushItWalksToAndThirstClaimsNothing`.
3. Toilet. The relief query with `on_shore`, `near_waste`, and `near_food` replacing
   `findToiletLocation`; the latrine kind; the two `when` rows and `serves(need)`; the bio pile spawn.
   Tests: the `ToiletActionCreation_*` tests (ported to the entry, same assertions),
   `StartsToiletActionOnArrival`, `ToiletFullyRelievesBladder`, `PoopingSpawnsBioPile`,
   `PeeingDoesNotSpawnBioPile`. Report the Toilet duration change (poop alone 4 s to 3 s) to the owner
   before merging.
4. Sleep. The bed-or-ground query, `sleep_quality`, interruptable, paying along its curve. Depends on
   the sleep curve of WOR-822 (Interruptable crafts: saved progress, what interrupts, and the
   crafter's hold on the station) if that has not landed; the curve is built here if it has not.
   Tests: `StartsSleepActionOnArrival`, `SleepRestorationAffectedByQuality`,
   `WokenSleeperCarriesOutTheNewTask`, and the sleep release of suspended records
   (`AWokenSleeperWhoseGoodsWereFetchedByAnotherDropsHisRecord`, which still uses old work code).
5. Eat. Last, because food choice and food claims are the most involved: one query over bushes,
   piles, and boxes with the nearest-of route request across kinds, the `Take` action group, the
   `meal` amount, `makeRoomToEat` before a take, the eater-urgency line between taken and held,
   `SourceReachMemo::FoodWait`, and provisions. Tests: every hunger test in `ChainClaims.test.cpp`
   (from `OnlyAnotherEaterHoldsAHungryColonistsFood` through `AHaulerArrivingAfterAHungryColonistAteEndsItsHaul`),
   `FoodSkipsEveryUnreachableNearerSource`, `AGonePileIsForgottenAndOtherFoodFound`,
   `EatWhoseFoodVanishedFinishesNothing`, `EatStartsOnArrivalAndFinishesOnceFed`,
   `HungerHarvestsTheSourceTheDecisionNamed`, `FindsANeedSourceInALargeMemory`. Scenario 12.

Proves: the engine carries every need the colonists have, food claims included.

Depends on: phase 2; Sleep on the sleep curve.

Exit criteria: for each need, its listed tests pass with their assertions unchanged (a test that
constructs an `Action` by factory is ported to build it from the shipped entry, asserting the same
values); the full `sim-tests` suite passes; scenarios 12 and 21 pass; the AI update's cost against
`main` is checked as below after Eat.

Deleted at the end (Eat being the last): the needs loop, `needToCapability`, `getFirstActionDefName`
for needs, `startNeedAction`, `kActionableNeeds`, `Task::needToFulfill` and the `need*` fields,
`NeedEffect` and `ConsumptionEffect` in favour of the primitives, `findToiletLocation`.

The game can now: run every need from data, so a mod changes how colonists eat, drink, sleep, wash, or
relieve themselves without code.

### Phase 4: idle behavior and player orders

Built: `wander` and `gather_food` as idle chains on the colonist kind (`place="around"`, the
`carried_food` term), the put-down of an unwanted armful as the idle rung's hands service, and
`<order>` entries at the player-order rung with a dev verb that issues one, so orders are testable
before the command-mode UI exists.

Proves: chains with a `pick="weighted"` query and no claims run at the idle rung, and a chain can be
given its target from outside.

Depends on: phase 3 (gather food shares the food query's sources and the bush Use claim).

Exit criteria: `StoppedWanderRepicksFreshTarget`, `WanderingColonistDoesNotRepickAFailedRoute`,
`GatheringFoodNeedsRoomForTheYield`, `AColonistGatheringKeepsItsBushWithAStoredBoxNearer`,
`AnArmfulNobodyWantsIsPutDownToGatherFood`, `TurningHungryOnTheWayToGatherEatsTheNearerFood` pass
unchanged; new tests for an order's chain and its tier; scenario 13 (colonists walk round obstacles)
passes.

Deleted at the end: the wander and gather-food options, the gather-food sentinel in `classifyTier`,
`TaskType::Wander`'s handling.

The game can now: issue a player order through the dev tools, and run idle behavior from data.

### Phase 5: colony work, one path at a time

Each path moves in its own change, with its claims named on steps and its option from a work entry; the
evaluator and start routine it replaces, its chain shape in `recordChainClaims`, and its per-type
`Task` fields go in the same change. Order, simplest first:

1. Place packaged. One Use claim, two legs, a packaged item and its set-down rule, no amounts. Tests:
   `PlacingAPackagedItemUsesItUntilPickedUp`, `FurnitureWhosePlacementIsCancelledIsSetDownPackaged`,
   `FurnitureSetDownOnASiteGoesToGroundOffIt`, `ReselectingAPlacementWhileCarryingGoesStraightToTheSpot`,
   `CarryingOnePackagedItemDoesNotMakeAnotherOneDelivering`, the `PlacePackaged*` and `PickupPackaged*`
   tests in `ActionSystem.test.cpp`. Deletes `chainStep`'s PlacePackaged use and
   `evaluatePlacePackagedOptions`.
2. Deconstruct. No claim, an open-ended work action counting down. Tests: the deconstruct tests in
   `BuildAction.test.cpp`. Deletes `evaluateDeconstructOptions`.
3. Build. Numbered slot claims on builder spots, the stand point service, the demolish stop. Tests:
   `EachBuilderOfAFoundationHoldsItsOwnSpot`, `ABuilderDecidingAgainKeepsItsSpot`,
   `ANeedReplacingABuildFreesItsSpotForAnother`, `AWallsBuildTakesNoSpotAndNoClaim`,
   `AStoredSpotTheGoalNoLongerHasIsAFreshPick`, `ABuilderIsNotOfferedASiteOrderedDemolished`,
   `AnIdleColonistIsOfferedOnlyTheSiteNotOrderedDemolished`, every test in `BuildAction.test.cpp`,
   `ASwitchToBuildFreesTheHands`. Scenarios 03, 04, 15. Deletes `evaluateBuildOptions`,
   `startBuildAction`, `Task::builderSlot` and `buildBlueprintEntityId`.
4. Box-to-box pull. Source quantity, inbound room, in-flight, the two-hand delivery guard, and the
   suspended record, with no gathering. Its record is adopted by the carry-in, still old code until
   step 5; the record is the kept `SuspendedWork`, so the two meet on the engine's structure, not on
   either runner. Tests: the `Pull*` and `TwoHandPull*` tests in `AIDecisionSystem.test.cpp`,
   `BoxToBoxPullClaimsTheSourceBox`, `WithdrawPullsFromSourceBoxThenDepositsIntoDestBox`,
   `WithdrawTwoHandWoodRidesInHandsThenDepositsIntoDestBox`, `NoPullBetweenSamePriorityBoxes`,
   `NoPullFromHigherPriorityBox`. Scenario 02.
5. The gathering: the cut for a goal, the site clear, the pile haul, and the carry-in, together,
   because a gathering hands its cargo from task to task under one instance id and that handoff must
   not span two runners. Tests: every gathering, haul, carry-in, provisions, suspended-record, and
   failed-route test in `ChainClaims.test.cpp`, the haul and harvest tests in `ActionSystem.test.cpp`
   (`HarvestBecomesItsDepositLegAndDeliversTheYield`, `PickupFromAGoneSourceEndsTheHaul`,
   `LooseFetchPicksUpWhenSourceOverlapsStation`, `CarryingAtSourceRedirectsToDepositInsteadOfDeadlock`
   and the rest), `HaulWhoseDepositIsNotConnectedIsNotPicked`, `OverweightColonistOffersNoHarvest`,
   `InterruptedTwoHandArmfulDropsOnceWithWholeQuantity`, `WorkGatesComeBeforeTheSources`,
   `HaulGoalsOfferEachPileOnce`, `HarvestGoalSeesEveryDefThatYieldsItsItem`. Scenarios 02, 11, 03.
   Deletes `evaluateHarvestOptions`, `evaluateHaulOptions`, `evaluateCarryInOptions`,
   `headToDeposit`, `carryHarvestIn`, `startHarvestAction`, `startHaulAction`, `chainStep`, and the
   haul and harvest fields on `Task`.
6. Craft with its carry-ins. The station slot, the metered deposit, the own-unfinished-work rung, the
   open-ended Craft action. The carry-ins already run as chains from step 5. Depends on WOR-822 having
   moved craft progress onto the job, since the action entry is open-ended work on the job. Tests:
   `AStationsSlotIsHeldByTheCrafterThatSelectedIt`, `MultiUnitCraftWithFetchedAndHarvestedInputsCompletes`,
   `CraftFetchCountsTowardTheCraftRoot`, `TheLastUnitOfAnIngredientIsFetchedOnce`,
   `CraftDepositMetersToRecipeNeedAndKeepsSurplus`, the `Craft*` tests in `ActionSystem.test.cpp`,
   `CraftingGoalSystem.test.cpp`. Scenario 01. Deletes the craft station options, `startCraftAction`,
   `CraftingEffect`, `Task::craftRecipeDefName` and `targetStationId`, and `recordChainClaims`.

Proves: the claims, supply, hands, and legs services carry every work shape the colony has, with the
validator watching every update.

Depends on: phase 4 (the idle put-down runs before any idle chain), and WOR-822 for the craft.

Exit criteria, per path: its listed tests pass with their assertions unchanged; `ClaimIndex.test.cpp`
and the validator pass on every AI update in the suite; its scenarios pass in game; after steps 3, 5,
and 6, the decision's cost is checked as below. At the end of the phase the colony work requirements
in the spec (1 to 31) each have their tests passing.

Deleted at the end: the work option builders, the start routines, `recordChainClaims`, `chainStep`,
`deliveryChosen` as a `Task` field (it becomes chain state), the per-type `Task` fields, and the work
rules in `classifyTier`; `work-types.xml` and `WorkTypeRegistry`, with the categories in
`assets/config/work/`.

The game can now: run all colony work from data, so a mod adds a kind of work with entries, and
`GoalKind` care exists for needs others meet.

### Phase 6: low-mood behaviors

Built: behaviors as entries with a target query and an effect chain, the behavior rung below critical
needs, `pick="weighted"` targets with `mood_source`, `count_of(query)` and `per` on weight rows, and the
log-only one-step chains for behaviors whose systems do not exist. This is WOR-951 (behavior definitions
as data) under WOR-919 (Colonist mood: low-mood behaviors, refusal, and colony failure).

Proves: the engine runs behaviors that were designed for it, on top of a colony whose every other
action already runs on it.

Depends on: phase 5, so a behavior never interrupts a hand-written chain whose interruption rules
differ from the runner's.

Exit criteria: the behavior tests WOR-951 defines; every suite passes; scenario 18 passes.

Deleted at the end: nothing old; the behavior code is new.

The game can now: show low-mood episodes acted out in the world.

### Phase 7: retire what is left

Deleted: `TaskType` and `assets::kTaskTypeNames` (the condition vocabulary's `task` set filled from
chain ids and task groups listing chain ids), `ActionType` and `kActionTypeNames`, the `ActionEffect`
variant and the factories, `GoalTask::type` as a `TaskType` (replaced by `GoalKind`), `ClaimHolder`'s
task type, `EvaluatedOption::taskType` and `needType` (the trace names entries), whatever of
`classifyTier` is left, and `ConfigValidator`'s remaining work checks. Docs brought to the code:
`config-loading.md`, `main-loop-timing.md`, `task-claims.md`'s function names, `colonist-task-arbitration.md`'s
ladder, and the development log entry.

Proves: one path; nothing names a behavior in C++.

Depends on: phases 1 to 6.

Exit criteria: the full suite (all nine test executables) passes; scenarios 01, 02, 03, 04, 11, 12, 13,
15, 18, 19, 21 and the enjoy-nature scenario pass; the perf check below.

The game can now: do nothing new; the code can no longer drift back to hand-written behaviors.

## How regressions are caught

- The tests are the contract. `ChainClaims.test.cpp`, `AIDecisionSystem.test.cpp`,
  `ActionSystem.test.cpp`, `BuildAction.test.cpp`, `ClaimIndex.test.cpp`, `GoalTaskRegistry.test.cpp`,
  `StoreOrDrop.test.cpp`, and the goal-system suites keep their assertions. A test whose setup builds
  an `Action` by factory, sets a `TaskType`, or reads a per-type `Task` field is ported to build the
  same thing from the shipped entries and read the chain's state; its expectations do not change. A
  port that would need an expectation changed stops the path and goes to the owner.
- The happy-path scenarios in `docs/testing/scenarios/` run as a manual regression pass before each
  path is committed, with the cheapest subagent fleet that can, each launch with `--background
  --no-settings`. A path that adds behavior adds its steps.
- The development-build claim validator (`ClaimIndex::findViolation`) runs after every AI update in
  every test, so claims named on steps are checked against a fresh rebuild from the first path moved.
- The decision hot path follows CLAUDE.md's timed-perf rules: `node scripts/machine-load.mjs` before and
  after, a capture scoped with `--zooms` and `--no-scroll` only when it reads `idle` or `mostly-idle`,
  the verdict from `node scripts/perf.mjs compare`, and a `Perf:` line in each PR. A worse number on a
  machine that was not idle is "worse under load, unconfirmed", and the nightly batch settles it.
  `BM_WorkDecision` and `BM_ClaimIndexRebuild` run on every path that touches the decision, and the
  counted tests (`FindsANeedSourceInALargeMemory`, `WorkGatesComeBeforeTheSources`,
  `EachRemainingNeedIsWorkedOutOncePerUpdate`) keep their counts.

## Risks

- A lesson the old code learned is lost in the move. Guard: each path lists its tests and moves only
  with them passing unchanged; the spec's two requirement lists name the test for each lesson; review
  of each path reads the deleted code against the new chain before merging.
- Queries cost more than the hand-written loops on the decision hot path. Guard: the definition cache
  and nearest-first walk are built and benched in phase 1, before any stable path depends on them; the
  benches and counted tests run on every path; the perf rules above.
- Two runners meet mid-migration (a record made by a new pull adopted by an old carry-in). Guard: the
  suspended record and the claim index are kept engine structures both sides use; the gathering family
  moves as one change so no cargo handoff spans two runners; the validator runs throughout.
- Claims named on steps diverge from what `recordChainClaims` recorded for a shape. Guard: the
  `ChainClaims.test.cpp` tests assert the claim lists themselves (`PileHaulClaimsTheSourceThenCarriesWhatItLifted`
  and its siblings), and they run against each moved shape.
- The tier ladder changes under the migration (WOR-822's nine rungs, the behavior rung). Guard: named
  rungs from phase 1, so a rung added later moves no entry.
- Data errors in shipped entries reach players. Guard: every loader fails the whole file on any error
  and shows it on the loading scene (scenario 19); the tests load the shipped folders.
- Save and load arrive mid-migration. Guard: chain progress is engine state on the task, the same shape
  for every chain, so a save writes one structure whatever path has moved.

## Not in this plan

- Interruptable crafts (WOR-822) itself, beyond the dependencies named above.
- Bumping, ownership, beds as slots, and mobile-target claims beyond what the cow example needs.
- The command-mode UI and context menus for player orders; phase 4 builds the orders and a dev verb.
- Lua: the `script` attribute stays reserved.
- Animals and the cow: the care goal system is built in phase 5 only if an animal exists by then.
- The facts that wait on systems not built (likes, diets, spoilage, relationships, ownership, fire,
  scarcity); they read 0 until their systems answer them.
- Tuning: thresholds, costs, and query weights are carried over at their current values; retuning is
  separate work.
- Splitting the work into Specboard items; that follows the spec's review.
