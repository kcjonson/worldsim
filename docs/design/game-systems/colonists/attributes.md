# Colonist Attributes

**Status:** Design  
**Created:** 2024-12-04  
**MVP Status:** See [MVP Scope](../mvp-scope.md) — Basic attributes in Phase 1

---

## Overview

Colonists have personality traits, physical attributes, and skills that affect their behavior and capabilities. This document covers **static attributes** — traits that define who the colonist is.

For dynamic status (needs, mood), see [Needs System](./needs.md).

---

## Personality (Core Attributes)

These are fixed traits assigned at colonist creation.

### Physical Attributes

| Attribute | Effect |
|-----------|--------|
| Strength | Carrying capacity, melee power, physical labor speed |
| Agility | Movement speed, reaction time, dexterity for fine work |
| Intelligence | Learning speed, problem solving, research ability |

### Mental/Social Attributes

| Attribute | Effect |
|-----------|--------|
| Civility | Social grace, politeness, conflict avoidance |
| Sanity | Mental stability, breakdown resistance |
| Kindness | Empathy, willingness to help others |
| Work Ethic | Task dedication, resistance to distraction |
| Social Need | How much interaction they require (high = extrovert) |

### Skills

| Property | Description |
|----------|-------------|
| Starter Skill Level | Initial proficiency in each skill |
| Skill Affinity | Natural talent for specific skills, affects learning rate and fulfillment mood |

See [Skills System](./skills.md) for full skill progression mechanics.

---

## Backstory

Each colonist has a procedurally generated backstory affecting:
- Starting skill levels
- Skill affinities (what they enjoy)
- Default work priorities
- Personality trait tendencies

**Example:** A colonist with a "Farmer" backstory might have:
- High starting Farming skill
- Farming affinity (mood boost when farming)
- Farming work priority boosted by default

---

## Traits

Traits are special modifiers beyond the core attributes. Each colonist has 1-3 traits.

**Example Traits:**
- "Hard Worker" — All work slightly faster
- "Night Owl" — Works better at night, sleep need shifts
- "Gourmand" — Higher mood from good food, lower from bad
- "Ascetic" — Doesn't care about room quality
- "Volatile" — Mood swings more dramatically

Traits create differentiation and emergent storytelling.

---

## Physical Condition

Beyond static attributes, colonists have physical conditions that change:

| Condition | Description |
|-----------|-------------|
| Injuries | Wounds, diseases, chronic conditions |
| Immediate Effects | Wet, cold, hot, in darkness, etc. |

These affect capabilities and mood but are not "attributes" per se.

---

## Related Documents

- [Needs System](./needs.md) — Dynamic needs and mood
- [AI Behavior](./ai-behavior.md) — How attributes influence decisions
- [Skills System](./skills.md) — Skill progression and learning
- [Work Priorities](./work-priorities.md) — How backstory affects work defaults

---

## Historical Addendum

This document is derived from the original docs/design/mechanics/colonists.md file. The original contained both static attributes AND dynamic status (needs, mood). These have been split:

- **Static attributes** → This file (attributes.md)
- **Dynamic status/needs** → [needs.md](./needs.md)

### Original "Dynamic Status" Section (Now in needs.md)

The original file had:

```
### Physical Status
| Status | Range | Effect |
|--------|-------|--------|
| Energy | 0-100% | Below 30% seeks sleep, 0% = collapse |
| Hunger | 0-100% | Below 50% seeks food, 0% = starvation |
...
```

This content is now in [needs.md](./needs.md).

### Original "Mood and Breakdown" Section (Now in needs.md)

The original file had mood calculation and breakdown thresholds. This is now consolidated in [needs.md](./needs.md) to avoid duplication.

### Consolidation Date
- **2024-12-04:** Split mechanics/colonists.md into attributes.md (static) + needs.md (dynamic)
