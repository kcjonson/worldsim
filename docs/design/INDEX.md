# Game Design Documents Index

## Overview

This directory contains game design documents, requirements, and planning documents for the world-sim project. These design docs describe *what* needs to be built from a **player-facing game design perspective**, while technical design docs describe *how* it's implemented.

**What belongs here:**
- Player experience and game mechanics
- UI/UX from the player's perspective
- Game systems (building, colonists, raids, etc.)
- Gameplay features and content
- Player-facing requirements

**What belongs in technical/:**
- Architecture and system design (client/server, ECS, etc.)
- Implementation details (algorithms, data structures)
- Tools and infrastructure (debug server, asset pipeline)
- Performance optimization
- Technical systems (networking, rendering, etc.)

**Important**: Game design docs may differ from actual implementation. When implementation diverges, see the corresponding Technical Design Document for what was actually built and why.

## Organization

Documents can be organized in subdirectories (`features/`, `requirements/`) or at the top level - use whatever makes sense as the project grows. Prefer many short focused docs (1-5 pages) over long comprehensive documents.

## Core Design Documents

### Game Concept
- [Game Overview](./game-overview.md) - Core concept, backstory options, world mechanics, and similar games
- [UI Art Style](./ui-art-style.md) - Visual style guide ("High tech cowboy")

### Visual Design & Procedural Tile System
- [Visual Style](./visual-style.md) - Overall visual art direction, color philosophy, aesthetic goals
- [Biome Ground Covers](./biome-ground-covers.md) - Physical ground surface types (grass, sand, rock, water)
- [Biome Influence System](./biome-influence-system.md) - Percentage-based biome blending, natural ecotones
- [Tile Transitions](./tile-transitions.md) - Visual appearance of biome transition zones
- [Procedural Variation](./procedural-variation.md) - Creating unique tiles while maintaining recognizability

### Game Mechanics
- [Colonist Attributes](./mechanics/colonists.md) - Character stats, needs, and personality traits
- [Skills and Talents](./mechanics/skills.md) - Skill system, learning mechanics, and task priorities
- [Room Types](./mechanics/rooms.md) - Room mechanics, types, bonuses, and ownership
- [Crafting and Resources](./mechanics/crafting.md) - Resource types and crafting mechanics

## Feature Designs

*Note: Many of these are planned but not yet written. Add them as needed when designing features.*

### Vector Graphics & SVG Assets
- [Vector Graphics Overview](./features/vector-graphics/README.md) - All SVG use cases: decorations, texture patterns, animation
- [SVG Decorations](./features/vector-graphics/svg-decorations.md) - Placed objects (flowers, trees, entities)
- [SVG Texture Patterns](./features/vector-graphics/svg-texture-patterns.md) - Fill patterns for code-drawn shapes (brick, concrete, wood)
- [Animated Vegetation](./features/vector-graphics/animated-vegetation.md) - Grass swaying, tree movement, environmental response
- [Environmental Interactions](./features/vector-graphics/environmental-interactions.md) - Trampling, harvesting, wind effects

### Core Application
- [Application Flow](./features/application-flow/README.md) - Splash screen, main menu, scene transitions *(planned)*
- [Main Menu](./features/main-menu/README.md) - Menu options and navigation *(planned)*

### World Creation
- [World Creation Flow](./features/world-creation/README.md) - Create world scene overview *(planned)*
- [3D Planet Rendering](./features/world-creation/planet-rendering.md) - Interactive 3D planet preview *(planned)*
- [World Parameters](./features/world-creation/parameters.md) - Generation controls and UI *(planned)*
- [World Generation](./features/world-creation/generation.md) - Generation process and progress display *(planned)*

### 2D Game
- [Game Scene](./features/game/README.md) - Top-down 2D tile-based gameplay *(planned)*
- [Camera Controls](./features/game/camera-controls.md) - Pan, zoom, edge scrolling *(planned)*
- [Infinite World](./features/game/infinite-world.md) - Chunk loading from spherical world *(planned)*

### UI Framework
- [UI Component Library](./features/ui-framework/README.md) - Overview of UI system from player perspective *(planned)*

## Requirements

- [Functional Requirements](./requirements/functional.md) - Core functionality
- [Non-Functional Requirements](./requirements/non-functional.md) - Performance, testability, maintainability
- [Performance Requirements](./requirements/performance.md) - Frame rate, load times, memory

## Related Technical Documentation

For technical implementation details, see [Technical Design Documents](/docs/technical/INDEX.md)

**Technical systems** (not player-facing game design):
- [Multiplayer Architecture](/docs/technical/multiplayer-architecture.md) - Client/server implementation
- [HTTP Debug Server](/docs/technical/http-debug-server.md) - Development tooling
- [Vector Asset Pipeline](/docs/technical/vector-asset-pipeline.md) - SVG rendering system
- [UI Testability Strategy](/docs/technical/ui-testability-strategy.md) - UI testing infrastructure
