# Storage System

**Status:** Design  
**Created:** 2024-12-04

## Overview

Items can be stored in containers. Each container has per-item priority settings that control what goes where and in what order.

## Container Configuration

Each container maintains a list of allowed items with individual settings:

**Example: Textile Bin**

| Item | Priority | Max Amount |
|------|----------|------------|
| Cloth | High | 45 |
| Wool | Medium | Unlimited |
| Leather | Low | 20 |

**Default Behavior:** When a container is placed, it allows everything it CAN hold, at Medium priority, with unlimited amounts. Player then customizes.

## Priority Levels

Five priority levels for each item type:

| Level | Name | Behavior |
|-------|------|----------|
| 1 | Critical | Fill first, pull from lower |
| 2 | High | Fill before normal |
| 3 | Medium | Default |
| 4 | Low | Fill last |
| - | Not Allowed | Cannot store this item |

## Priority-Based Movement

Higher priority containers "pull" items from lower priority ones.

**Scenario:**
1. Crate A has 10 wood at Medium priority
2. Player sets Crate B to High priority for wood
3. System creates haul task: move wood from A to B
4. Result: All wood moves to Crate B

**With Max Amounts:**
1. Crate A has 10 wood at Medium
2. Crate B set to High priority, max 15 wood
3. Crate B already has 10 wood
4. Only 5 wood moves (hits max of 15)
5. Remaining 5 stays in Crate A

## Allowed vs Not Allowed

Containers only show items they CAN hold in their configuration UI.

**Textile Bin** shows: Cloth, Wool, Leather, Thread, etc.

**Food Storage** shows: Berries, Meat, Meals, etc.

Player cannot add "Wood" to a Textile Bin - it's not in the list.

**Not Allowed** means: remove from the allowed list. The item will not be stored here even if space exists.

## Hauling Task Generation

The storage system generates hauling tasks:

**New Item Created:**
- Item spawns on ground (harvested berries, crafted goods)
- System finds appropriate container (allows item, has space, best priority)
- Creates "haul to storage" task

**Priority Changed:**
- Player increases priority on a container
- System checks if items elsewhere should move
- Creates redistribution tasks

**Max Changed:**
- Player lowers max amount
- Container now over limit
- System finds alternate storage
- Creates "move excess" tasks

## No Forbidden State

Unlike some games, items are never "forbidden." All items on the ground are fair game for hauling.

**Rationale:** Simplifies the system. If you don't want something hauled, don't have storage that accepts it.

## Storage Types

Different containers hold different item categories:

| Container | Holds | Notes |
|-----------|-------|-------|
| Stockpile Zone | Anything | Open-air, basic |
| Crate | General goods | Enclosed, protected |
| Food Storage | Edibles | May have preservation |
| Textile Bin | Cloth, leather | |
| Material Rack | Wood, stone, metal | |

## MVP Scope

**Phase 1:** Stockpile zones only. Simple "allowed/not allowed" per item type. No priority levels.

**Phase 2:** Priority levels. Max amounts. Pull behavior.

**Phase 3:** Multiple container types. Preservation effects.
