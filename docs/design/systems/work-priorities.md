# Work Priorities System

**Status:** Design  
**Created:** 2024-12-04

## Overview

Each colonist has personal work priority settings. When their needs are satisfied, they perform work based on these settings.

The system supports two UI modes: simple (checkboxes) and detailed (numerical priorities). Both control the same underlying behavior.

## Priority Modes

### Simple Mode (Default)

Each work category is ON or OFF:

```
☑ Emergency
☑ Medical  
☑ Farming
☐ Crafting     ← Disabled
☑ Construction
☑ Hauling
☑ Cleaning
```

Categories are evaluated in fixed order. First enabled category with available work is selected.

### Detailed Mode

Each work type has a numerical priority (1-9, or disabled):

```
1 = Do first
5 = Normal
9 = Do last
- = Never do
```

**Example:**
- Harvest Crops: 2 (high priority)
- Plant Crops: 5 (normal)
- Haul to Storage: 7 (low priority)

All enabled work types are sorted by priority. Within same priority, distance determines selection.

### Toggle Between Modes

Player can switch modes anytime. This is a UI preference, not a fundamental data change.

**Design Goal (Fluffy's Work Tab style):** Start with simple checkboxes. Players who want control can expand to detailed view showing all sub-categories with individual priorities.

## Work Categories

### Emergency (Always Active)
Cannot be disabled. All colonists respond.
- Rescue (carry incapacitated colonist to safety)
- Firefight (extinguish fires)

### Medical
- Patient Care (bring food to sick, tend minor wounds)
- Doctoring (treat injuries, requires skill)
- Surgery (operations, requires high skill)

### Farming
- Plant (sow seeds)
- Harvest Crops (gather from farms)
- Harvest Wild (forage wild plants)
- Animal Care (feed, train)

### Crafting
Sub-categories based on workbench type:
- Woodworking
- Smithing
- Tailoring
- Cooking
- Electronics
- Masonry
- (etc - expands as workbenches are added)

### Construction
- Build (new structures)
- Repair (fix damage)
- Deconstruct (tear down)

### Hauling
- Haul to Storage
- Haul to Workbench (deliver materials)
- Haul to Construction (deliver building materials)

### Cleaning
- Clean Indoor
- Clean Outdoor

### Research/Learning
- Self-Study
- Write Manual
- Teach Others

## Skill Requirements

Some work requires skills. If colonist lacks the skill, that work type is locked (cannot enable).

**Example:** Doctoring requires Medicine skill. Colonist with no Medicine skill sees "Doctoring: Requires Medicine" and cannot enable it.

As colonists learn skills (see [Skills System](../../mechanics/skills.md)), new work types unlock.

## Task Selection

When colonist is ready to work:

1. Get all enabled work types, sorted by priority
2. For each work type, look for available tasks the colonist knows about
3. Skip tasks that are reserved by others
4. Skip tasks colonist can't reach
5. Select the best remaining task

**Ties:** Same priority = choose nearest task

## Default Priorities

New colonists get defaults based on:
- **Backstory:** Former farmer → Farming boosted
- **Skill Affinity:** Natural talent for smithing → Smithing boosted
- **Traits:** "Hard Worker" → All work slightly boosted

Player can always override.

## Hauling Note

Hauling has no skill requirement. Anyone can haul. Speed/capacity based on Strength attribute.

Hauling is typically lower priority than skilled work. The default ensures colonists do meaningful work before organizing inventory.

## MVP Scope

**Phase 1:** Simple mode only. Two work types: Harvest Wild, Haul to Storage.

**Phase 2:** Detailed mode. Full farming category. Construction.

**Phase 3:** Crafting categories. Skill-based unlocking.
