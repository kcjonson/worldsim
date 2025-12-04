# Colonist Systems

**Status:** Design  
**MVP Status:** See [MVP Scope](../mvp-scope.md)

---

## Overview

This folder contains all design documents related to colonist behavior and mechanics. Colonists are the core agents of the game — autonomous characters that make decisions, have needs, learn skills, and perform work.

---

## Documents

### Core Behavior

| Document | Description |
|----------|-------------|
| [AI Behavior](./ai-behavior.md) | Decision hierarchy, task selection, autonomous behavior |
| [Needs System](./needs.md) | Physical needs, mood contributors, breakdown thresholds |
| [Memory System](./memory.md) | What colonists know, how they learn locations |

### Character Definition

| Document | Description |
|----------|-------------|
| [Attributes](./attributes.md) | Static personality traits, backstory |
| [Skills](./skills.md) | Skill progression, learning methods, knowledge transfer |

### Work & Interaction

| Document | Description |
|----------|-------------|
| [Work Priorities](./work-priorities.md) | Personal work settings, simple/detailed modes |

---

## System Dependencies

```
AI Behavior
    ├── Needs System (triggers tiers 3 and 5)
    ├── Memory System (filters available tasks)
    ├── Work Priorities (tier 6 behavior)
    └── Entity Capabilities (need fulfillment)

Memory System
    └── AI Behavior (constrains task selection)

Skills
    └── Work Priorities (unlocks work types)
    └── Attributes (affects learning rate)
```

---

## MVP Summary

For the first playable prototype:

- **Included:** Hunger, Thirst, Energy, Bladder needs; AI tiers 5-7; basic task queue
- **Deferred:** Panic/Breakdown, Hygiene/Recreation, player control modes, skills

See [MVP Scope](../mvp-scope.md) for complete details.

---

## Related Topics

- [Entity Capabilities](../world/entity-capabilities.md) — How entities fulfill needs
- [Player Control](../features/player-control.md) — AI tier 4 behavior
- [Storage System](../features/storage-system.md) — Hauling work type
