# Colonist Memory System

**Status:** Design  
**Created:** 2024-12-04  
**MVP Status:** See [MVP Scope](../mvp-scope.md) — Basic memory in Phase 1, social sharing in Phase 2

---

## Overview

**This is a core differentiating feature.** Unlike most colony sims where colonists have omniscient knowledge, our colonists can only interact with things they know about.

A colonist cannot pathfind to, reserve, or use an entity they have never seen.

---

## What Colonists Remember

### Entity Locations

Colonists remember WHERE entities are. That's the core of the system.

> "There's a berry bush at the south edge of camp."

### Memory Is a Snapshot, and Can Be Wrong

**Key Decision (revised 2026-06-12, supersedes "State Is Always Current"):** Memory records what was true at last observation. Truth is reconciled only by seeing the entity again.

A colonist who knows a tree may walk to it and find it died, fell, or was chopped by someone else while they were away. On arrival, vision reconciles: the memory entry updates or is removed, the task fails gracefully, and the colonist re-evaluates.

**Rationale:** The original decision (known location grants access to live state) quietly reintroduced omniscience: a colonist across the map instantly knowing a bush was harvested is the same unreality as knowing where everything is. Snapshot memory also produces behaviors we actively want: walking to a dead tree, hunting from a last-seen position, and planning routes through walls they haven't seen yet.

**This is not staleness simulation.** Entries keep their last-observed snapshot; nothing decays or is recomputed per tick. The only cost is reconciling on re-observation, which vision already does.

**Implication:** A farmer checks remembered crop fields and may find some already harvested on arrival. Witnessing changes (below) keeps nearby colonists current; social sharing and radios propagate the rest.

### Mobile Entities

For animals and other moving things, colonists remember the **last known location**.

**Example:** Bob saw a deer at coordinates (50, 30). The deer has since moved. Bob's memory says "deer at (50, 30)" but when he goes there, the deer is gone.

**Behavior:** Bob arrives, deer isn't there. He either gives up and re-evaluates, or enters search behavior (see Search & Discovery Behaviors below).

### Structures Are Remembered Too

Walls, doors, and buildings are remembered entities like anything else, and navigation plans against the layout a colonist *believes*:

- A door they've never seen doesn't exist for them. To get inside, they must discover one.
- A wall built while they were away doesn't block their plans. They route as if it weren't there, see it, and re-route.
- Raiders arrive knowing the colony's location, not its floor plan. They can't path to a storage room they've never seen.

Mechanics live in [Pathfinding Architecture](/docs/technical/pathfinding-architecture.md) (Belief-Filtered Navigation).

---

## How Memory Is Acquired

### Direct Observation (Primary)

Colonists see entities within a sight radius around them.

**MVP:** Circular range, sees through walls  
**With buildings:** Walls occlude sight; windows pass sight but block movement. This is a hard dependency of belief-filtered navigation (without it, anyone walking past a building learns its whole interior through the walls) and lands with it — mechanics in [Vision Architecture](/docs/technical/vision-architecture.md).

Everything within sight range is automatically known. Colonists continuously observe while doing other activities.

### Witnessing Changes

If Bob is within visual range when Alice harvests a berry bush, Bob sees it happen. His knowledge updates automatically.

### Social Sharing

Colonists share knowledge during casual conversation.

**Trigger:** Whenever colonists are idle or doing social activities AND in proximity to each other.

**What's Shared:** Random subset of known entities, biased toward groupings. Instead of sharing one random berry bush, Alice shares "here's where all the berry bushes are" — clusters of related knowledge.

**UI Affordance** - a small UI affordance should be made above both the colonists heads, and an item should appear in the event log

**MVP Status:** Deferred to Phase 2

---

## Starting Knowledge

### At Spawn

Colonists know whatever is within their sight range at spawn. No pre-loaded knowledge, no "briefing" system.

### New Arrivals

Newcomers to an established colony see their surroundings immediately. They don't know about the farm on the other side of camp until they walk there or someone tells them.

---

## Lost Knowledge

If the only colonist who knew about a distant resource dies, that knowledge is lost to the colony. Others must rediscover it.

This creates meaningful consequences for colonist death beyond losing labor.

---

## Search & Discovery Behaviors

What an agent does when memory runs out or turns out to be wrong. Three behaviors, shared by colonists, animals, and raiders:

- **Stale-target arrival.** Go to the remembered position; vision reconciles on arrival; if the target is gone, the memory entry invalidates, the task fails gracefully, and the agent re-evaluates.
- **Last-known-position search** (hunting). Go to where the target was last seen, then search outward: an expanding pattern of looks biased toward where it could plausibly have gone. The hunter who saw a deer by the river checks the river first.
- **Door discovery** (perimeter probe). The agent knows a building but no way in. Walk the exterior, watching the walls, until an opening is found or the loop closes ("sealed, as far as I know" is itself remembered). A raider casing the colony and a newcomer finding the kitchen door are the same behavior.

These read as intent, not error, only if the UI helps: the current-task line surfaces the navigation state with status colors (see Current Task Navigation States under UI Elements), and the re-route moment is shown rather than silent.

## Impact on Gameplay

| Game Stage | Memory Impact |
|------------|---------------|
| Early Game | New colony knows very little. First priority is exploration. |
| Established | Thorough exploration means efficient operations. |
| New Arrivals | Newcomers are inefficient at first, learn over time. |
| Remote Resources | Require expeditions or waiting for wanderers. |

---

## UI Elements

### Colonist Info Panel

Show what this colonist knows:
- Known entities: 47 total - expandable to exact list

### Current Task Navigation States

The info panel's current-task line surfaces *how* the colonist is moving, not just what they're doing. Belief-driven behaviors must read as intent; the state vocabulary plus the existing status colors does that. "Searching" states are a mild warning on purpose: they mean colonist time is being spent on uncertainty the player could fix (stale knowledge, a missing door, a lost target).

| Navigation state | Display | Color |
|------------------|---------|-------|
| Traveling | "Going to [target]" | active (green) |
| Re-routing (momentary) | "Re-routing" | neutral, brief |
| LKP search | "Searching for [target]" | mild warning (yellow) |
| Door discovery | "Looking for a way into [building]" | mild warning (yellow) |
| No believed route | "Can't find a way to [target]" | blocked (red) |
| Idle | "Wandering" | idle (gray) |

The momentary "Re-routing" beat doubles as the discovery affordance: the player sees the colonist react to the new wall instead of wondering why they walked at it.

### Debug Overlay (Development)

(low priority)

Visualization mode showing colonist's knowledge:
- Bright: Entities this colonist knows
- Dim: Entities they don't know

Useful for debugging "why won't Bob eat?" situations.

---

## Future Features

### Radios (Technology Unlock)

Once researched, colonists with radios share memory in real-time.

**Before radios:** Colonist under attack must physically reach others to warn them.  
**After radios:** Attack alert instantly shared with all radio-equipped colonists.

### Maps

Physical map items that grant knowledge when read.
- Starting scenario might include local area map
- Scouts could draw maps of explored territory
- Found maps reveal new regions
- maps contain a list of knowledge items

---

## Scale Considerations

The memory system stores entity IDs per colonist. For a colony with 20 colonists and 10,000 entities:
- **Worst case:** 200,000 entries (20 × 10,000)
- **Realistic:** Much less — not every colonist knows every entity
might need to bucket by type. this colonist knows there are trees (and a secondary step to search/access which trees). similar for stacks of items, this colonist knows there is a stack of wood (and need further query to get infomration about whats in that wood pile)

Scale concerns are engineering problems, not gameplay constraints.

---

## Related Documents

- [AI Behavior](./ai-behavior.md) — How memory constrains task selection
- [Skills System](./skills.md) — Skill knowledge (separate from location knowledge)
- [Entity Capabilities](../world/entity-capabilities.md) — What colonists look for
- [Pathfinding Architecture](/docs/technical/pathfinding-architecture.md) — Belief-filtered navigation, search primitives

---

## Historical Addendum

This document was moved from `docs/design/systems/colonist-memory.md` during the 2024-12-04 documentation reorganization.

### Original MVP Scope Section (Removed)

```
## MVP Scope

**Phase 1 (MVP):**
- Binary known/unknown per entity (entity IDs in ECS)
- Circular sight range (through walls for simplicity)
- Memory filters task selection - can't target unknown entities
- Player directives magically known by all colonists
- Witnessing changes in visual range updates memory
- No social sharing yet

**Phase 2:**
- Social knowledge sharing (idle + proximity trigger)
...
```

This is now consolidated in [MVP Scope](../mvp-scope.md).
