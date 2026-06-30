# Colonist Task Arbitration

Status: Spec (planning)
Created: 2026-06-29

This spec **restores the documented decision hierarchy** — it is not a new architecture. The
categorical tier ordering was already designed in
[Colonist AI Behavior](../design/game-systems/colonists/ai-behavior.md) (a fixed tier order,
higher tiers preempt lower) and
[Priority Configuration System](../design/game-systems/colonists/priority-config.md) (wide
numeric bands: Critical 30000, PlayerDraft 20000, Needs 10000, Work 1000-5000, Idle 0 —
categorical by construction). The code drifted: those bands collapsed into single-digit gaps
(Build 41, Craft 40, Wander 10) where a ±50 distance bonus plus skill, chain, and in-progress
bonuses swamp the category gaps. The floor constants `kWorkOrderProvisionFloor` (20) and
`kStorageStockingFloor` (15) are band-aids pinning specific orderings the gaps can no longer
enforce. That drift is the arbitration bug. The **(tier, score)** key restores the documented
categorical intent as a type guarantee: tier compared first, inviolable; score orders within a
tier only.

The gameplay intent is already documented in
[Colonist AI Behavior](../design/game-systems/colonists/ai-behavior.md) and
[Decision Trace](../design/game-systems/colonists/decision-trace.md); this doc scopes the
engine changes and records the locked design decisions.

**MVP Status:** See [MVP Scope](../design/mvp-scope.md). This epic is core plumbing;
it lands no new gameplay features but fixes several colonist reliability bugs.

---

## Summary

Replace `DecisionTrace::calculatePriority()` (a single additive float) with a
lexicographic **(tier: int, score: float)** key. Tier is categorical and inviolable;
score orders options within the same tier only. A far high-tier task always beats a
near low-tier one. The floor constants `kWorkOrderProvisionFloor` and
`kStorageStockingFloor` are deleted; the `chainBonus` (+2000) becomes a tier
classification, not a score inflation; `inProgressBonus` becomes a within-tier
hysteresis margin.

This fixes: the harvest loop ("no harvestable entity found" stale-target cycle); "colonist
won't reliably finish the queued job"; the "colonist chops a tree 70 m away over an
adjacent one" efficiency issue; the multi-unit craft stall (deliveredAmount staleness); and
the category-ordering fragility where adding a new task type can corrupt global ordering.

---

## Why

`calculatePriority()` returns one float:

```
base_tier + distanceBonus(-50..+50) + skillBonus(0..+100)
          + chainBonus(+2000) + inProgressBonus(+200) + taskAgeBonus(0..+100)
```

[Priority Configuration System](../design/game-systems/colonists/priority-config.md)
specified wide bands — 10000-unit gaps between categories — so the distance and skill terms
(small relative to 10000) could never cross a category boundary. The code shrank those gaps
to single digits (Build 41, Craft 40, Wander 10), inverting that guarantee. Now a large
distance or skill term swamps the gaps. The `kWorkOrderProvisionFloor` (20) and
`kStorageStockingFloor` (15) constants were added to pin specific orderings the gaps can't
enforce on their own. Adding a task type today requires reasoning about the entire float
range to avoid breaking existing ordering.

The reference genre uses categorical-first arbitration throughout: RimWorld's think-tree
walks a fixed priority tree and never scores across categories; Oxygen Not Included sorts
on a lexicographic (priority-class, sub-priority, distance) key; Going Medieval uses
discrete ordered levels. None uses a single additive float.

---

## Scope

**In this epic**

- The **(tier, score)** key: data model, comparison, sort, and selection in
  `AIDecisionSystem` + `DecisionTrace`.
- Mapping current task types onto the new tier ladder; deletion of the floor constants.
- Within-tier score: strong nearest-reachable preference, skill, task age, and stickiness
  hysteresis for the in-progress option.
- Job end-conditions and target re-resolution (fixes harvest loop + queued-craft
  abandonment + multi-unit craft stall) in `CraftingGoalSystem`, `ConstructionSystem`,
  `StorageGoalSystem`, and the harvest locate-by-entity path.
- Tiers as task-type data (tier is a property of a task definition, not an if-chain in C++).
- `TaskListView` inspector enhancement: surface tier, within-tier score, bonus breakdown,
  and `selectionSummary` per option; write `DecisionTrace::lastEvaluationTime`; loosen the
  `contentBuilt` cache so the view updates live.

**Deferred**

- The forced-orders player-command mechanism (tier 3 slot is reserved, the mechanism is a
  separate epic).
- The survival/danger tier (tier 1 reserved, no threats in Phase 1 / Phase 2).
- Utility-curve scoring for within-tier factors.
- Any global scheduler or gang-work coordination.

---

## Design decisions (locked)

### 1. Priority key is (tier: int, score: float)

Compare tier first (lower number = higher priority). Score is secondary: it orders options
within the same tier only and can never lift a lower-tier option above a higher-tier one.

### 2. Tier ladder

| Tier | Name | Trigger | Status |
|------|------|---------|--------|
| 1 | Survival / danger | Active threat, flee | RESERVED — not this epic |
| 2 | Critical needs | Any need past its critical threshold (~10%) | Implemented |
| 3 | Forced player orders | Player queues a direct command | RESERVED — separate epic |
| 4 | Active work orders | A queued craft/build and its provisioning chain | Implemented — STICKY |
| 5 | Actionable needs | A need past the comfort threshold | Implemented |
| 6 | Opportunistic work | Haul loose items, stock storage, undirected harvest | Implemented |
| 7 | Idle | Gather food for the larder, wander | Implemented |

"Lower number = higher priority" throughout; tier 1 always wins.

### 3. Needs vs. work ordering (LOCKED)

Tier 4 (active work orders) sits **above** tier 5 (actionable needs): a colonist finishes a
committed/queued work order before addressing a non-critical need. Only critical needs (tier 2)
preempt active work. When idle of assigned work, a hungry colonist eats (tier 5) before
opportunistic work (tier 6) or wandering (tier 7).

Design basis: [Multi-colonist Crafting](../design/multi-colonist-crafting.md) §2b —
"Finishing is very high priority. Completing an in-progress craft outranks starting new work
by a wide margin, so if the assigned colonist leaves to sleep or eat, they come back and finish
it before taking on anything new. The station stays reserved for that colonist across the
interruption." Tier-4 stickiness is the mechanism that delivers this. A tier-5 actionable need
(eat, sleep) briefly preempts tier 4, then the colonist returns to their active work order; this
matches the spec's "comes back and finishes" intent.

This resolves the apparent tension with `ai-behavior.md`'s "Actionable Needs above Gather Food"
ordering: that document's tier-6 "Gather Food / work" slot is what this spec calls
**opportunistic work** (tier 6), which the new ladder correctly keeps below actionable needs.
Work that is committed (active order) gets its own tier 4 above needs; the `ai-behavior.md`
design never had a committed-work tier, so this is an additive split, not a contradiction.

### 4. Within-tier score

Factors, in rough weight order: nearest reachable source (strong preference — fixes the
known "chops a tree 70 m away over an adjacent one" issue); skill level; task age; and a
stickiness / hysteresis bias for the currently in-progress option. Exact weights are an
open question (see below).

Score factors apply only within one tier. A tier-6 option with a large skill bonus cannot
surpass any tier-5 option.

### 5. Floor constants deleted; chain becomes a tier lock

`kWorkOrderProvisionFloor`, `kStorageStockingFloor` — deleted. Their job was to prevent
distance penalties from pushing committed work below Wander; tiers make that unnecessary.

The `chainBonus` (+2000 score inflation, from
[Task Chains](../design/game-systems/colonists/task-chains.md)) becomes a **tier
classification**: a provisioning haul or harvest that is mid-chain is classified at tier 4
(active work order), not tier 6 (opportunistic work). The score term disappears.

### 6. Stickiness / hysteresis

An in-progress job is interrupted only by a strictly higher tier (survival, critical needs,
forced orders). Same-tier swaps require the challenger's score to exceed the current
option's score by a hysteresis margin, preventing thrashing. The existing ~0.5 s re-eval
cadence (from [Priority Configuration System](../design/game-systems/colonists/priority-config.md)
`reEvalInterval`) and event triggers are unchanged; only the preemption rule changes. The
`taskSwitchThreshold` from priority-config.md becomes the within-tier hysteresis margin.

### 7. Tiers as task-type data

A task type's tier is a property of its definition or config, not an if-chain in C++. New
task types opt in to the right tier at definition time. Designers can retune tiers without
code changes.

### 8. Pull model retained

Each colonist evaluates and picks locally; no global scheduler. This is how RimWorld,
Oxygen Not Included, and Going Medieval all scale across hundreds of agents.

---

## Model

### Decision key

```
priority_key = (tier: int, score: float)
```

Comparison: `(t1, s1) > (t2, s2)` iff `t1 < t2`, or `t1 == t2 && s1 > s2`.

### Tier classifications (current task types)

| Task type | Old base score | New tier | Notes |
|-----------|---------------|----------|-------|
| FulfillNeed (critical, needValue < 10%) | 300+ | 2 | Unchanged behaviour |
| FulfillNeed (actionable, needValue < threshold) | 100–150 | 5 | Unchanged behaviour |
| Craft (queued work order) | 40 | 4 | |
| Build / Deconstruct (queued work order) | 41 | 4 | |
| Haul (servesActiveWorkOrder) | 37 (floored at 20) | 4 | chain -> tier classification |
| Harvest (servesActiveWorkOrder) | 36 (floored at 20) | 4 | chain -> tier classification |
| Haul (servesStorageStocking) | 36 (floored at 15) | 6 | |
| Harvest (servesStorageStocking) | 35 (floored at 15) | 6 | |
| Haul (opportunistic) | 37 | 6 | |
| Harvest (opportunistic) | 36 | 6 | |
| PlacePackaged | 38 | 4 or 6 | see open questions |
| FulfillNeed (gather food sentinel) | 12 (flat) | 7 | |
| Wander | 10 (flat) | 7 | |

### Within-tier score composition

```
score = distanceFactor + skillBonus + taskAgeBonus + hysteresisBonus
```

`distanceFactor` is a strong nearest-reachable-source term (exact formula: open question;
the old ±50 distanceBonus range is likely too narrow for a 0-250 within-tier score — see
open questions). `hysteresisBonus` replaces `inProgressBonus` and is only applied to the
option that matches the current in-progress task.

### Job lifecycle / end-conditions

Every job/goal has exactly three terminal states:

- **Done** — work completed successfully.
- **Abort / re-resolve** — target became invalid (entity gone, forgotten by memory, no longer
  reachable, or minimum already satisfied). Locate another valid target of the same goal; if
  found, resume. If none is reachable, retire the goal.
- **Retire** — no valid target exists; remove the goal from the registry. Never loop.

Specific fixes this covers:

- **Harvest loop** (`#20`): locate the harvestable by the goal's stored target entity ID, not
  by a 0.5 m proximity re-search. If the entity is gone or out-of-tolerance, abort and
  re-resolve (or retire). `StorageGoalSystem::reconcileStockingHarvests` must retire a
  stocking-harvest once the storage minimum is met or the source entity vanishes.
- **Queued craft abandonment**: tier-4 stickiness keeps the colonist on the active work
  order; re-resolution surfaces the next provisioning step automatically when an ingredient
  source changes.
- **Multi-unit craft stall** (`CraftingGoalSystem.cpp:332-341`): `deliveredAmount` is never
  decremented after unit 1 consumes the station store, so the same-recipe branch sees
  `deliveredAmount >= targetAmount` and never rebuilds the Haul/Harvest children — a
  multi-unit craft hangs after the first unit. Fix: recompute `deliveredAmount` from the
  live station store each tick rather than accumulating it. Same never-stall family as the
  harvest loop; folded into this epic, not tracked separately.

Invariant: a goal **never loops** — it completes, re-resolves to another valid target, or retires.

---

## Reconciliation with prior design docs

Each entry states what this spec honors, what it supersedes, and why.

### [Colonist AI Behavior](../design/game-systems/colonists/ai-behavior.md)

Records the categorical tier hierarchy (Panic / Breakdown / Critical Needs / Player Control /
Actionable Needs / Gather Food / Wander) and the pull-model cadence.

**Honor:** the categorical-first intent is exactly what the (tier, score) key enforces.
**Extend:** the ai-behavior.md ladder has no committed-work tier; this spec adds tier 4
(Active Work Orders) as a split of the old "work" bucket, placing committed work above needs.
The tier numbering in ai-behavior.md should be updated post-acceptance to match the 7-tier
ladder here (tier 1-7 vs. the current 1-7 with different slot assignments).

### [Priority Configuration System](../design/game-systems/colonists/priority-config.md)

Records the numeric bands (Critical 30000, PlayerDraft 20000, Needs 10000, Work 1000-5000,
Idle 0), the bonus formulas, the `PriorityConfig` class, and `priority-tuning.xml`. The wide
bands were the categorical-first guarantee; the code's collapse to single-digit gaps is the bug.

**Supersede:** the band bases become a tier integer; `chainBonus` becomes a tier
classification; `inProgressBonus` becomes the within-tier hysteresis margin.
**Honor:** `distanceBonus`, `skillBonus`, and `taskAgeBonus` carry over as within-tier score
factors; `reEvalInterval` 0.5 s is unchanged; `taskSwitchThreshold` becomes the within-tier
hysteresis threshold; `reservationTimeout` is unchanged. The `PriorityConfig` class and
`priority-tuning.xml` need a targeted update post-acceptance to reflect the new factor roles.

### [Task Chains](../design/game-systems/colonists/task-chains.md)

Records `chainBonus +2000` as the mechanism for keeping provisioning chains together, plus the
stow-or-drop interruption behavior and the "critical needs interrupt chains" invariant.

**Supersede:** the `chainBonus` score inflation is replaced by tier-4 classification for any
task with `servesActiveWorkOrder == true`. The +2000 term disappears.
**Honor:** the chain-continuation mechanism and stow-or-drop interruption behavior are
unchanged. The "critical needs interrupt chains" invariant maps directly to tier 2 preempting
tier 4.

### [Work Priorities](../design/game-systems/colonists/work-priorities.md)

Records per-colonist 1-9 work-type preference and the 7 fixed work categories
(Emergency > Medical > Farming > Crafting > Construction > Hauling > Cleaning).

**Honor:** the 1-9 UI preference becomes a within-tier score factor for tier 6 opportunistic
work (a colonist set to prefer Crafting ranks opportunistic crafting tasks higher within tier 6).
The simple-mode category order (Emergency > Medical > ...) becomes the default within-tier
ordering for tier 6. Emergency and Medical as *categories* may deserve higher tiers when their
tasks land — that mapping is deferred (see open questions).

### [Task Registry System](../design/game-systems/colonists/task-registry.md)

Records the goal-driven `GlobalTaskRegistry` (tasks generated from goals, not discovery),
discovery-gating (undiscovered entities generate no tasks), and the reservation mechanism
(Phase 3, unwired).

**Honor:** unchanged. `servesActiveWorkOrder` lives on GoalTasks in the registry. Discovery
gating is unaffected. Reservations remain Phase-3-unwired.

### [Decision Trace System](../design/game-systems/colonists/decision-trace.md)

Records the `DecisionTrace` component and task-queue inspector UI (the inspector design).

**Honor:** unchanged contract.
**Extend:** add `tier` and `score` fields to `EvaluatedOption`; the inspector renders tier
label, within-tier score, and bonus breakdown per option; `selectionSummary` is rendered (it
is populated at `AIDecisionSystem.cpp:1558` but never displayed today); fix
`lastEvaluationTime` (declared, never written); loosen the `contentBuilt` cache so the panel
updates live within a selection, not only on colonist switch.

### [Crafting & Material Provisioning — Technical Architecture](./crafting-provisioning-architecture.md)

Records `servesActiveWorkOrder` as the classifier for provisioning tasks, and the priority
floor (`kWorkOrderProvisionFloor`) as the predecessor mechanism keeping provisioning above
Wander. Also records the craft-as-construction analogy and the metered-deposit / no-leftover
invariants.

**Supersede:** the priority floor is replaced by tier-4 classification. The floor constant
is deleted.
**Honor:** `servesActiveWorkOrder` remains the classifier input. The craft-as-construction
analogy, metered deposit, and no-leftover invariants are unchanged.

### [Task Generation Architecture](./task-generation-architecture.md)

Records that there are two distinct priority concepts: the colonist-agnostic UI display
priority (work-type tier + distance) and the per-colonist AI selection priority.

**Honor:** only the AI-selection path becomes (tier, score). The UI display path is unchanged
and maps cleanly onto the tier ladder without modification.

### [Multi-colonist Crafting](../design/multi-colonist-crafting.md)

Records workstation ownership (one colonist per active craft, station reserved across
interruptions) and the "finishing is very high priority" invariant.

**Honor:** tier-4 stickiness is the mechanism that implements this. "Return to finish after
eat/sleep" is tier-5 actionable needs briefly preempting tier 4, then the colonist returning.
This spec also answers the open question from multi-colonist-crafting.md — "what outranks
finish-priority?" — the answer is only tiers 1-2 (danger, critical needs).

### [Colonist Needs System](../design/game-systems/colonists/needs.md)

Records critical threshold (~10% for needs like bladder emergency) and the seek thresholds
(50%, 30%, etc.) that trigger actionable-need tasks.

**Honor:** the current thresholds map directly to tier 2 (critical, ~10%) and tier 5
(actionable, below comfort threshold). These are the existing constants; "confirm thresholds"
is listed as an open question but the default is to keep them unless tuning reveals otherwise.

---

## Implementation plan

Grouped by area; each group has an acceptance note. Sequence is roughly top-down.
Not code — engine touch-points are named so the work is concrete.

### 1. (tier, score) key and comparison

- Add `tier: int` and `score: float` fields to `EvaluatedOption` (in
  `libs/engine/ecs/components/DecisionTrace.h`), replacing the single `calculatePriority()`
  float return with a comparable key type.
- Replace the `std::sort` comparator in `AIDecisionSystem::buildDecisionTrace` with the
  lexicographic (tier asc, score desc, tiebreakId asc) comparison.
- Replace the `calculatePriority()` call at the selection step with the new key comparison.
- Delete `kWorkOrderProvisionFloor`, `kStorageStockingFloor`, `kWanderPriority`, and
  `kGatherFoodPriority` constants once the tier ladder makes them redundant.

Acceptance: category ordering is guaranteed by construction; a far tier-4 option beats any
tier-5 option regardless of distance or skill.

### 2. Map current task types onto tiers

- For each task type, assign a `tier` value at evaluation time in
  `AIDecisionSystem::buildDecisionTrace` (and the individual evaluator methods that build
  `EvaluatedOption`s). The tier comes from the task type's definition / config, not from an
  if-chain — evaluators read it from there.
- Remove the `chainBonus` (+2000) score term. Instead, any Haul or Harvest option with
  `servesActiveWorkOrder == true` is assigned tier 4.
- The `servesActiveWorkOrder` and `servesStorageStocking` flags remain as classification
  inputs; they no longer affect a floor.

Acceptance: colonist finishes a queued Build before eating when a non-critical need fires;
opportunistic haul cannot preempt an active work order.

### 3. Within-tier score: nearest-reachable + stickiness

- Replace the flat `distanceBonus` (-50..+50) with a strong nearest-reachable-source term
  that gives a decisive preference for the closest valid target within a tier. Exact formula
  is a tuning question (see Open questions), but it must be monotonically decreasing with
  distance and dominate skill/age within the tier.
- Rename `inProgressBonus` to hysteresis margin: applied only to the option matching the
  current in-progress task, and only when comparing within the same tier. Must be large
  enough to prevent thrashing on small score differences but smaller than the distance term
  for a clearly closer alternative.
- `skillBonus` and `taskAgeBonus` carry over with the same semantics.

Acceptance: a colonist consistently picks the nearest tree/source within a tier; does not
switch tasks on every re-eval tick (hysteresis); switching to a clearly better same-tier
target still happens within one or two re-eval cycles.

### 4. Job end-conditions and re-resolution

- In `CraftingGoalSystem`, `ConstructionSystem`, and `StorageGoalSystem`: implement the
  Done / Abort-and-re-resolve / Retire lifecycle. On target invalidation, attempt to locate
  another valid target of the same goal before retiring.
- In the Harvest evaluator in `AIDecisionSystem`: locate the target by entity ID
  (`harvestTargetEntityId`), not by proximity. If the entity is absent from memory or the
  world, mark the option as `NoSource` rather than re-queuing to a random nearby entity.
- In `StorageGoalSystem::reconcileStockingHarvests`: retire a stocking-harvest goal when
  the storage minimum is already met or the source entity is gone.
- In `CraftingGoalSystem` (lines 332-341): recompute `deliveredAmount` from the live station
  store each tick rather than accumulating it. This prevents the multi-unit craft stall where
  the same-recipe branch incorrectly sees the already-consumed first unit still counted.
- Confirm Abort triggers on: entity gone (destroyed or unloaded), entity forgotten by the
  colonist's memory system, target no longer reachable (returns `NoSource` from the
  reachability check), or minimum already satisfied (storage goal).

Acceptance: no "no harvestable entity found" loop; a stale harvest goal retires cleanly; a
colonist re-resolves to the next valid ingredient source when one ingredient pile is claimed
by another colonist; a satisfied storage minimum stops generating harvest goals; a multi-unit
craft completes all units without hanging after unit 1.

### 5. Tiers as task-type data

- Add a `tier` field to the task-type definition / config schema (e.g. the action type
  registry or a new `TaskTypeConfig` asset). Evaluators read tier from there rather than
  hardcoding it per task type in C++.
- Provide a validation check that every registered task type has an explicit tier assigned;
  unassigned types fail startup.

Acceptance: adding a new task type requires only a config entry to get correct ordering;
no C++ if-chain changes needed.

### 6. TaskListView inspector enhancement

- **Write `DecisionTrace::lastEvaluationTime`**: the field is declared in `DecisionTrace.h`
  but never assigned. `AIDecisionSystem` should write the current game time to it after each
  `buildDecisionTrace` call.
- **Loosen the `contentBuilt` cache** in `TaskListView::update`
  (`apps/world-sim/scenes/game/ui/views/TaskListView.cpp`): currently returns early
  whenever `lastColonistId == colonistId`. Change to also re-render when
  `lastEvaluationTime` has advanced since the last render. This makes the inspector update
  live within a selection, not only on colonist switch.
- **Enhance the per-option render** in `TaskListView::rebuildContent`: alongside
  `option.reason`, also surface the tier (as a category label), the within-tier score, and
  the bonus breakdown (distance/skill/age/hysteresis — already fields on `EvaluatedOption`
  after step 1). Show `OptionStatus` as the existing badge (Selected / Available / Blocked /
  Idle).
- **Render `selectionSummary`**: currently populated at `AIDecisionSystem.cpp:1558` but
  never rendered. Add a summary line at the top of the trace panel using the existing
  `StatusTextLine` component.

Acceptance: a player can open the task list for any colonist and see: each option's tier
label, its within-tier score, the bonus breakdown, and the status badge; the panel updates
live (each re-eval tick) without requiring a colonist switch; the winner's `selectionSummary`
is visible.

---

## Acceptance criteria (epic-level)

- Category ordering is guaranteed: a far tier-4 active-work-order option beats any tier-5
  actionable-need option, with no floor constants needed.
- The regression scenarios in [docs/testing/](../testing/) pass reliably:
  - A colonist with a queued craft finishes it before eating when only a non-critical need
    fires.
  - A colonist with no queued work and a hungry need eats before opportunistic haul or wander.
  - A critical need (needValue < 10%) preempts active work.
  - A colonist consistently harvests the nearest tree, not a distant one.
  - A stale or unreachable harvest goal retires without looping.
  - A colonist sees a queued craft through its full provisioning chain.
  - A multi-unit craft completes all units without stalling after unit 1.
- The `TaskListView` inspector shows each option's tier, within-tier score, bonus breakdown,
  and status badge; updates live; shows the winner's `selectionSummary`.
- Engine tests green; new tests cover: lexicographic tier ordering, job end-conditions
  (retire on invalid target, re-resolve on valid alternative), within-tier nearest
  preference, and multi-unit craft completion.

---

## Open questions / tuning

- **Within-tier distance formula.** The old ±50 `distanceBonus` range is likely too narrow
  against a 0-250 within-tier score: a max skill or age bonus could swamp distance entirely.
  Proposed default: scale the distance term up so that "clearly closer" dominates skill and
  age — e.g. a linear term yielding 0-150 across the expected working range. Exact function
  (linear, inverse, sqrt) and range to settle during tuning.
- **Hysteresis margin value.** How large is the in-progress stickiness term relative to the
  rescaled distance term? Too small = thrashing; too large = colonist ignores a much closer
  target.
- **Critical vs. actionable need thresholds.** The boundary between tier 2 (critical, ~10%)
  and tier 5 (actionable, below comfort threshold) uses the existing constants from
  [Colonist Needs System](../design/game-systems/colonists/needs.md). Default: keep current
  values unless tuning reveals otherwise.
- **Opportunistic harvest vs. stocking sub-rank.** Both are currently tier 6. Should
  stocking (`servesStorageStocking`) sit at tier 6a and undirected opportunistic harvest at
  tier 6b? The flag could drive a sub-rank within tier 6 without adding a new tier. Proposed
  default: flat tier 6 with no sub-rank; promote to 6a/6b if playtest shows a need.
- **`PlacePackaged` tier.** Proposed: tier 4 when it serves an active work order, tier 6
  otherwise. The "already carrying" in-transit case may warrant explicit classification to
  prevent a carrying colonist from being rerouted mid-carry.
- **Emergency / Medical category tier mapping.** [Work Priorities](../design/game-systems/colonists/work-priorities.md)
  lists Emergency and Medical as the top work categories. When these land as concrete task
  types they likely deserve a tier above tier 6; mapping is deferred until those task types
  are implemented.
- **Inspector layout.** How much horizontal space does tier + score + breakdown need? Does
  the breakdown collapse behind a hover/expand or always show? Panel width budget unknown.

---

## Dependencies & related docs

- [Colonist AI Behavior](../design/game-systems/colonists/ai-behavior.md) — the design-side
  tier intent. Needs a targeted update post-acceptance: tier numbering and the addition of
  tier 4 (Active Work Orders) as an explicit slot.
- [Priority Configuration System](../design/game-systems/colonists/priority-config.md) —
  the predecessor numeric-band system. Needs a targeted update post-acceptance: band bases
  replaced by tier integer, bonus roles updated. The `reEvalInterval`, `taskSwitchThreshold`,
  and `reservationTimeout` fields carry over unchanged.
- [Decision Trace System](../design/game-systems/colonists/decision-trace.md) — the design
  contract for the per-colonist trace component and the task list display.
- [Work Priorities](../design/game-systems/colonists/work-priorities.md) — the existing
  within-tier work selection detail; remains relevant for the tier-6 score composition.
- [Task Chains](../design/game-systems/colonists/task-chains.md) — multi-step task
  continuation; the chain-becomes-tier-lock decision here supersedes the chainBonus approach.
- [Task Registry System](../design/game-systems/colonists/task-registry.md) — goal-driven
  task generation and reservations; unchanged by this spec.
- [Crafting & Material Provisioning — Technical Architecture](./crafting-provisioning-architecture.md)
  — the servesActiveWorkOrder classifier and the predecessor floor mechanism.
- [Task Generation Architecture](./task-generation-architecture.md) — the two priority
  concepts (UI display vs. AI selection); only the AI-selection path changes here.
- [Multi-colonist Crafting](../design/multi-colonist-crafting.md) — workstation ownership
  and the "finish is very high priority" design basis for tier-4 stickiness.
- [Colonist Needs System](../design/game-systems/colonists/needs.md) — need thresholds that
  determine tier-2 vs. tier-5 classification.
- [docs/testing/](../testing/) — the regression guard; the combined flow tests are the
  primary acceptance signal for this epic.
- Competitive reference (concepts, not URLs): RimWorld think-tree / WorkGiver priority walk;
  Oxygen Not Included lexicographic (priority-class, sub-priority, distance) key; Going
  Medieval discrete ordered levels. All use categorical-first arbitration; none uses a
  single additive float.
