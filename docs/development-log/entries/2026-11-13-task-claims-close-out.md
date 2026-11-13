# 2026-11-13 - Task claims close-out (WOR-820, WOR-811, WOR-634)

## Summary

The day set out to finish the epic "Goal-Driven Task Generation" (WOR-46). The first step was an audit of its open tickets against the current code and docs, since most were months old, and the audit changed the scope: two tickets were already built or superseded, one was parked, two moved to other epics. What remained became three pieces of work, all on one branch.

- A stale Sleep action that outlived the task it served (WOR-820, a bug found by the audit).
- Two headless benchmarks, and the collapse of the work options to one per goal, item, and source kind (WOR-811).
- The task panel's data: workers, quantities, who knows of a task, the all-claimed wait, and a per-colonist Known work list (WOR-634).

The decision cost at large memories fell by an order of magnitude and the trace is bounded by goals instead of remembered trees. Timings were all taken on a machine that was not idle; idle readings are still owed (see Next steps). `sim-tests` runs 1068 tests and `world-sim-tests` 12. All nine suites passed on the merged tree at `24985556`; one small commit followed it (`1e88eed2`), and an in-game regression pass was run after that, results in the PR.

## Details

### Audit and ticket outcomes

- "Phase 3: Two-Level Task/Reservation Model" (WOR-186) and "Phase 4: Memory Integration" (WOR-187): built or superseded by the claim index and the memory per-def lists. Closed on the board with evidence from the code.
- Bumping (WOR-632): parked by the owner. Nothing in the game can trigger a bump, so it stays out of the epic until something can.
- Craft station binding (WOR-633): moved to a new epic for interruptable crafts (WOR-822). The owner ruled that crafts must be interruptable, since large crafts may last days, and by ordinary needs as well as critical ones. The ladder today ranks queued crafts (tier 4) above ordinary needs (tier 5), so this changes arbitration, not only the Craft action; it needs saved progress on the job, a change to where a long craft sits against ordinary needs, and the binding. That is a crafting design with its own spec, not a claims step.
- The sim-cli scenarios (WOR-635): moved under the sim-cli epic (WOR-603), blocked on its step 10 (WOR-623), because `apps/sim-cli` does not exist yet.

### The stale Sleep action (WOR-820)

Sleep is the one interruptable action. When a critical need woke a colonist sleeping off a non-critical one, `AIDecisionSystem` switched the task but left the `Action` alone. `ActionSystem` starts an action only when none is active, so the stale Sleep ran on under the new task, and its completion cleared the new task undone (a toilet task ended with nothing done, a walk to water was cut short).

The fix clears the `Action` with the task on the switch path. It needed two more changes in how `isSameTask` tells tasks apart, both found by the test:

- A `FulfillNeed` task now compares the need being met. Needs met in place share the colonist's own spot as their target, so a sleeper taking an in-place toilet looked like the same task and kept its Sleep.
- The world origin is a target like any other. The old check skipped the comparison when both positions were near (0,0), so a colonist asleep at the origin never matched its own selection and restarted its sleep every tick.

The off-mesh snap (the colonist set back onto the nav mesh, which clears its task) had the same gap and now clears the action too, in both of its branches. The interruption-policy comment in `ActionSystem.cpp` and the note in `docs/technical/task-claims.md` were corrected to say what happens: non-interruptable actions run to completion, a switch clears the interruptable one, and an interrupted sleep pays out nothing.

Tests (`NeedActionInPlaceTest`): `WokenSleeperCarriesOutTheNewTask` (toilet in place, the action starts on the switch's own tick and finishes, a BioPile spawns, the sleep paid out nothing) and `WokenSleeperWalksToDistantNeedAndDrinks` (the Sleep is cleared, never comes back during the walk, and the drink on arrival is the new task's own). Both start away from the origin; the first version sat at (0,0), which hid the origin case.

### Benchmarks

Both live in `libs/sim/ecs/systems/AIDecision.bench.cpp`, headless, with no nav system, so every remembered source counts as reachable (as in the AI tests). Each scenario builds its own `World` and clears the goal registry.

- `BM_ClaimIndexRebuild` sets up 200 colonists across 300 goals, each holding a task with claims of the usual shapes: half a harvest carried in (Use on a tree, Inbound at the destination, InFlight toward the root), half a fetch from stock (SourceQuantity on a box, Inbound, InFlight), and every fourth with a suspended record. It times one warm `ClaimIndex::rebuild` and reports its mean, p99, and maximum. It also compares the data pointers and capacities of the index's entry, group, and table storage before and after each rebuild (`storageChanges`), which is how the "no allocation when warm" requirement is checked.
- `BM_WorkDecision` sets up N colonists each remembering the same M trees that yield wood, with three goals that want wood (a box stocking it by cutting, a craft whose recipe needs it, a build site that needs it). Every timed update re-decides every colonist, and the run fails if one did not. It reports the update's mean, p99, and maximum, the mean per colonist, and the first colonist's trace size: all options, the harvests among them, and the mean across colonists.

Limits: no nav mesh, so none of the route and reach cost a real decision pays is in these numbers; and the development build includes the claim index validator that the update runs, which a release build does not. The benchmarks measure the decision's own work, and neither replaces a game capture.

### The option collapse (WOR-811)

The problem was the trace and the decision's cost, both linear in remembered sources. With three wood goals, the option count for one colonist was 308 at 100 remembered trees, 3,008 at 1,000, and 15,008 at 5,000, because every goal emitted an option per tree. Each was built, scored, and sorted. After the change the count is 11 at every size.

Design, in three commits:

1. `6e4c546b`: shared sources, no shape change. `WorkSources` fills each item's source span once per decision and hands the same span to every goal that asks, with lazy per-entry answers (claim, regrowing, stand point, reach) worked out the first time any group needs them. The option score goes through one static function over its five terms, so the ranking and the option can never disagree.
2. `1997dbe6`: cuts and clears grouped. One option per cut goal and one per clear goal.
3. `a22f591a`: piles (loose hauls, craft fetches, build-site fetches) and box pulls grouped, with the claim docs.

Grouping key: (goal id, item, kind). The kinds are Cut (a harvest toward an item), Clear (a clear goal's footprint, whatever it yields), Pile (loose items, for a storage haul, craft fetch, or build-site fetch), and Pull (a box-to-box pull). Everything the arbitration compares except the trip distance, a cut's hysteresis, and the tiebreak id is constant inside a group (tier, gathering rank, storage priority bias, skill bonus, age bonus), so the groups rank against each other exactly as the per-source options did.

Ranking is off the trace. Each group ranks its candidates in a heap (`WorkGroup`, `RankedSource`) by the score each would have as its own option, with a total order (score, tiebreak id, key), so no option, string, claim probe, or nav lookup is made per candidate. The option shows the first candidate not taken whole by other colonists' claims. A group shows at most one Reserved row, for the first held candidate in rank order, and only when it ranks ahead of every offered one.

The walk's advance and reinsertion. When the shown source fails a test that depends on it (regrowing, nowhere to stand, a verdict, not connected), the group pops its next offered candidate, the option's source fields are rewritten and its bonuses recomputed, and the option moves to where it now ranks among the untested options (`upper_bound` over the sorted tail, then `rotate`); the loop tests the same index again. That is the per-source walk exactly: a fallback that drops below another goal's source lets that source go first, so selection is unchanged and only the trace's shape and the cost differ. The behaviour tests passed untouched, and the tests that pinned the per-source shape were rewritten (`FindsANeedSourceInALargeMemory`, `HarvestGoalSeesEveryDefThatYieldsItsItem`, `ClearHarvestOffersOnlyHarvestablesInsideItsFootprint`, `HaulGoalsOfferEachPileOnce`, `RegrowingSourceIsPassedOverForAReadyOne`). Two new tests pin the fallback and reinsertion order, `AIDecisionRiverTest.CutFallsBackPastAnUnreachableTreeInOneDecision` and `AIDecisionWallTest.HaulFallsBackPastAPileWithAVerdict`.

Routing stayed single-target plus a verdict. A nearest-of request for work would pick among one group's candidates by path, which can take one that ranks below another option, and adopting it means generalising the source choice, the claim re-record, and the held-against check to three task shapes. That changes which source wins. The route-failure path already records a verdict that the next decision passes over, so the open ticket's "retried forever" no longer held (it stopped holding with the nav entry 110 work). A livelock would need more than about ten sealed-but-connected sources nearer than the first open one under a constantly moving belief epoch, a dense grove for example; if that shows, a group's next few admitted candidates are the natural route choices for it, as a separate change.

The deposit-leg rule: a haul whose deposit leg has no known way fails the same way for every source in its group, so the option goes NoSource at once and none of the alternates is tried.

Deleted: `NearestReserved` and its three uses (the Reserved row comes from the rank), the per-source loops and push lambdas (replaced by one fill-from-candidate function per kind, used by the build and by the walk), the per-pile reach probe in the haul evaluator (reach is cached on the candidate), the walk's per-option NoSource branches, and the per-goal refill of the harvest and carry buffers. `m_routeEnds` now holds only non-candidate targets (deposits, stations, sites, need rounds), since candidates keep their reach on their own entry; it was growing with every source the walk tested. The "(planned)" dedup paragraph in `task-claims.md` and the "not deduplicated" line in `decision-trace.md` were rewritten to match.

Independent review, over the whole range, found no parity or claims bug. It found:

- An exhausted Regrowing option named the wrong source (the group's first candidate, not the regrowing one). The group now keeps the best-ranked candidate found regrowing (`regrowing`, replacing the `triedRegrowing` flag) and the option shows that one.
- Four paths without a test: regrowing, the deposit leg, box pulls, and a later pile taking over; tests added.
- A doc sentence that left out the tiebreak id from what differs inside a group, and a comment that named a function (`goalsWithOfferedWork`) already replaced.

Left as it is, deliberately:

- Three parity decisions kept for now, each a possible change of its own: the trace no longer summarises passed-over sources; beyond 60 m of trip the distance factor is 0, so the tie falls to the tiebreak id and the nearest far source is not the one chosen; a haul's hysteresis still ignores which pile.
- `workGroupOf` finds a group by a linear search. It runs on a failure and for tested Harvest options, over a few dozen groups.
- Reserved rows carry the age bonus and are chosen by rank, not trip. They are trace only and never selected, and the spec says so.

Measured under load (the machine-load check read `busy` on both sides): with one colonist the option and harvest counts were as designed at every tree count, the decision cost fell by about an order of magnitude at large memories, and nothing regressed. The plan guessed 6 harvest options and 14 options per colonist for eight colonists; the benchmark gave 3 to 5 and 11 to 13, so those guesses were off, not the design.

### The task panel (WOR-634)

What a row shows now (`docs/technical/task-claims.md`, Player-facing output; `docs/design/ui/screens/in-game.md`):

- Status, by precedence: Blocked, "Waiting (no source found)", Working, "Waiting (all known sources claimed)", then Available (detail "far", over 50 m) or Unassigned. A build site reads Needs materials, Building, or Ready to build.
- Workers: every colonist whose current task serves the row, two names joined with ", " and more as "+N". A cut or haul serves the goal it was picked through; a build, craft, or placement serves the entity it works, so the Build umbrella names its builders.
- Quantities: what the root needs and what is on its way, "needs 8, 4 on the way", a part left out at zero. A storage haul shows only "N on the way", because its basis is the box's free room, not a shortfall (a box with no maximum read "needs 1987").
- Known to: for a row nobody works, the colonists whose last decision offered work toward it ("known to Alice, Bob").
- All-claimed: some colonist held a Reserved option for the goal and none had a source chosen, available, blocked only by its own load, or regrowing.

`ColonyGoalWork` (`libs/sim/ecs/systems/ColonyGoalWork.{h,cpp}`) is built once per panel refresh by one pass over every colonist's task and last trace, so a row is a few lookups. It reads the claim index for "on the way" instead of summing claims itself, so the panel counts what the AI counts as coming. A first version summed the claims directly and showed a sleeping carrier's goods and a stranded cutter's expected yield, which the AI does not count. The panel keeps its own `ClaimIndex` per model (not a function-local static in the adapter) and rebuilds it per pass. `ColonyGoalWork` moved out of the registry's header into its own files under `systems/`, so the registry does not include a systems header.

`TaskRowText` (`apps/world-sim/scenes/game/ui/adapters/TaskRowText.{h,cpp}`) holds the status and detail as pure functions of a plain `TaskRowFacts` struct, so `world-sim-tests` links and tests them without the ECS.

The Known work tab in the colonist dossier lists the panel's rows for goals that colonist works or was offered, measured from the colonist, refreshed on the panel's 5 Hz interval and redrawn only when a row's status or detail changes (the first version rebuilt every frame and compared a row count).

Dropped from the spec's planned list:

- The count of available sources per root: Memory stores no yields, and counting sources would walk every colonist's memory on each refresh.
- "Destination full": a full box is hidden from the panel by design, so the reason would never show.
- "Station in use by Alice" and "bumped by": the craft binding and bumping are not built.

Two rounds of review changed the code after the first commit (`11d33ddf`):

- "On the way" moved to the claim index's rules, and "needs N" became net of it.
- All-claimed no longer fires when a free source exists but the colonist's own load is in the way (NoRoom), or when the wait is regrowth.
- A craft, build, teardown, or placement option counts as an offer toward its entity's goal, and a colonist's own work stays on its Known work tab.
- A worked storage goal counts as having work, so the Known work tab never lists a row the panel hides. A teardown shows on neither the panel nor the tab, and the spec says so.
- `ColonyGoalWork` was moved into its own files, and `TaskRowText` grew the status logic.
- Scenario 02 (`docs/testing/scenarios/02-stock-box-with-wood.md`) step 4 was wrong about when "on the way" appears, and now notes the panel's and the tab's two refresh timers.

### Files

- `libs/sim/ecs/systems/`: `AIDecisionSystem.{h,cpp}`, `ActionSystem.cpp`, `WorkSources.{h,cpp}`, `ColonyGoalWork.{h,cpp}` and tests, `AIDecision.bench.cpp`, `ChainClaims.test.cpp`.
- `libs/sim/ecs/`: `GoalTaskRegistry.{h,cpp}` and its test, `components/DecisionTrace.h`, `libs/sim/CMakeLists.txt`.
- `apps/world-sim/scenes/game/ui/`: `adapters/GlobalTaskAdapter.{h,cpp}`, `adapters/TaskRowText.{h,cpp}` and test, `components/GlobalTaskRow.{h,cpp}`, `dialogs/ColonistDetails{Dialog,Model}.{h,cpp}`, `models/GlobalTaskListModel.{h,cpp}`, `GameUI.cpp`.
- Docs: `docs/technical/task-claims.md`, `docs/design/game-systems/colonists/decision-trace.md`, `docs/design/ui/screens/in-game.md`, `docs/testing/scenarios/02-stock-box-with-wood.md`.

## Related Documentation

- [Task claims](../../technical/task-claims.md): "What is built", "Reserved rows, and option dedup", "Player-facing output", "Performance requirements"
- [Decision trace](../../design/game-systems/colonists/decision-trace.md)
- [In-game screen](../../design/ui/screens/in-game.md)
- [Task claims and colonist survival](./2026-11-11-task-claims-and-colonist-survival.md)

## Next steps

- Write the spec for interruptable crafts (WOR-822): what may interrupt a craft, saved progress, where a long craft sits against ordinary needs, and the station binding (WOR-633). Whether "ordinary needs interrupt" holds for builds too belongs in it.
- Take idle-machine readings of `BM_ClaimIndexRebuild` and `BM_WorkDecision` and record them in `task-claims.md`. Decide whether `sim` joins the nightly bench targets; it would pull in about 73 nav scenarios, and the nightly compares whole-run time, not the counters these benchmarks report.
- Open questions for the owner: whether a work option should say what it passed over ("2 nearer regrowing, 1 no known way"); whether the 60 m tie (nearest far source not chosen) deserves its own fix; whether a haul should stick to its pile like a harvest sticks to its tree; whether an interrupted sleep should restore something pro rata; and confirmation of the move of the sim-cli scenarios (WOR-635) under WOR-603.
- Noted, not fixed: `workGroupOf`'s linear search, and the trace no longer lists passed-over sources.
