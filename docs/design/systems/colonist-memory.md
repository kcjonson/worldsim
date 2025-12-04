# Colonist Memory System

**Status:** Design  
**Created:** 2024-12-04

## Overview

**This is a core differentiating feature.** Unlike most colony sims where colonists have omniscient knowledge, our colonists can only interact with things they know about.

A colonist cannot pathfind to, reserve, or use an entity they have never seen.

## What Colonists Remember

### Entity Locations
"There's a berry bush at the south edge of camp."

Colonists remember where they've seen things. For stationary objects (trees, buildings), this is reliable. For mobile things (animals), this may be outdated.

### Entity States
"That berry bush had ripe berries."

Colonists remember the condition of things. This matters for work selection - no point sending a farmer to a field they know is empty.

**Open Question:** How current is state knowledge? If Bob saw crops yesterday, does he know they're ready today? Probably not - he knows the location, but state may require re-checking.

## How Memory Is Acquired

### Direct Observation
Colonists see things in a radius around them. Currently sees through walls (simpler), future versions may require line of sight.

Everything within sight range is automatically added to memory with current state.

### Exploration (Wander)
When colonists have no tasks, they wander. Wandering naturally discovers new areas and entities.

Wander behavior should prefer unexplored edges of known territory.

### Social Sharing
Colonists share knowledge during casual conversation.

"Hey Bob, there's a pond to the west."

When colonists chat, they exchange some location knowledge. This spreads information through the colony without everyone needing to personally explore.

### Maps (Future)
Physical map items could grant knowledge when read. Starting scenario might include a local area map. Scouts could draw maps of explored territory.

## Stale Information

Memory can become outdated:
- Berry bush was harvested by someone else
- Animal moved to new location
- Building was constructed/destroyed

**Resolution:** Colonist travels to remembered location, observes current reality, updates memory. Task may fail, colonist re-evaluates.

This creates realistic moments: "Went to get berries but they're gone."

## Impact on Gameplay

### Early Game
New colony knows very little. First priority is exploration to find resources. Players may need to directly control colonists to scout.

### Established Colony
Well-explored territory means efficient task selection. Colonists know where everything is.

### New Colonists
Arrivals know nothing about local area. They're inefficient until they learn the lay of the land (or others share knowledge).

### Lost Knowledge
If the only colonist who knew about a distant resource dies, that knowledge is lost. Others must rediscover it.

## UI Elements

### Colonist Info
Show count of known entities by type:
- Food sources: 12
- Water sources: 3
- Work targets: 45

### Debug Visualization
Overlay showing what specific colonist knows - highlighting known entities, dimming unknown ones.

## MVP Scope

**Phase 1:** Basic entity memory (location, exists). Circular sight range through walls. Memory used for task selection.

**Phase 2:** State tracking. Social knowledge sharing. Stale info handling.

**Phase 3:** Line of sight (no see-through-walls). Memory decay over time. Map items.
