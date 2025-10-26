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
- [Vector Asset Pipeline](./vector-asset-pipeline.md) - SVG-based assets with procedural variation
- [Build System](./build-system.md) - CMake + vcpkg configuration
- [C++ Coding Standards](./cpp-coding-standards.md) - Style guide and best practices
- [Technical Notes](./technical-notes.md) - Research notes, open questions, and technical references

### Observability & Developer Tools
- [Observability Overview](./observability/INDEX.md) - Complete system overview
- [Developer Server](./observability/developer-server.md) - HTTP/SSE streaming from applications
- [Developer Client](./observability/developer-client.md) - External TypeScript/Vite web app
- [UI Inspection](./observability/ui-inspection.md) - UI hierarchy and hover inspection
- [Logging System](./logging-system.md) - Structured logging with HTTP streaming
- [Diagnostic Drawing](./diagnostic-drawing.md) - In-viewport debug visualization

### Networking & Multiplayer
- [Multiplayer Architecture](./multiplayer-architecture.md) - Client/server design for colony sim
- [Process Management](./process-management.md) - Client spawns/monitors server process

### Engine Patterns (Implement These!)
- [String Hashing](./string-hashing.md) - Fast string→int conversion for lookups (Implement Now)
- [Logging System](./logging-system.md) - Structured logging with categories (Implement Now)
- [Memory Arenas](./memory-arenas.md) - Fast temporary allocations (Implement Soon)
- [Resource Handles](./resource-handles.md) - Safe resource references (Implement Soon)
- [Diagnostic Drawing](./diagnostic-drawing.md) - Immediate mode debug draw (Implement Later)

### World & Game
- [World Generation Architecture](./world-generation-architecture.md) - Pluggable world generators
- [Chunk Management System](./chunk-management-system.md) - Infinite 2D world from 3D sphere
- [3D to 2D Sampling](./3d-to-2d-sampling.md) - Converting spherical world to tile data

### Graphics & Rendering
- [Renderer Architecture](./renderer-architecture.md) - OpenGL abstraction, 2D/3D rendering
- [Resource Management](./resource-management.md) - Textures, shaders, fonts

#### Vector Graphics System
**Complete documentation**: [Vector Graphics Index](./vector-graphics/INDEX.md)

- [Vector Asset Pipeline](./vector-graphics/asset-pipeline.md) - High-level overview of SVG asset workflow
- [Architecture](./vector-graphics/architecture.md) - Four-tier rendering system, integration design
- [Tessellation Options](./vector-graphics/tessellation-options.md) - Comparative analysis: libtess2, Earcut, custom
- [SVG Parsing Options](./vector-graphics/svg-parsing-options.md) - Comparative analysis: NanoSVG, LunaSVG, PlutoVG, custom
- [Rendering Backend Options](./vector-graphics/rendering-backend-options.md) - Comparative analysis: NanoVG, Blend2D, custom batched, Vello
- [Batching Strategies](./vector-graphics/batching-strategies.md) - GPU batching, streaming VBOs, atlasing
- [Animation System](./vector-graphics/animation-system.md) - Spline deformation, wind, trampling
- [Collision Shapes](./vector-graphics/collision-shapes.md) - Dual representation design
- [LOD System](./vector-graphics/lod-system.md) - Level of detail strategies
- [Memory Management](./vector-graphics/memory-management.md) - Memory architecture, budgets, arenas
- [Performance Targets](./vector-graphics/performance-targets.md) - Performance budgets, profiling

#### UI Framework
**Complete documentation**: [UI Framework Index](./ui-framework/INDEX.md)

- [Library Options](./ui-framework/library-options.md) - Comparative analysis: Dear ImGui, Nuklear, RmlUI, NanoGUI, MyGUI, custom implementation
- [Architecture](./ui-framework/architecture.md) - Scene graph, event system, component lifecycle *(planned)*
- [Scrolling Containers](./ui-framework/scrolling-containers.md) - OpenGL clipping, culling, performance *(planned)*
- [Text Rendering](./ui-framework/text-rendering.md) - SDF fonts for crisp vector text *(planned)*
- [Renderer Integration](./ui-framework/renderer-integration.md) - Batching, render passes, clipping stack *(planned)*

### Engine & Application
- [Scene Management](./scene-management.md) - Scene lifecycle and transitions
- [Configuration System](./config-system.md) - JSON-based game configuration
- [Application Lifecycle](./application-lifecycle.md) - Fast splash screen, lazy loading

## System Architecture Diagram

### Applications & Services Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          WORLD-SIM PROJECT                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────────────────────┐              ┌──────────────────────────┐   │
│  │   Game Server Process    │              │  Game Client Process     │   │
│  │   (world-sim-server)     │              │  (world-sim)             │   │
│  │                          │              │                          │   │
│  │  - ECS (game state)      │   WebSocket  │  - Renderer              │   │
│  │  - World generation      │◄────────────►│  - UI layer              │   │
│  │  - Game logic (60 TPS)   │   Port 9000  │  - Input handling        │   │
│  │  - Chunk management      │   60 Hz      │  - Audio                 │   │
│  │                          │              │                          │   │
│  │  Developer Server:       │              │  Developer Server:       │   │
│  │  Port 8080 (HTTP/SSE)    │              │  Port 8082 (HTTP/SSE)    │   │
│  └──────────┬───────────────┘              └──────────┬───────────────┘   │
│             │                                         │                    │
│             │                                         │                    │
│  ┌──────────▼─────────────────────────────────────────▼──────────────┐    │
│  │              Developer Client (Browser)                           │    │
│  │              External TypeScript/Vite Web App                     │    │
│  │                                                                    │    │
│  │  Connects via HTTP/SSE to observe running applications:           │    │
│  │                                                                    │    │
│  │  - Metrics charts (FPS, memory, performance)                      │    │
│  │  - Log viewer (real-time log streaming)                           │    │
│  │  - Profiler (function timing, flame graphs)                       │    │
│  │  - UI inspector (hierarchy, hover data)                           │    │
│  │                                                                    │    │
│  │  http://localhost:8080 → Game Server                              │    │
│  │  http://localhost:8081 → UI-Sandbox                               │    │
│  │  http://localhost:8082 → Game Client                              │    │
│  └────────────────────────────┬───────────────────────────────────────┘   │
│                               │                                            │
│                               │ Also connects to:                          │
│                               │                                            │
│  ┌────────────────────────────▼──────────────────────────┐                │
│  │   UI-Sandbox Process                                  │                │
│  │   (ui-sandbox)                                        │                │
│  │                                                        │                │
│  │   Library development & testing sandbox               │                │
│  │                                                        │                │
│  │   - Develop atomic UI components (buttons, text)      │                │
│  │   - Test OpenGL drawing code in isolation             │                │
│  │   - Develop vector asset rendering (SVG)              │                │
│  │   - Test graphics primitives & base rendering         │                │
│  │   - Develop drawing libraries outside game context    │                │
│  │                                                        │                │
│  │   Developer Server: Port 8081 (HTTP/SSE)              │                │
│  └────────────────────────────────────────────────────────┘                │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘

KEY DISTINCTIONS:

Gameplay Network (Production):
  Game Server ←──WebSocket (port 9000, 60 Hz)──→ Game Client
  Purpose: Real-time game state synchronization
  Available in: All builds (Development + Release)

Observability Network (Development Only):
  Applications ──→ HTTP/SSE (ports 8080/8081/8082, 10-20 Hz) ──→ Developer Client
  Purpose: Monitoring, debugging, inspection
  Available in: Development builds only (compiled out in Release)

Applications:
  1. Game Server    - Backend game logic (headless, no rendering)
  2. Game Client    - Player-facing game (rendering, UI, audio)
  3. UI-Sandbox     - Library development sandbox (UI components, OpenGL, SVG)
  4. Developer Client - External browser-based monitoring dashboard
```

### Port Assignments

| Port | Service | Protocol | Purpose | Availability |
|------|---------|----------|---------|--------------|
| 9000 | Game Server ↔ Client | WebSocket | Gameplay sync (60 Hz) | All builds |
| 8080 | Game Server Developer Server | HTTP/SSE | Observability (10-20 Hz) | Development only |
| 8081 | UI-Sandbox Developer Server | HTTP/SSE | Observability (10-20 Hz) | Development only |
| 8082 | Game Client Developer Server | HTTP/SSE | Observability (10-20 Hz) | Development only |

### Process Communication

**Game Client ↔ Game Server (Gameplay):**
- Protocol: WebSocket (bidirectional)
- Rate: 60 Hz (real-time)
- Purpose: Game state synchronization
- Managed by: Game client spawns/monitors server process
- See: [Multiplayer Architecture](./multiplayer-architecture.md), [Process Management](./process-management.md)

**Applications → Developer Client (Observability):**
- Protocol: HTTP with Server-Sent Events (SSE) (server→client streaming)
- Rate: 10-20 Hz (throttled, non-critical)
- Purpose: Monitoring, debugging, inspection
- Data: Metrics, logs, profiler data, UI hierarchy
- See: [Observability Overview](./observability/INDEX.md)

### Build Configurations

**Development Build:**
- Game Server: WebSocket server (9000) + Developer Server (8080)
- Game Client: WebSocket client (9000) + Developer Server (8082)
- UI-Sandbox: Developer Server (8081) only
- Developer Client: Web app served from developer servers

**Release Build:**
- Game Server: WebSocket server only (9000)
- Game Client: WebSocket client only (9000)
- Developer servers: Compiled out completely
- Developer Client: Not bundled

## Related Documentation

- [Development Status](/docs/status.md) - Current work and decisions
- [Game Design Documents](/docs/design/INDEX.md) - Feature requirements and player experience
