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

### State Is Always Current

**Key Decision:** If a colonist knows an entity's location, they have access to its current state.

No staleness tracking. No "I remember it had berries yesterday." The memory is "I know that bush exists" and the game state tells you what's currently there.

**Rationale:** Tracking stale states adds complexity without clear gameplay benefit. The interesting constraint is knowing vs not knowing.

**Implication:** A farmer looking for harvest work can check all known crop fields and see which ones are actually ready right now.

### Mobile Entities

For animals and other moving things, colonists remember the **last known location**.

**Example:** Bob saw a deer at coordinates (50, 30). The deer has since moved. Bob's memory says "deer at (50, 30)" but when he goes there, the deer is gone.

**Behavior:** Bob arrives, deer isn't there. He either gives up and re-evaluates, or enters a "searching" behavior (future feature).

---

## How Memory Is Acquired

### Direct Observation (Primary)

Colonists see entities within a sight radius around them.

**MVP:** Circular range, sees through walls  
**Future:** Proper line of sight with wall occlusion

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

### Searching Behavior

When colonist goes to last known location and target is gone, they could enter a "search" mode — wandering in expanding circles looking for the target.

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
