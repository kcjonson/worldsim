# Colonist Systems Design Documents

**Status:** Design  
**Created:** 2024-12-04

## Overview

This folder contains design documents for colonist behavior and related game systems. These define WHAT the systems do from a player perspective, not HOW they're implemented.

## Core Systems

### [Colonist AI](./colonist-ai.md)
Decision hierarchy, task selection, autonomous behavior. The brain that makes colonists act.

### [Needs System](./needs-system.md)
Physical needs (hunger, thirst, etc.) and mood contributors. What drives colonist behavior.

### [Colonist Memory](./colonist-memory.md)
**Core differentiator.** Colonists only know what they've seen. Knowledge acquisition and sharing.

### [Work Priorities](./work-priorities.md)
Personal work settings. Simple (checkboxes) and detailed (numerical) modes.

### [Player Control](./player-control.md)
Observation, command, and direct control modes. How players interact with colonists.

### [Entity Capabilities](./entity-capabilities.md)
How entities advertise what they can do (edible, sleepable, etc.). Connects needs to fulfillment.

### [Storage System](./storage-system.md)
Per-item priorities, max amounts, priority-based redistribution.

## MVP Scope Summary

For the first playable prototype:

**Colonist Behavior:**
- Autonomous operation (leave running, watch it work)
- Four needs: Hunger, Thirst, Energy, Bladder
- Sleep on ground, bathroom creates Bio Pile
- One work type: Harvest Wild (foraging)
- Wander when idle

**World Entities:**
- Berry Bush (Edible, Harvestable)
- Pond (Drinkable)
- Ground (Sleepable fallback, Toilet fallback)
- Bio Pile (created by bathroom)

**Player Interaction:**
- Observation only (no control modes yet)
- View colonist info and task queue

**NOT in MVP:**
- Panic / Breakdown
- Player control modes
- Hygiene, Recreation, Shelter needs
- Construction, Crafting, Complex work
- Beds, Toilets, Storage containers

## System Dependencies

```
Colonist AI
    ├── Needs System (tier 3, 5 triggers)
    ├── Memory System (filters task selection)
    ├── Work Priorities (tier 6 behavior)
    └── Entity Capabilities (finding fulfillment)

Player Control
    └── Colonist AI (tier 4, suspends queue)

Storage System
    └── Entity Capabilities (Haulable, Storable)
    └── Work Priorities (Hauling tasks)
```

## Related Documents

- [Colonist Attributes](../../mechanics/colonists.md) - Personality, stats, backstory
- [Skills System](../../mechanics/skills.md) - Learning, tiers, knowledge transfer
- [Rooms](../../mechanics/rooms.md) - Room types and bonuses
