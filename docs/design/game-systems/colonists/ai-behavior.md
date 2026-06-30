# Colonist AI Behavior

**Status:** Design  
**Created:** 2024-12-04  
**MVP Status:** See [MVP Scope](../mvp-scope.md) — Tiers 5-7 in Phase 1, full hierarchy in Phase 3

---

## Overview

Colonists are autonomous agents who make their own decisions. The player influences behavior through priority settings and environmental design, but does not micromanage individual actions.

**Core Experience Goal:** Leave the game running and watch colonists take care of themselves. They eat when hungry, sleep when tired, work when able, and wander when idle.

---

## Decision Hierarchy

Colonists always evaluate what to do in this fixed order. Higher tiers (lower number) interrupt lower tiers. The key is lexicographic: a `(tier, score)` pair is compared tier-first, so any tier-2 option beats any tier-5 option regardless of distance or skill. See [Colonist Task Arbitration](../../../technical/colonist-task-arbitration.md) for the implementation.

| Tier | Name                | Trigger                                         | Example                               | Status                   |
| ---- | ------------------- | ----------------------------------------------- | ------------------------------------- | ------------------------ |
| 1    | Survival / danger   | Active threat, flee                             | Being chased by predator              | RESERVED (no threats yet) |
| 2    | Critical Needs      | Need below ~10%                                 | Bladder emergency                     | Implemented              |
| 3    | Forced player orders | Player queues a direct command                 | Direct movement mode                  | RESERVED (separate epic) |
| 4    | Active Work Orders  | Colonist has a queued craft/build in progress   | Finish the house foundation           | Implemented, STICKY      |
| 5    | Actionable Needs    | Need below comfort threshold                    | Hungry, seeks food                    | Implemented              |
| 6    | Opportunistic work  | Needs satisfied; loose work available           | Haul items, stock storage, harvest    | Implemented              |
| 7    | Idle                | Nothing else to do                              | Gather food for larder, wander        | Implemented              |

"Lower number = higher priority." Tier 4 (active work orders) sits above tier 5 (actionable needs): a colonist finishes a committed craft or build before addressing a non-critical need. Only tier 2 (critical needs) preempts active work. See [Colonist Task Arbitration §3](../../../technical/colonist-task-arbitration.md) for the locked design basis (multi-colonist crafting §2b, "finishing is very high priority").

### Tier 1: Survival / danger (RESERVED)

Colonist is actively threatened — chased by predator, caught in fire, etc.

**Behavior:** Seek safety by running toward shelter or other colonists.

**Design Note:** RimWorld's panic is criticized because colonists flee randomly into more danger. Our panic must pathfind to actual safety.

**MVP Status:** Deferred (no threats in Phase 1)

### Tier 2: Critical Needs

Any physical need hits critical threshold (~10%). Colonist immediately addresses it, bypassing all other priorities including active work orders.

**MVP Status:** Included in Phase 1

### Tier 3: Forced player orders (RESERVED)

Player has taken direct control. See [Player Control](../features/player-control.md).

**MVP Status:** Mechanism is a separate epic; tier slot is reserved.

### Tier 4: Active Work Orders (STICKY)

A queued craft or build order and its entire provisioning chain (haul, harvest). The colonist commits to this work and will not switch to a same- or lower-tier task without a large score advantage. A tier-5 need (eat, sleep) briefly interrupts tier 4, then the colonist returns to finish.

This tier is the mechanism that implements the "finishing is very high priority" invariant from [Multi-colonist Crafting](../multi-colonist-crafting.md). Chain steps (provisioning hauls and harvests that serve an active work order) are classified at tier 4, not tier 6, so they stay bound to the parent order.

**MVP Status:** Implemented in Phase 1 (as part of colonist task arbitration).

### Tier 5: Actionable Needs

Physical needs have dropped below comfortable thresholds. Colonist seeks entities that fulfill the most urgent need.

**Key Constraint:** Colonist must KNOW where to find fulfillment. No omniscient pathfinding to unseen resources. See [Memory System](./memory.md).

**MVP Status:** Included in Phase 1

### Tier 6: Opportunistic work

Needs comfortable; no active work order. Colonist does available work: haul loose items to storage, stock containers, undirected harvest. Work-type preference (1-9, see [Work Priorities](./work-priorities.md)) affects within-tier ordering.

**MVP Status:** Implemented in Phase 1. The original "Gather Food" sentinel that kept colonists fed while idle is now tier 7 (idle); what was informally called "work" here is tier 6.

### Tier 7: Idle

Nothing else to do. Colonist gathers food for the larder or wanders known areas, which:
- Looks natural (no frozen standing)
- Discovers new entities
- Creates social interaction opportunities

**MVP Status:** Included in Phase 1

---

## The Task Queue

Each colonist has a visible queue showing current and pending tasks. The queue is **computed on-demand** from the colonist's current needs and memory — it's not a pre-planned schedule.

**Shows:**
- Current task with progress
- Pending tasks in priority order
- Reason each task was selected

**Implementation:** See [Decision Trace](./decision-trace.md) for the system that powers this display.

**Player Value:** "Why isn't Bob eating?" → Check queue → "Oh, he's drinking first because Thirst is at 35%."

---

## Reservation System

When a colonist decides to use a resource, they "reserve" it. Others skip reserved resources. This prevents two colonists walking to the same berry bush.

---

## Task Selection Algorithm

```python
def select_task(colonist, task_registry, priority_config):
    # Build a list of (tier, score) candidates; return the lexicographic winner.
    # Lower tier = higher priority. Score orders within the same tier only.

    if is_threatened(colonist):
        return flee_to_safety()  # Tier 1: RESERVED (no threats yet)

    for need in colonist.needs:
        if need.value < need.critical_threshold:
            return fulfill_need_urgently(need)  # Tier 2: preempts everything below

    # Tier 3: RESERVED for forced player orders (separate epic)

    # Tier 4: active work orders (sticky, stays committed across tier-5 interruptions)
    active_order = get_active_work_order(colonist, task_registry)
    if active_order:
        return active_order  # Craft, build, or provisioning chain step for the order

    for need in colonist.needs:
        if need.value < need.seek_threshold:
            task = find_fulfillment(need, colonist.memory)
            if task:
                return task  # Tier 5: actionable need

    # Tier 6: opportunistic work: haul, stock, undirected harvest
    work_task = select_work_from_registry(colonist, task_registry, priority_config)
    if work_task:
        return work_task

    return idle()  # Tier 7: gather food for larder, wander
```

See [Task Registry](./task-registry.md) for GlobalTaskRegistry architecture and [Work Priorities](./work-priorities.md) for detailed work selection algorithm.

---

## Related Documents

- [Needs System](./needs.md): What triggers Tiers 2 and 5
- [Memory System](./memory.md) — What colonists know about
- [Decision Trace](./decision-trace.md) — Task queue display system
- [Work Priorities](./work-priorities.md) — Tier 6 behavior
- [Player Control](../features/player-control.md): Tier 3 behavior (reserved)
- [Entity Capabilities](../world/entity-capabilities.md) — How needs are fulfilled
- [Task Registry](./task-registry.md): Global task list architecture for Tiers 4 and 6
- [Priority Config](./priority-config.md): Within-tier score factors and thresholds
- [Task Chains](./task-chains.md) — Multi-step task continuation
- [Colonist Task Arbitration](../../../technical/colonist-task-arbitration.md): Implementation of the (tier, score) key, tier ladder, job lifecycle

---

## Historical Addendum

This document is derived from docs/design/systems/colonist-ai.md with the following changes:

### Removed MVP Scope Section

The original file had:
```
## MVP Scope

**Phase 1:** Tiers 5-7 only. Four needs (Hunger, Thirst, Energy, Bladder). One work type (Foraging). Basic queue display.

**Later:** Panic, Breakdown, Player Control, complex work types.
```

This is now consolidated in [MVP Scope](../mvp-scope.md).

### Cross-Reference Updates

Original file referenced `../../mechanics/colonists.md` for mood thresholds. This is now in [needs.md](./needs.md).

### Consolidation Date
- **2024-12-04:** Moved from systems/colonist-ai.md → game-systems/colonists/ai-behavior.md
