# 2026-06-29 - Colonist task arbitration

## Summary

Replaced the drifted additive-float priority system with a lexicographic `(tier, score)` key,
restoring the categorical-first arbitration the design docs always specified. Three commits:
tier/score key + tier ladder + job-lifecycle fixes; TaskListView inspector; doc updates. 879
engine-tests green. Combined four-scenario flow (craft axe, stock box, chop-and-stock, build
foundation + walls) runs end-to-end with one colonist, zero phantom harvest-loops, zero
queued-job stalls.

## Details

### This is a restoration, not a new architecture

The `(tier, score)` key is what [Priority Configuration System](../../design/game-systems/colonists/priority-config.md)
always specified: wide numeric bands (Critical 30000, Needs 10000, Work 1000-5000, Idle 0)
that were categorical by construction. The code drifted to single-digit gaps (Build 41, Craft
40, Wander 10) where a ±50 distance bonus could swamp the category boundary. The fix brings
the code back to the documented design; the floor constants (`kWorkOrderProvisionFloor` (20),
`kStorageStockingFloor` (15)) were band-aids covering that drift and are now deleted.

### What the three commits changed

**Commit 1: (tier, score) key + tier ladder + job lifecycle**

- Added `tier: int` and `score: float` to `EvaluatedOption` in
  `libs/engine/ecs/components/DecisionTrace.h`; replaced `calculatePriority()` float return
  with a comparable `(tier, score)` key.
- Replaced `std::sort` comparator in `AIDecisionSystem::buildDecisionTrace` with
  lexicographic (tier asc, score desc, tiebreakId asc).
- Deleted `kWorkOrderProvisionFloor`, `kStorageStockingFloor`, `kWanderPriority`,
  `kGatherFoodPriority` floor constants.
- `chainBonus` (+2000 score inflation) deleted; any task with `servesActiveWorkOrder == true`
  is assigned tier 4 at evaluation time instead.
- `inProgressBonus` (+200 cross-tier) replaced by a within-tier hysteresis margin (50)
  applied only when comparing same-tier options.
- `distanceFactor` formula: `300 * max(0, 1 - d/60)` — yields 0-300, dominates skill (0-100)
  and age (0-100) so the nearest source reliably wins within a tier.
- 7-tier ladder loaded authoritatively from `assets/config/work/priority-tuning.xml
  <TaskTiers>` with fail-loud startup validation (unassigned task type = startup error).
- Harvest locate-by-entity-id: the harvest evaluator finds the target by its stored
  `harvestTargetEntityId`, not by a proximity re-search. Entity absent from memory or
  world → `NoSource`, not a re-queue to a random nearby entity. This kills the phantom
  "no harvestable entity found" loop.
- `StorageGoalSystem::reconcileStockingHarvests` retires a stocking-harvest when the storage
  minimum is already met or the source entity is gone — previously it looped indefinitely.

**Commit 2: TaskListView decision inspector**

- `DecisionTrace::lastEvaluationTime` is now written after each `buildDecisionTrace` call
  (it was declared but never assigned).
- `contentBuilt` cache in `TaskListView::update` loosened: re-renders when
  `lastEvaluationTime` advances, not only on colonist switch. Inspector updates live.
- Per-option render in `TaskListView::rebuildContent` now surfaces: tier label, within-tier
  score, bonus breakdown (distanceFactor/skill/age/hysteresis), `OptionStatus` badge.
- `selectionSummary` (populated at `AIDecisionSystem.cpp:1558`, never displayed before)
  shown as a summary line at the top of the trace panel.
- T hotkey toggles the inspector open.

**Commit 3: doc updates**

- `docs/technical/colonist-task-arbitration.md` status line updated.
- `docs/design/game-systems/colonists/ai-behavior.md` tier table and pseudocode reconciled.
- `docs/design/game-systems/colonists/priority-config.md` band roles, bonus descriptions,
  and formula sections updated.
- `docs/status.md` epic moved to completed, planned entry removed.
- `docs/testing/README.md` combined-flow caveat updated.
- This dev-log entry.

### Tier ladder

| Tier | Name                | Status           |
|------|---------------------|------------------|
| 1    | Survival / danger   | RESERVED         |
| 2    | Critical needs      | Implemented      |
| 3    | Forced player orders | RESERVED        |
| 4    | Active work orders  | Implemented, STICKY |
| 5    | Actionable needs    | Implemented      |
| 6    | Opportunistic work  | Implemented      |
| 7    | Idle                | Implemented      |

Tier 4 (active work orders) is STICKY: a colonist finishes a committed craft or build before
addressing a tier-5 need. A tier-5 need briefly interrupts tier 4 (eat, sleep), then the
colonist returns to finish. Only tier 2 (critical needs) outright preempts tier 4. This
implements the "finishing is very high priority" invariant from multi-colonist-crafting.md §2b.

## Acceptance

Combined four-scenario flow (craft axe → craft and stock box → chop-and-stock ~200 wood →
build wood foundation + walls) runs end-to-end with one colonist on the quickstart world.
Phantom harvest-loop: gone. Queued-job stall: gone. 879 engine-tests green (up from 872 in
the stabilization session; 7 new tests: lexicographic tier ordering, job end-conditions,
within-tier nearest preference, multi-unit craft completion).

## Related Documentation

- `docs/technical/colonist-task-arbitration.md` — the spec; now marked Implemented
- `docs/design/game-systems/colonists/ai-behavior.md` — tier table reconciled
- `docs/design/game-systems/colonists/priority-config.md` — bonus roles updated
- `docs/testing/README.md` — combined-flow caveat updated

## Follow-ups

- **Packaged-furniture lifecycle + findValidPositionNear**: crafted box still lands on the
  crafting station (cosmetic). The broader fix is `findValidPositionNear` placing dropped
  items off the station and off water; also covers items landing in rivers.
- **Stocking re-arm / discovery-gating**: a box won't fill to its full minimum unaided once
  the colonist exhausts the trees he's discovered — the storage goal system needs to re-arm
  harvest goals when the colonist discovers new trees. Discovery-gating follow-up.
