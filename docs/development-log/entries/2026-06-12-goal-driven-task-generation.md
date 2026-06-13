# 2026-06-12 - Goal-Driven Task Generation (core)

## Summary

Landed PR #115: task generation is now goal-driven instead of discovery-driven. Goals (storage wants items, recipe queued, furniture to place) create a bounded set of GoalTasks (~O(goals)) that colonists evaluate against their Memory, replacing the old model where VisionSystem created one task per discovered item (~O(items), thousands of invalid hauls with no destination). The PR was authored in January, sat as a draft, and was brought up to date with 5 months of main (worldgen, render overhaul, Windows port) before merging.

## Details

New components (from the original PR):
- `GoalTaskRegistry` (libs/engine/ecs/) - singleton goal store with destination/type/owner/parent indices
- `CraftingGoalSystem` - creates Craft + Harvest + Haul goal hierarchies for queued recipes, with Harvest -> Haul dependency unlock via `notifyGoalCompleted`
- `StorageGoalSystem` - Haul goals for storage containers with capacity
- `BuildGoalSystem` - PlacePackaged goals for furniture placement
- `AIDecisionSystem` evaluates goals from the registry against colonist Memory; `ActionSystem` records `deliveredAmount` on completion; `GlobalTaskAdapter` renders the goal hierarchy in the task list
- `GoalOwner` ownership system (second PR commit) fixed the infinite goal regeneration bug: StorageGoalSystem was reaping CraftingGoalSystem's child Haul goals via `removeGoalByDestination`, nuking Craft parents every cycle

Cleanup done at merge time (review found these before landing):
- Deleted the old `GlobalTaskRegistry` entirely (class + tests + GameLoadingScene wiring). The PR had removed its only producer (VisionSystem discovery notifications) but left the dead registry wired into scene lifecycle.
- Deleted the item-reservation API (`reserveItem`/`releaseItem`/`releaseAllForColonist`): it shipped with zero callers, so the anti-double-claim checks in AIDecisionSystem were always false. Removing it is behavior-neutral (pre-PR main had no working reservations either). Real reservations are a tracked follow-up (status.md Phase 3).
- `GoalTaskRegistry::Get().clear()` on game load; goals no longer persist across New Game (singleton lifecycle gap).
- Fixed index corruption in `createGoal` when a goal already exists for a destination (type/owner indices went stale; now re-indexed in place with stable IDs).
- Stripped the WIP debug tracing left from the January bug hunt, including per-query `LOG_DEBUG` and release-visible `LOG_INFO` in per-cycle paths.
- Added tests: cancel cascade (`removeGoalWithChildren`), job-removal reap, dependency unlock, delivery completion. Engine tests grew from 7 goal tests to 11.

Known limitations (deliberate, tracked in status.md):
- No item reservations: multiple colonists can target the same item/goal until deliveries land; first to arrive wins.
- `availableCapacity()` is target minus delivered only (no in-flight accounting).
- Colonist details "Tasks" tab shows all goals, not colonist-specific ones (was already effectively true; the filter read reservations that never existed).
- Memory push integration (Phase 4) not implemented; AIDecisionSystem polls Memory at decision time.

## Related Documentation

- Spec: `/docs/design/game-systems/colonists/task-registry.md` (PR #113)
- Architecture: `/docs/technical/task-generation-architecture.md`
- Note: the PR description cited `/docs/design/goal-driven-task-generation.md`, a path that never existed; the docs above are the real spec.

## Next Steps

- Wire item-level reservations into AIDecisionSystem/ActionSystem (status.md Phase 3), including release on task abandonment and colonist death
- Memory push integration (Phase 4)
- StorageGoalSystem/BuildGoalSystem lifecycle unit tests (only CraftingGoalSystem has direct coverage)
