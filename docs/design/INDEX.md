# Game Design Documents Index

## Overview

This directory contains game design documents describing **what** we're building from a player-facing perspective. For technical implementation details, see [Technical Design Documents](/docs/technical/INDEX.md).

**What belongs here:**
- Player experience and game mechanics
- UI/UX from the player's perspective  
- Game systems (colonists, building, world, etc.)
- Gameplay features and content
- Player-facing requirements

**What does NOT belong here** (goes in `technical/`):
- Architecture and system design
- Implementation details (algorithms, data structures)
- Tools and infrastructure
- Performance optimization

---

## Quick Links

- **[MVP Scope](./mvp-scope.md)** — What's included in each development phase
- **[Game Overview](./game-overview.md)** — Core concept, world mechanics
- **[Visual Style](./visual-style.md)** — Art direction, aesthetics

---

## Game Systems

Organized by topic area. Each folder contains related design documents.

### Colonists

Location: `game-systems/colonists/`

Everything about colonist behavior, needs, and mechanics.

| Document | Description |
|----------|-------------|
| [README](./game-systems/colonists/README.md) | Overview and navigation |
| [AI Behavior](./game-systems/colonists/ai-behavior.md) | Decision hierarchy, autonomous behavior |
| [Needs System](./game-systems/colonists/needs.md) | Physical needs, mood, breakdowns |
| [Memory System](./game-systems/colonists/memory.md) | What colonists know |
| [Attributes](./game-systems/colonists/attributes.md) | Personality, backstory |
| [Skills](./game-systems/colonists/skills.md) | Learning, progression |
| [Work Priorities](./game-systems/colonists/work-priorities.md) | Work assignment |

### World

Location: `game-systems/world/`

World mechanics, rooms, resources, and entity interactions.

| Document | Description |
|----------|-------------|
| [Entity Capabilities](./game-systems/world/entity-capabilities.md) | How entities advertise what they do |
| [Rooms](./game-systems/world/rooms.md) | Room types, detection, bonuses |
| [Crafting](./game-systems/world/crafting.md) | Resource chains, recipes |

### Features

Location: `features/`

Discrete player-facing features.

| Document | Description |
|----------|-------------|
| [Game Start Experience](./features/game-start-experience.md) | Main menu → gameplay flow |
| [Player Control](./features/player-control.md) | Observation, command, direct control |
| [Storage System](./features/storage-system.md) | Item storage, priority hauling |
| [Vector Graphics](./features/vector-graphics/) | Asset workflow, animations |
| [Multiplayer](./features/multiplayer/) | Co-op, multiplayer design |
| [Debug Server](./features/debug-server/) | Developer tools (technical overlap) |

---

## Research

| Document | Description |
|----------|-------------|
| [Competitive Analysis](./research/competitive-analysis.md) | Analysis of similar games |

---

## Document Standards

### MVP References

Do NOT create "MVP Scope" sections in individual docs. Instead reference:

```markdown
**MVP Status:** See [MVP Scope](./mvp-scope.md) — This feature: Phase X
```

### Cross-References

Use relative paths for links within docs/:
```markdown
See [Needs System](./game-systems/colonists/needs.md)
```

### Historical Content

When consolidating or superseding documents, add a "Historical Addendum" section at the bottom preserving the original content.

---

## Related Documentation

- [Technical Design Documents](/docs/technical/INDEX.md) — How things are implemented
- [Development Log](/docs/development-log/README.md) — Implementation history
- [Project Status](/docs/status.md) — Current work
