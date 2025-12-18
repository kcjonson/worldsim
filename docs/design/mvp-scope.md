# MVP Scope Definition

**Status:** Active  
**Last Updated:** 2024-12-04

This document is the **single source of truth** for MVP scope. All other documents should reference this file rather than defining their own MVP sections.

---

## MVP Goal

**First Playable Prototype:** A colonist that autonomously takes care of itself. Leave the game running and watch it work.

---

## In Scope for MVP

### Colonist Behavior
- Autonomous operation (no player intervention needed)
- **Four needs:** Hunger, Thirst, Energy, Bladder
- Decision hierarchy: Critical Needs → Actionable Needs → Gather Food → Wander
- Sleep on ground (no beds yet)
- Bathroom creates Bio Pile (no toilets yet)
- One work type: Gather Food (proactive harvesting when inventory empty)
- Wander behavior when idle
- Basic task queue display

### World Entities
| Entity | Capabilities |
|--------|--------------|
| Berry Bush | Edible, Harvestable |
| Pond | Drinkable |
| Ground | Sleepable (fallback), Toilet (fallback) |
| Bio Pile | Created by bathroom |

### Player Interaction
- Observation only (no control modes)
- View colonist info panel
- View task queue

### Technical
- Basic ECS integration
- Basic behavior scripting
- Single colonist (multi-colonist deferred)
- No saving/loading
- No UI beyond info panels

---

## NOT in MVP (Deferred)

### Colonist Systems
- Panic / Breakdown tiers
- Player control modes (Command, Direct Control)
- Hygiene, Recreation needs
- Temperature/thermal comfort (environmental modifier; affects sleep/energy, no separate Shelter need)
- Mood system (beyond basic need satisfaction)
- Social interactions
- Memory sharing between colonists

### World Features
- Construction system
- Crafting system
- Complex work types (Medical, Construction, Crafting, etc.)
- Beds, Toilets, Storage containers
- Multiple colonists
- Animals/threats

### Technical
- Multiplayer
- Save/Load
- Procedural world generation integration
- Full UI framework

---

## Phase Progression

### Phase 1 (MVP) — Current Target
As defined above.

### Phase 2
- Hygiene, Recreation needs
- Temperature/thermal comfort hooked into sleep quality and energy decay
- Beds, Toilets, Washing
- Hauling work type
- Basic mood with thoughts
- Multiple colonists
- Simple memory sharing

### Phase 3
- Panic / Breakdown behaviors
- Player control modes
- Full mood system with all contributors
- Construction basics
- Crafting basics

### Phase 4+
- Full work category system
- Skills and learning
- Complex crafting chains
- Threats and combat
- Multiplayer

---

## How to Use This Document

**When writing design docs:** Do NOT create "MVP Scope" sections. Instead, write:

```markdown
**MVP Status:** See [MVP Scope](/docs/design/mvp-scope.md)
- This feature: Included in Phase 1
```

Or:

```markdown
**MVP Status:** See [MVP Scope](/docs/design/mvp-scope.md)
- This feature: Deferred to Phase 2
```

This keeps all MVP decisions in one place and prevents drift.
