# Technical Design Documents Index

## Overview

World-sim is a C++20 game project built as a monorepo of independent libraries. The architecture emphasizes modularity, testability (especially for UI), and clean dependency hierarchies. Libraries are designed to potentially become standalone open-source projects in the future.

**What belongs here:**
- Architecture and system design (client/server, ECS, rendering)
- Implementation details (algorithms, data structures, protocols)
- Tools and infrastructure (debug server, asset pipeline, build system)
- Performance optimization and profiling
- Technical patterns and conventions (logging, memory management, etc.)

**What belongs in design/:**
- Player-facing game features and mechanics
- UI/UX from the player's perspective
- Game systems from a design perspective (not implementation)
- Player-facing requirements

## Core Architecture Principles

- **Clear dependency layers**: Foundation → Renderer → UI/World → Game Systems → Engine
- **Testability first**: UI must be inspectable and testable by AI agents
- **Minimal coupling**: Each library should be self-contained with clear interfaces
- **Configuration-driven**: Use JSON configs where possible for flexibility

## Technical Design Documents

### Core Systems
- [Monorepo Structure](./monorepo-structure.md) - Library organization and dependencies
- [UI Testability Strategy](./ui-testability-strategy.md) - Making C++ UI inspectable like web apps
- [Vector Asset Pipeline](./vector-asset-pipeline.md) - SVG-based assets with procedural variation
- [Build System](./build-system.md) - CMake + vcpkg configuration
- [C++ Coding Standards](./cpp-coding-standards.md) - Style guide and best practices
- [Technical Notes](./technical-notes.md) - Research notes, open questions, and technical references

### Networking & Multiplayer
- [Multiplayer Architecture](./multiplayer-architecture.md) - Client/server design for colony sim
- [Process Management](./process-management.md) - Client spawns/monitors server process
- [HTTP Debug Server](./http-debug-server.md) - Real-time metrics and debugging via SSE

### Engine Patterns (Implement These!)
- [String Hashing](./string-hashing.md) - Fast string→int conversion for lookups (Implement Now)
- [Logging System](./logging-system.md) - Structured logging with categories (Implement Now)
- [Memory Arenas](./memory-arenas.md) - Fast temporary allocations (Implement Soon)
- [Resource Handles](./resource-handles.md) - Safe resource references (Implement Soon)
- [Debug Rendering](./debug-rendering.md) - Immediate mode debug draw (Implement Later)

### World & Game
- [World Generation Architecture](./world-generation-architecture.md) - Pluggable world generators
- [Chunk Management System](./chunk-management-system.md) - Infinite 2D world from 3D sphere
- [3D to 2D Sampling](./3d-to-2d-sampling.md) - Converting spherical world to tile data

### Graphics & Rendering
- [Renderer Architecture](./renderer-architecture.md) - OpenGL abstraction, 2D/3D rendering
- [Resource Management](./resource-management.md) - Textures, shaders, fonts
- [Vector Asset Pipeline](./vector-asset-pipeline.md) - SVG loading, rasterization, caching

### Engine & Application
- [Scene Management](./scene-management.md) - Scene lifecycle and transitions
- [Configuration System](./config-system.md) - JSON-based game configuration
- [Application Lifecycle](./application-lifecycle.md) - Fast splash screen, lazy loading

## System Diagrams

*To be added as architecture evolves*

## Related Documentation

- [Development Status](/docs/status.md) - Current work and decisions
- [Game Design Documents](/docs/design/INDEX.md) - Feature requirements and player experience
