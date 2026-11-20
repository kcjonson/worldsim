# Colonist AI Behavior

**Status:** Design  
**Created:** 2024-12-04  
**MVP Status:** See [MVP Scope](../mvp-scope.md) — `ordinary_need`, `opportunistic_work`, and `idle` in Phase 1, full hierarchy in Phase 3

---

## Overview

Colonists are autonomous agents who make their own decisions. The player influences behavior through priority settings and environmental design, but does not micromanage individual actions.

**Core Experience Goal:** Leave the game running and watch colonists take care of themselves. They eat when hungry, sleep when tired, work when able, and wander when idle.

---

## Decision Hierarchy

Colonists always evaluate what to do in this fixed order. A higher rung interrupts a lower one. The key is lexicographic: a `(rung, score)` pair is compared rung-first, so any `critical_need` option beats any `ordinary_need` option regardless of distance or skill. See [Colonist Task Arbitration](../../../technical/colonist-task-arbitration.md) for the implementation.

| Rung                  | Trigger                                          | Example                            |
| --------------------- | ------------------------------------------------ | ---------------------------------- |
| `survival`            | Active threat, flee (reserved)                   | Being chased by a predator         |
| `critical_need`       | Need below its critical threshold                | Bladder emergency                  |
| `low_mood`            | A low-mood behavior during a mood episode        | Snapping at a colonist             |
| `player_order`        | An order the player gave                         | Harvest that bush                  |
| `urgent_need`         | Need below its urgent threshold                  | Very hungry, leaves its work       |
| `own_unfinished_work` | A craft whose part-done unit the colonist holds  | Back to the half-made axe          |
| `committed_work`      | A queued craft, build, teardown, site clear, or placement carry, and its provisioning | Finish the house foundation        |
| `ordinary_need`       | Need below its seek threshold                    | Slightly hungry, seeks food        |
| `opportunistic_work`  | Needs satisfied; loose work available            | Haul items, stock storage, harvest |
| `idle`                | Nothing else to do                               | Gather food for larder, wander     |

What a colonist does at each rung (a need's chain, a piece of work, an idle chain, a player's order, a low-mood behavior) is defined in data as chains of steps whose targets are picked by queries ([Queries and chains](../../../technical/queries-and-chains.md#rungs), which holds the ladder); the rungs say only what comes first. A low-mood behavior sits directly below critical needs and above the player's orders ([Mood](./mood.md#the-episode-cycle)), and [Interruptable crafts](../../../technical/interruptable-crafts.md#the-ladder) gives urgent needs and a crafter's own unfinished work their rungs.

`committed_work` sits above `ordinary_need`: a colonist finishes a committed craft or build before addressing a slightly hungry or tired state. Only a critical need, a mood episode, or a player's order takes a colonist off committed work; an ordinary need does not. Letting a need below its urgent level do the same is a described change ([An urgent-need rung](../../../technical/queries-and-chains.md#an-urgent-need-rung)). See [Colonist Task Arbitration §3](../../../technical/colonist-task-arbitration.md) for the locked design basis (multi-colonist crafting §2b, "finishing is very high priority").

### `survival` (reserved)

Colonist is actively threatened — chased by predator, caught in fire, etc.

**Behavior:** Seek safety by running toward shelter or other colonists.

**Design Note:** RimWorld's panic is criticized because colonists flee randomly into more danger. Our panic must pathfind to actual safety.

**MVP Status:** Deferred (no threats in Phase 1)

### `critical_need`

Any physical need falls below its critical threshold (set per need in the needs config, ~10% for most). Colonist immediately addresses it, bypassing all other priorities including active work orders.

**MVP Status:** Included in Phase 1

### `player_order`

An order the player gave a colonist, such as harvesting a bush they clicked, runs as a chain on this rung and outranks work and needs below a critical one. Direct control is not a rung: it suspends the colonist's AI altogether. See [Player Control](../features/player-control.md).

**MVP Status:** Mechanism is a separate epic.

### `committed_work` (sticky)

A queued craft or build order and its entire provisioning chain (haul, harvest). The colonist commits to this work and will not switch to a same- or lower-rung task without a large score advantage. A non-critical need (`ordinary_need`) never interrupts it; only a need that drops below the critical threshold (~10%, `critical_need`) does, besides a mood episode or a player's order, and the colonist returns to finish afterwards. Letting a need below its urgent level interrupt too is a described change ([An urgent-need rung](../../../technical/queries-and-chains.md#an-urgent-need-rung)).

This rung is the mechanism that implements the "finishing is very high priority" invariant from [Multi-colonist Crafting](../multi-colonist-crafting.md). Chain steps (provisioning hauls and harvests that serve an active work order) are on `committed_work`, not `opportunistic_work`, so they stay bound to the parent order.

**MVP Status:** Implemented in Phase 1 (as part of colonist task arbitration).

### `ordinary_need`

Physical needs have dropped below comfortable thresholds. Colonist seeks entities that fulfill the most urgent need.

**Key Constraint:** Colonist must KNOW where to find fulfillment. No omniscient pathfinding to unseen resources. See [Memory System](./memory.md).

**Hunger sources.** A hungry colonist eats from the nearest food it can reach, whatever kind: its own pack first (no walking), then a ripe berry bush, a loose pile of food it remembers, or a storage box the colony knows that holds food. From a pile or box it takes one meal, enough to stop being hungry, and leaves the rest for everyone else; from a bush it keeps what the cut gives. A full pack never stops it eating: at the food it puts away just enough of what it carries to make room for the meal, never a tool, and cargo it is carrying for someone's work last. The one limit: a colonist whose every pack slot, belt slot, and hand holds a tool can't make room, and its take lifts nothing. Stored food is the buffer between harvests, so a colonist whose bushes are all regrowing eats from the larder instead of waiting. Colonists can starve, but only if they never find food: another colonist's work (hauling the berries to a box, cutting the bush for a goal) never keeps food from a hungry one, though a hungry colonist prefers food nobody is heading for. Between two hungry colonists of different urgency, the critically hungry one takes food the routinely hungry one was walking to; at equal urgency the first to claim it keeps it, and the other looks elsewhere. See [Task Claims](../../../technical/task-claims.md#need-tasks).

**Water.** A thirsty colonist drinks, and a grubby one washes, at the nearest shore it remembers. Water never runs out, so any number of colonists drink and wash at one shore point together. Drinking and washing are separate tasks at the same water: a colonist low on both takes a critical need first, else drinks first, then does the other.

**MVP Status:** Included in Phase 1

### `opportunistic_work`

Needs comfortable; no active work order. Colonist does available work: haul loose items to storage, stock containers, undirected harvest. Work-type preference (1-9, see [Work Priorities](./work-priorities.md)) affects within-rung ordering.

**MVP Status:** Implemented in Phase 1. The "Gather Food" sentinel that keeps colonists fed while idle is on `idle`.

### `idle`

Nothing else to do. Colonist gathers food into its own pack (only while the pack holds none) or wanders known areas, which:
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

## Claims

When a colonist accepts work, its task claims what it will use: the berry bush, the units it will take from a pile, the room it will fill in a box. Other colonists see those claims and pick something else, so two colonists never cut the same plant or haul the same units. Food is the exception: a hungry colonist still takes a bush or berries that another colonist's work or idle gathering holds, and one a less urgent eater holds, though it picks food nobody holds first when it knows some. Nothing else takes over a claim: there is no bumping, and player orders take nothing from anyone. See [Task Claims](../../../technical/task-claims.md).

---

## Task Selection Algorithm

```python
def select_task(colonist, task_registry, priority_config):
    # Build a list of (rung, score) candidates; return the lexicographic winner.
    # A higher rung wins. Score orders within the same rung only.

    if is_threatened(colonist):
        return flee_to_safety()  # survival: reserved (no threats yet)

    for need in colonist.needs:
        if need.value < need.critical_threshold:
            return fulfill_need_urgently(need)  # critical_need: preempts everything below

    # player_order: handled by the order mechanism (separate epic)

    # committed_work: sticky, stays committed across ordinary-need interruptions
    active_order = get_active_work_order(colonist, task_registry)
    if active_order:
        return active_order  # Craft, build, or provisioning chain step for the order

    for need in colonist.needs:
        if need.value < need.seek_threshold:
            task = find_fulfillment(need, colonist.memory)
            if task:
                return task  # ordinary_need: actionable need

    # opportunistic_work: haul, stock, undirected harvest
    work_task = select_work_from_registry(colonist, task_registry, priority_config)
    if work_task:
        return work_task

    return idle()  # idle: gather food for larder, wander
```

See [Task Registry](./task-registry.md) for GlobalTaskRegistry architecture and [Work Priorities](./work-priorities.md) for detailed work selection algorithm.

### What a colonist can get to

A decision never searches for a path. For each place it considers, it asks nav whether the place is
joined to him by ground colonists have explored, which is a quick compare
([Nav mesh](../../../technical/nav-mesh-variable-resolution.md), 4.6). "Not joined" rules the place out
for this decision only. "Joined" is not a promise: a gap too narrow for him, or a wall he knows of,
can still stop him, and only a route answers that.

For a need, he takes the nearest few sources he remembers, by straight line, that are joined to him,
and asks for one route to whichever of them is nearest on foot. The route that comes back decides
which one he goes to. If none can be reached, he remembers "no way" for each of them and the next
decision tries the next few, so a source that is close but sealed off never keeps him from one a
little farther away. A need that finds no way in a round of 8 asks about 16 sources the next time,
then 32; a route that comes back resets it to 8. If the source he is sent to is claimed by another
colonist before the answer arrives, he drops his claims and decides again at once. A thing inside an obstacle, such as a tree to cut or a pile dropped at a bush's
centre, is walked to the spot beside it where he can stand; with no such spot it isn't offered.

"No way" is remembered per colonist, because it depends on his size and on the walls he knows. It is
recorded against the nav mesh's connectivity as it stands and against what he knows. When what he
knows changes (he learns of a wall or a door), every "no way" is forgotten at once. When the mesh's
connectivity changes (the ground changes, an obstacle is felled, a wall or door changes), only the
ones at least 300 AI updates old are forgotten, so a busy colony cannot keep wiping them; a way that
opens can go unnoticed for up to 300 updates, 5 s at 60 updates a second. A place he simply hasn't explored his way to is never
remembered as "no way"; it is out of reach only until ground between gets explored.

The route comes a moment after he asks, usually two frames. While it is out he stands, and the panel
says "Finding a way" once the wait is noticeable; if he is already walking toward the same place
(a replan after he spots a wall, or after obstacles appear ahead of him), he keeps walking and
switches to the new route where he stands (at his projection onto the nearest leg of it), and the
panel says "Re-routing" if it is slow.

While the forward snap holds him inside an obstacle, his decisions wait, one interval each, up to 4;
if the wait runs out the route fails without a "no way" on the target.

A decision's cost: he is located once, and each place costs one component-label lookup and a
compare. Finding the spot beside a thing inside an obstacle is done only for the places that have
no label of their own and are on the mesh. A wander point must share his component label and fit
his disc; a wander already under way keeps its point and probes nothing.

---

## When a colonist decides

A colonist runs the selection above on an event, and otherwise every 0.5 s (`AIDecisionSystem::kReEvalInterval`), whatever he is doing. `AIDecisionSystem::shouldReEvaluate` holds the triggers.

He decides at once when:

- he has no task (he just spawned, or finished or dropped one);
- he has arrived where his task leads. While an action runs there he decides every frame, which keeps the panel's trace current; the action is only interrupted by a higher rung;
- the action system asks for a decision now: after a landed cut, or a pickup by a colonist already carrying goods from earlier legs of the same gathering;
- a need crosses into critical, unless he already serves a critical need. The crossing is measured against the needs that were critical at his last decision (`Task::criticalNeedsAtDecision`, one bit per need), so a need set critical directly by a dev verb or a test counts as a crossing, and one already critical when he spawns or finishes another task is caught by the no-task event;
- his route fails (`NavState::CantFindWayTo`, a route answer of "no known way"), once, at the failure. If the decision that follows picks a target whose route fails too, later decisions wait for the interval until a route is found (`Task::routeFailureDecided`).

Waiting for a route is not an event: he stands with his movement off, decides on the interval like any other walk, and a route that comes back adds no decision.

A walking idle wander also decides every frame, so anything better is taken the moment it appears.

Everything that persists runs on the interval: a critical need with nothing reachable to serve it, a route that keeps failing, a wait on regrowing food, food another eater holds. A critical need or a failed route no longer re-decides every frame for as long as it lasts. He never freezes either; each interval he looks again and takes whatever has become available.

---

## Related Documents

- [Needs System](./needs.md): What triggers `critical_need` and `ordinary_need`
- [Memory System](./memory.md) — What colonists know about
- [Decision Trace](./decision-trace.md) — Task queue display system
- [Work Priorities](./work-priorities.md) — `opportunistic_work` behavior
- [Player Control](../features/player-control.md): direct control and player orders
- [Entity Capabilities](../world/entity-capabilities.md) — How needs are fulfilled
- [Task Registry](./task-registry.md): Global task list architecture for `committed_work` and `opportunistic_work`
- [Priority Config](./priority-config.md): Within-rung score factors and thresholds
- [Task Chains](./task-chains.md) — Multi-step task continuation
- [Queries and chains](../../../technical/queries-and-chains.md): needs, actions, work, and behaviors as data
- [Colonist Task Arbitration](../../../technical/colonist-task-arbitration.md): Implementation of the (rung, score) key, the ladder, job lifecycle

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
