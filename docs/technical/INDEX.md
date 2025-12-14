# Technical Design Documents Index

## Overview

World-sim is a C++20 game project built as a monorepo of independent libraries. The architecture emphasizes modularity, testability, and clean dependency hierarchies.

**What belongs here:**
- Architecture and system design
- Implementation details (algorithms, data structures, protocols)
- Tools and infrastructure
- Performance optimization
- Technical patterns and conventions

**What belongs in design/:**
- Player-facing game features and mechanics
- UI/UX from the player's perspective
- Game systems from a design perspective

---

## Quick Links

- **[Library Decisions](./library-decisions.md)** — All library choices in one place
- **[Monorepo Structure](./monorepo-structure.md)** — Project organization
- **[C++ Coding Standards](./cpp-coding-standards.md)** — Style guide

---

## Architecture Overview

### Application Architecture

```
Applications:
  1. Game Server    - Backend game logic (headless, no rendering)
  2. Game Client    - Player-facing game (rendering, UI, audio)
  3. UI-Sandbox     - Library development sandbox
  4. Developer Client - Browser-based monitoring dashboard
```

### Port Assignments

| Port | Service | Protocol | Purpose |
|------|---------|----------|---------|
| 9000 | Game Server ↔ Client | WebSocket | Gameplay sync (60 Hz) |
| 8080 | Game Server Developer Server | HTTP/SSE | Observability |
| 8081 | UI-Sandbox Developer Server | HTTP/SSE | Observability |
| 8082 | Game Client Developer Server | HTTP/SSE | Observability |

---

## Technical Documents by Topic

### Core Systems

| Document | Description |
|----------|-------------|
| [Monorepo Structure](./monorepo-structure.md) | Library organization and dependencies |
| [Build System](./build-system.md) | CMake + vcpkg configuration |
| [C++ Coding Standards](./cpp-coding-standards.md) | Style guide and best practices |
| [Technical Notes](./technical-notes.md) | Research notes, open questions |

### Engine Patterns

| Document | Description | Priority |
|----------|-------------|----------|
| [ECS Patterns](./ecs-patterns.md) | std::variant for polymorphic components | Active |
| [String Hashing](./string-hashing.md) | Fast string→int conversion | Implement Now |
| [Logging System](./logging-system.md) | Structured logging with categories | Implement Now |
| [Memory Arenas](./memory-arenas.md) | Fast temporary allocations | Implement Soon |
| [Resource Handles](./resource-handles.md) | Safe resource references | Implement Soon |
| [Diagnostic Drawing](./diagnostic-drawing.md) | Immediate mode debug draw | Implement Later |
| [Debugging Strategy](./debugging-strategy.md) | Multi-agent hypothesis exploration + disciplined investigation | Active |

### UI Framework

| Document | Description |
|----------|-------------|
| [Event System](./ui-framework/event-system.md) | Input event propagation and consumption |
| [SDF Rendering](./ui-framework/sdf-rendering.md) | GPU-based primitive rendering (planned) |

### Networking & Multiplayer

| Document | Description |
|----------|-------------|
| [Multiplayer Architecture](./multiplayer-architecture.md) | Client/server design for colony sim |
| [Process Management](./process-management.md) | Client spawns/monitors server process |

### Observability & Developer Tools

| Document | Description |
|----------|-------------|
| [Observability Overview](./observability/INDEX.md) | Complete system overview |
| [Developer Server](./observability/developer-server.md) | HTTP/SSE streaming from applications |
| [Developer Client](./observability/developer-client.md) | External TypeScript/Vite web app |
| [UI Inspection](./observability/ui-inspection.md) | UI hierarchy and hover inspection |

### Vector Graphics System

**Complete documentation:** [Vector Graphics Index](./vector-graphics/INDEX.md)

| Document | Description |
|----------|-------------|
| [Architecture](./vector-graphics/architecture.md) | Four-tier rendering system |
| [Asset Pipeline](./vector-graphics/asset-pipeline.md) | SVG asset workflow |
| [Tessellation Options](./vector-graphics/tessellation-options.md) | Library comparison |
| [SVG Parsing Options](./vector-graphics/svg-parsing-options.md) | Parser comparison |
| [Rendering Backend Options](./vector-graphics/rendering-backend-options.md) | Renderer comparison |
| [Performance Targets](./vector-graphics/performance-targets.md) | Budgets and profiling |

### Asset System

**Complete documentation:** [Asset System Index](./asset-system/README.md)

| Document | Description |
|----------|-------------|
| [Overview](./asset-system/README.md) | Simple vs procedural assets |
| [Asset Definitions](./asset-system/asset-definitions.md) | XML format, inheritance |
| [Entity Placement](./entity-placement-system.md) | Biome rules, distribution |
| [Lua Scripting API](./asset-system/lua-scripting-api.md) | Procedural generation |

### World & Game

| Document | Description |
|----------|-------------|
| [World Generation Architecture](./world-generation-architecture.md) | Pluggable world generators |
| [Chunk Management System](./chunk-management-system.md) | Infinite 2D world from 3D sphere |
| [3D to 2D Sampling](./3d-to-2d-sampling.md) | Converting spherical world to tiles |
| [Flat Tile Storage Refactor](./flat-tile-storage-refactor.md) | Proposed: Replace layered tiles with flat array |
| [Ground Textures](./ground-textures.md) | SVG tile patterns with GPU rasterization cache |

### Graphics & Rendering

| Document | Description |
|----------|-------------|
| [Renderer Architecture](./renderer-architecture.md) | OpenGL abstraction |
| [Resource Management](./resource-management.md) | Textures, shaders, fonts |

---

## Related Documentation

- [Game Design Documents](/docs/design/INDEX.md) — What we're building
- [Development Log](/docs/development-log/README.md) — Implementation history
- [Project Status](/docs/status.md) — Current work
- [Workflows](/docs/workflows.md) — Common development tasks
