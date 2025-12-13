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

Colonists always evaluate what to do in this fixed order. Higher tiers interrupt lower tiers.

| Tier | Name             | Trigger              | Example                    |
| ---- | ---------------- | -------------------- | -------------------------- |
| 1    | Panic            | Active threat        | Being chased by predator   |
| 2    | Breakdown        | Low mood             | Mental break at <20% mood  |
| 3    | Critical Needs   | Need below ~10%      | Bladder emergency          |
| 4    | Player Control   | Player takes control | Direct movement mode       |
| 5    | Actionable Needs | Need below threshold | Hungry, seeks food         |
| 6    | Work             | Needs satisfied      | Farming, building, hauling |
| 7    | Wander           | Nothing to do        | Idle exploration           |

### Tier 1: Panic (Flee)

Colonist is actively threatened — chased by predator, caught in fire, etc.

**Behavior:** Seek safety by running toward shelter or other colonists.

**Design Note:** RimWorld's panic is criticized because colonists flee randomly into more danger. Our panic must pathfind to actual safety.

**MVP Status:** Deferred (no threats in Phase 1)

### Tier 2: Breakdown

Mood has dropped below breakdown thresholds (35% / 20% / 5%). See [Needs System](./needs.md) for thresholds.

**Behavior:** Varies by severity — wandering sad, binge eating, social withdrawal, destructive outbursts at extremes.

**MVP Status:** Deferred to Phase 3

### Tier 3: Critical Needs

Any physical need hits critical threshold (~10%). Colonist immediately addresses it, bypassing all other priorities.

**MVP Status:** Included in Phase 1

### Tier 4: Player Control

Player has taken direct control. See [Player Control](../features/player-control.md).

**MVP Status:** Deferred to Phase 2

### Tier 5: Actionable Needs

Physical needs have dropped below comfortable thresholds. Colonist seeks entities that fulfill the most urgent need.

**Key Constraint:** Colonist must KNOW where to find fulfillment. No omniscient pathfinding to unseen resources. See [Memory System](./memory.md).

**MVP Status:** Included in Phase 1

### Tier 6: Work

All needs comfortable. Colonist performs work based on personal priority settings. See [Work Priorities](./work-priorities.md).

**MVP Status:** One work type (Harvest Wild) in Phase 1

### Tier 7: Wander

Nothing else to do. Colonist moves randomly through known areas, which:
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
def select_task(colonist, world):
    # Evaluate tiers in order
    if is_threatened(colonist):
        return flee_to_safety()  # Tier 1
    
    if colonist.mood < breakdown_threshold:
        return breakdown_behavior()  # Tier 2
    
    for need in colonist.needs:
        if need.value < need.critical_threshold:
            return fulfill_need_urgently(need)  # Tier 3
    
    if colonist.under_player_control:
        return await_player_input()  # Tier 4
    
    for need in colonist.needs:
        if need.value < need.seek_threshold:
            task = find_fulfillment(need, colonist.memory)
            if task:
                return task  # Tier 5
    
    work_task = find_work(colonist.work_priorities, colonist.memory)
    if work_task:
        return work_task  # Tier 6
    
    return wander()  # Tier 7
```

---

## Related Documents

- [Needs System](./needs.md) — What triggers Tiers 3 and 5
- [Memory System](./memory.md) — What colonists know about
- [Decision Trace](./decision-trace.md) — Task queue display system
- [Work Priorities](./work-priorities.md) — Tier 6 behavior
- [Player Control](../features/player-control.md) — Tier 4 behavior
- [Entity Capabilities](../world/entity-capabilities.md) — How needs are fulfilled

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
