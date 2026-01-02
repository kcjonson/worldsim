# Work Priorities System

**Status:** Design (Work in Progress)  
**Created:** 2024-12-04  
**MVP Status:** See [MVP Scope](../mvp-scope.md) — One work type (Harvest Wild) in Phase 1

---

## Overview

Each colonist has personal work priority settings. When their needs are satisfied, they perform work based on these settings.

The system supports two UI modes: simple (checkboxes) and detailed (numerical priorities). Both control the same underlying behavior.

---

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

---

## Work Categories

> **Note:** This list is a work in progress and will evolve as the game develops.

### Emergency (Tier 1)

**Cannot be disabled** — all colonists respond to emergencies.

| Work Type | Description |
|-----------|-------------|
| Rescue | Carry incapacitated colonist to safety |
| Firefight | Extinguish fires |

### Medical (Tier 2)

| Work Type | Description | Skill Required |
|-----------|-------------|----------------|
| Patient Care | Bring food to sick, tend minor wounds | None |
| Doctoring | Treat injuries, apply bandages | Medicine |
| Surgery | Perform operations | Medicine (high) |

### Farming (Tier 3)

| Work Type | Description | Skill Required |
|-----------|-------------|----------------|
| Plant | Sow seeds, tend crops | Farming |
| Harvest Crops | Gather mature crops | Farming |
| Harvest Wild | Forage wild plants | None |
| Animal Care | Feed, train animals | Ranching |

### Crafting (Tier 4)

Sub-categories based on workbench type:

| Work Type | Description | Skill Required |
|-----------|-------------|----------------|
| Woodworking | Craft at wood workbench | Woodworking |
| Smithing | Craft at forge/smithy | Smithing |
| Tailoring | Craft at tailoring bench | Tailoring |
| Cooking | Prepare food at kitchen | Cooking |
| Electronics | Craft at electronics bench | Electronics |
| Masonry | Craft at stonecutting bench | Masonry |

**Note:** Crafting work types are dynamically generated from workbench definitions.

### Construction (Tier 5)

| Work Type | Description | Skill Required |
|-----------|-------------|----------------|
| Build | Construct new structures | Construction |
| Repair | Fix damaged structures | Repair + material skill |
| Deconstruct | Tear down structures | Construction |

### Hauling (Tier 6)

| Work Type | Description | Skill Required |
|-----------|-------------|----------------|
| Haul to Storage | Move items to stockpiles | None |
| Haul to Workbench | Deliver materials to crafting | None |
| Haul to Construction | Deliver materials to build sites | None |

**Note:** Hauling has no skill. Speed/capacity based on Strength attribute.

### Cleaning (Tier 7)

| Work Type | Description | Skill Required |
|-----------|-------------|----------------|
| Clean Indoor | Clean floors, remove debris | None |
| Clean Outdoor | Clear outdoor areas | None |

### Research/Learning (Optional Tier)

| Work Type | Description | Skill Required |
|-----------|-------------|----------------|
| Self-Study | Research independently | Intelligence |
| Write Manual | Document skills for others | Skill being documented |
| Teach | Train other colonists | Skill being taught |

---

## Skill Requirements

Some work requires skills. If colonist lacks the skill, that work type is locked (cannot enable).

**Example:** Doctoring requires Medicine skill. Colonist with no Medicine skill sees "Doctoring: Requires Medicine" and cannot enable it.

As colonists learn skills (see [Skills System](./skills.md)), new work types unlock.

---

## Task Selection Algorithm

The task selection uses the [Global Task Registry](./task-registry.md) with per-colonist priority scoring:

```python
def select_work_task(colonist, registry, config):
    # Get all tasks from memory-sourced global registry
    # (only entities at least one colonist has seen)
    all_tasks = registry.get_unreserved_tasks()

    # Filter to tasks this colonist can do
    candidate_tasks = []
    for task in all_tasks:
        work_type = WorkTypeRegistry.get(task.workTypeDefName)

        # Check if work type is enabled for this colonist
        if not colonist.work_priorities.is_enabled(work_type):
            continue

        # Check skill requirement
        if work_type.skillRequired:
            if not colonist.skills.meets_requirement(work_type.skillRequired, work_type.minSkillLevel):
                continue

        candidate_tasks.append(task)

    # Score each candidate (see priority-config.md for formula)
    scored_tasks = []
    for task in candidate_tasks:
        priority = task.basePriority                              # From work type band
        priority += config.calculate_distance_bonus(colonist.position, task.position)
        priority += config.calculate_skill_bonus(colonist.skills.get(task.skillRequired))
        priority += config.calculate_chain_bonus(colonist.current_chain, task)
        priority += config.calculate_in_progress_bonus(colonist.current_task, task)
        scored_tasks.append((task, priority))

    # Sort by priority (highest first)
    scored_tasks.sort(key=lambda x: x[1], reverse=True)

    if scored_tasks:
        best_task, _ = scored_tasks[0]
        registry.reserve(best_task, colonist)
        return best_task

    return None  # No work available → Wander
```

See [Task Registry](./task-registry.md) for the global registry architecture and [Priority Config](./priority-config.md) for bonus calculations.

---

## Default Priorities

New colonists get defaults based on:
- **Backstory:** Former farmer → Farming boosted
- **Skill Affinity:** Natural talent for smithing → Smithing boosted
- **Traits:** "Hard Worker" → All work slightly boosted

Player can always override.

---

## Related Documents

- [AI Behavior](./ai-behavior.md) — Tier 6 in decision hierarchy
- [Skills System](./skills.md) — Skill-based work unlocking
- [Memory System](./memory.md) — Constrains available tasks
- [Attributes](./attributes.md) — Backstory affects defaults
- [Task Registry](./task-registry.md) — Global task list architecture
- [Work Types Config](./work-types-config.md) — XML format for defining work types
- [Priority Config](./priority-config.md) — Tunable priority weights and bonuses
- [Task Chains](./task-chains.md) — Multi-step tasks like haul

---

## Historical Addendum

This document was moved from `docs/design/systems/work-priorities.md` during the 2024-12-04 documentation reorganization.

### Original MVP Scope Section (Removed)

```
## MVP Scope

**Phase 1:** Simple mode only. Two work types: Harvest Wild, Haul to Storage.

**Phase 2:** Detailed mode. Full farming category. Construction.

**Phase 3:** Crafting categories. Skill-based unlocking.
```

This is now consolidated in [MVP Scope](../mvp-scope.md).
