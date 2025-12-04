# MVP Entities

**Status:** Design  
**Created:** 2024-12-04

## Overview

Minimum entities needed for the first playable prototype. Goal: watch a colonist autonomously fulfill their needs without player intervention.

## Entity List

### Berry Bush

**Purpose:** Food source

**Capabilities:**
- Edible (nutrition: 0.3)
- Harvestable (yield: berries)

**Behavior:**
- Stationary flora
- Colonist walks to bush, eats directly OR harvests berries (loose items)
- For MVP: Eat directly, no harvesting/hauling

### Pond

**Purpose:** Water source

**Capabilities:**
- Drinkable (quality: clean)

**Behavior:**
- Stationary water body
- Colonist walks to edge, drinks
- Drinking increases bladder need

### Bio Pile

**Purpose:** Waste marker

**Capabilities:**
- (None - just exists as entity)

**Behavior:**
- Created when colonist relieves self outdoors
- Future: attracts flies, mood penalty if near base, can be cleaned up
- MVP: Just sits there

**Spawn Rules:**
When colonist needs bathroom outdoors:
1. Find spot that is outdoor
2. NOT adjacent to water
3. PREFER near existing Bio Pile (clustering)
4. PREFER away from buildings/traffic

### Ground (Implicit)

**Purpose:** Fallback for sleep and bathroom

**Capabilities:**
- Sleepable (quality: terrible, recovery: 0.5x)
- Toilet (fallback)

**Behavior:**
- Not a spawned entity - ground tiles implicitly have these capabilities
- Sleep on ground: slow recovery, mood penalty
- Bathroom on ground: creates Bio Pile, mood penalty

## Not In MVP

These come later:

| Entity | Phase | Purpose |
|--------|-------|---------|
| Bedroll | 2 | Better sleep |
| Bed | 3 | Good sleep |
| Latrine | 2 | Outdoor toilet |
| Toilet | 3 | Indoor toilet |
| Crops | 2 | Farmed food |
| Storage Crate | 2 | Item storage |
| Workbench | 3 | Crafting |

## Test Scenario

**Setup:**
- One colonist spawns at map center
- Several berry bushes scattered nearby
- One pond within walking distance
- Open ground for sleeping/bathroom

**Expected Behavior (leave running):**
1. Colonist wanders initially
2. Discovers berry bush and pond through sight
3. When hungry → walks to known berry bush → eats
4. When thirsty → walks to known pond → drinks
5. Drinking increases bladder need
6. When bladder urgent → finds outdoor spot → creates Bio Pile
7. When tired → sleeps on ground
8. Repeat forever

**Success Criteria:**
- Colonist survives indefinitely
- Task queue shows sensible decisions
- No player intervention required
- Colonist discovers new entities through wandering
