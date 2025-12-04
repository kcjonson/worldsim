# Colonist Memory System

**Status:** Design  
**Created:** 2024-12-04  
**Updated:** 2024-12-04

## Overview

**This is a core differentiating feature.** Unlike most colony sims where colonists have omniscient knowledge, our colonists can only interact with things they know about.

A colonist cannot pathfind to, reserve, or use an entity they have never seen.

## What Colonists Remember

### Entity Locations

Colonists remember WHERE entities are. That's it.

"There's a berry bush at the south edge of camp."

### State Is Always Current

**Key Decision:** If a colonist knows an entity's location, they have access to its current state.

No staleness tracking. No "I remember it had berries yesterday." The memory is "I know that bush exists" and the game state tells you what's currently there.

**Rationale:** Tracking stale states adds complexity without clear gameplay benefit. The interesting constraint is knowing vs not knowing - not remembering outdated information.

**Implication:** A farmer looking for harvest work can check all known crop fields and see which ones are actually ready right now.

### Mobile Entities

For animals and other moving things, colonists remember the **last known location**.

**Example:** Bob saw a deer at coordinates (50, 30). The deer has since moved. Bob's memory says "deer at (50, 30)" but when he goes there, the deer is gone.

**Behavior:** Bob arrives at last known location, deer isn't there. He either:
- Gives up and re-evaluates tasks
- Enters a "searching" behavior (future feature)

This creates interesting hunting/tracking gameplay later. For MVP, mobile entities may not be relevant.

## How Memory Is Acquired

### Direct Observation (Primary)

Colonists see entities within a sight radius around them.

**MVP:** Circular range, sees through walls  
**Future:** Proper line of sight with wall occlusion

Everything within sight range is automatically known. Colonists continuously observe while doing other activities.

### Witnessing Changes

If Bob is within visual range when Alice harvests a berry bush, Bob sees it happen. His knowledge updates automatically - he now knows that bush is empty.

No special "watch others" behavior needed. Visual range handles it.

### Social Sharing

Colonists share knowledge during casual conversation.

**Trigger:** Whenever colonists are idle or doing social activities AND in proximity to each other.

**What's Shared:** Random subset of known entities, biased toward groupings.

Example: Instead of sharing one random berry bush, Alice shares "here's where all the berry bushes are" - a cluster of related knowledge makes more sense conversationally than scattered random facts.

**Amount:** Small subset per interaction (exact numbers TBD during implementation). Knowledge spreads gradually through the colony, not instantly.

### Player Directives (Magic)

**All colonists automatically know about player-designated things:**
- Build sites ("construct wall here")
- Designated zones (stockpiles, rooms)
- Assigned tasks

**Rationale:** The player is the overseer. When they designate something, it's "announced" to the colony. This prevents frustrating scenarios where colonists ignore build orders because no one "discovered" the blueprint.

### NOT Triggers for Memory

- **Sound:** No hearing-based discovery. Too complex.
- **Inference:** Seeing Alice carry berries does NOT mean Bob infers "berries exist somewhere"
- **Reasoning:** No "I haven't seen food in the north, so there probably isn't any"

Keep it simple: you see it, you know it.

## Exploration

### Wandering Discovers

When colonists wander (idle behavior), they move around and naturally discover new entities through their sight range.

### Tracking Explored vs Unexplored

**OPEN QUESTION - Engineering Required**

How do colonists know where they haven't been?

Options to explore:
- Per-tile "seen" flags (memory intensive with millions of tiles)
- Per-chunk "explored" tracking (coarser granularity)  
- Boundary inference (edge of known entities = unexplored beyond)
- Time-based "not visited recently" rather than "never visited"

**Leave as stub.** Wander behavior works even without this - random movement still discovers things. Optimizing exploration is a later concern.

## Memory Scope and Scale

### Chunk-Based Loading

Memory is associated with map chunks. When colonist is active, only memories for nearby chunks are relevant.

**Practical effect:** A colonist won't path across the entire planet to pick a specific berry bush. Distance limits on task selection handle this naturally.

### No Capacity Limit (Design-wise)

No artificial "colonist can only remember 100 things" limit. Scale concerns are engineering problems, not gameplay constraints.

**Engineering will handle:** Efficient storage, chunk-based queries, pruning strategies if needed.

## Starting Knowledge

### At Spawn

Colonists know whatever is within their sight range at the moment they spawn.

New arrivals to an established colony see their surroundings immediately. They don't know about the farm on the other side of camp until they walk over there (or someone tells them).

### No Pre-loaded Knowledge

No "briefing" system where existing colonists automatically share everything with newcomers. Knowledge spreads through normal social interaction over time.

## Lost Knowledge

If the only colonist who knew about a distant resource dies, that knowledge is lost to the colony. Others must rediscover it.

This creates meaningful consequences for colonist death beyond just losing their labor.

## Impact on Gameplay

### Early Game
New colony knows very little. First priority is exploration. Players may directly control colonists to scout efficiently.

### Established Colony
Thorough exploration means efficient operations. Most resources are known.

### New Arrivals
Newcomers are inefficient at first. They learn the colony layout through observation and conversation.

### Remote Resources
Distant discoveries require either:
- Sending expeditions (direct control exploration)
- Waiting for wanderers to stumble upon them
- Future: Maps revealing locations

## UI Elements

### Colonist Info Panel

Show what this colonist knows:
- Known entities: 47 total
- Food sources: 12
- Water sources: 3
- Work targets: 30

### Debug Overlay (Development)

Visualization mode showing colonist's knowledge:
- Bright: Entities this colonist knows
- Dim: Entities they don't know
- Useful for debugging "why won't Bob eat?" situations

## Future Features

### Radios (Technology Unlock)

Once researched, colonists with radios share memory in real-time.

**Gameplay Impact:**
- Before radios: Colonist under attack must physically reach others to warn them
- After radios: Attack alert instantly shared with all radio-equipped colonists

This creates a meaningful technology milestone where "colony awareness" fundamentally changes. Defense coordination becomes much easier.

**Design Questions for Later:**
- Do radios require power/batteries?
- Range limits?
- Can radios be jammed/destroyed?

### Maps

Physical map items that grant knowledge when read.

- Starting scenario might include local area map
- Scouts could draw maps of explored territory
- Found maps reveal new regions
- Maps could be traded with other factions

### Searching Behavior

When colonist goes to last known location and target is gone (especially mobile entities), they could enter a "search" mode - wandering in expanding circles looking for the target.

## Relationship to Skill System

This memory system tracks **where things are** (location knowledge).

The [Skills System](../../mechanics/skills.md) tracks **how to do things** (skill knowledge) - crafting tiers, unlocked recipes, etc.

These are separate systems that both relate to "what colonists know":

| Type | Example | Lost When Colonist Dies |
|------|---------|------------------------|
| Location Memory | "Berry bush at south camp" | Yes - others must rediscover |
| Skill Knowledge | "Can craft rough clothing" | Yes - colony loses that tier |

Both reinforce the "knowledge is fragile" theme but are mechanically independent.

## Technical Notes

- Memory entries are entity IDs from the ECS
- Memory is chunk-associated for efficient loading
- Distance limits on task selection naturally bound relevant memory scope

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
- Sharing biased toward entity groupings ("all the berry bushes")
- Wander prefers unexplored areas (requires exploration tracking)
- Mobile entity handling (last known location, give up if missing)

**Phase 3:**
- Line of sight (walls block vision)
- Map items that grant knowledge
- Radios for real-time memory sharing
- Searching behavior for missing mobile entities
- Exploration tracking optimization
