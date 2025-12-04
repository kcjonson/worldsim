# Entity Capabilities

**Status:** Design  
**Created:** 2024-12-04

## Overview

Entities advertise what they can do through **capabilities**. Colonists look for entities with specific capabilities to fulfill needs or complete work.

**Example:** A colonist is hungry. They search their memory for entities with the "Edible" capability, find a berry bush, and go eat.

## How Capabilities Work

### Entity Defines Capabilities

Each entity type declares its capabilities in its definition:

**Berry Bush:**
- Edible (nutrition: 0.2, spoils: no)
- Harvestable (yield: berries, skill: none)

**Pond:**
- Drinkable (quality: clean)
- Washable

**Bed:**
- Sleepable (quality: good, recovery: 1.2x)

**Toilet:**
- Toilet (hygiene bonus: yes)

### Colonist Queries by Capability

When a colonist needs something, they search for entities matching the required capability.

"I'm hungry" → Search memory for Edible entities → Find nearest → Go eat

"I need sleep" → Search for Sleepable entities → Find nearest → Go sleep

### Capability Properties

Capabilities can have properties that affect how well they fulfill needs:

**Edible:**
- nutrition: How much hunger restored
- quality: Affects mood (lavish meal vs raw food)
- spoilable: Does it go bad?

**Sleepable:**
- quality: Affects mood while sleeping
- recovery: Multiplier on energy restoration rate

**Drinkable:**
- quality: Clean water vs dirty water (health effects)

## Standard Capabilities

### Need Fulfillment

| Capability | Fulfills Need | Examples |
|------------|---------------|----------|
| Edible | Hunger | Berries, meals, raw meat |
| Drinkable | Thirst | Pond, well, water bottle |
| Sleepable | Energy | Ground, bedroll, bed |
| Toilet | Bladder | Latrine, toilet, ground (fallback) |
| Washable | Hygiene | Pond, bath, shower |
| Recreational | Recreation | Horseshoe pit, chess table |

### Work Targets

| Capability | Work Type | Examples |
|------------|-----------|----------|
| Harvestable | Farming/Harvest | Crops, berry bushes, trees |
| Constructable | Construction | Blueprints, unfinished buildings |
| Repairable | Construction/Repair | Damaged structures |
| Craftable | Crafting | Recipes at workbenches |

### Item Properties

| Capability | Meaning | Examples |
|------------|---------|----------|
| Haulable | Can be picked up and moved | Resources, items |
| Storable | Can go in storage containers | Most items |
| Stackable | Multiple can occupy same slot | Resources |

## Capability Conflicts

Some capabilities interact:

**Edible + Spoilable:** Food quality degrades over time. Eventually becomes inedible or harmful.

**Drinkable + Dirty:** Drinking dirty water may cause illness.

## Ground as Fallback

Some capabilities have "ground" as a fallback option with penalties:

| Need | Preferred | Fallback | Penalty |
|------|-----------|----------|---------|
| Sleep | Bed | Ground | Slow recovery, mood penalty |
| Bladder | Toilet | Ground (outdoor) | Mood penalty, creates Bio Pile |

## Adding New Capabilities

Capabilities are defined in asset definitions (XML). Adding a new capability to an entity:

1. Add capability to entity's definition
2. Colonist AI automatically considers it when seeking that capability type

This supports modding - modders can create entities with new capability combinations.

## MVP Scope

**Phase 1:** Edible, Drinkable, Sleepable (ground only), Toilet (ground only)

**Phase 2:** Harvestable, Haulable, Storable. Actual furniture (beds, latrines).

**Phase 3:** Washable, Recreational, work-related capabilities.
