# Product Specifications Index

## Overview

This directory contains product specifications, requirements, and planning documents for the world-sim project. These specs describe *what* needs to be built, while technical design docs describe *how* it's implemented.

**Important**: Specs may differ from actual implementation. When implementation diverges from specs, see the corresponding Technical Design Document for what was actually built and why.

## Feature Specifications

### Core Application
- [Application Flow](./features/application-flow/README.md) - Splash screen, main menu, scene transitions
- [Main Menu](./features/main-menu/README.md) - Menu options and navigation

### World Creation
- [World Creation Flow](./features/world-creation/README.md) - Create world scene overview
- [3D Planet Rendering](./features/world-creation/planet-rendering.md) - Interactive 3D planet preview
- [World Parameters](./features/world-creation/parameters.md) - Generation controls and UI
- [World Generation](./features/world-creation/generation.md) - Generation process and progress display

### 2D Game
- [Game Scene](./features/game/README.md) - Top-down 2D tile-based gameplay
- [Camera Controls](./features/game/camera-controls.md) - Pan, zoom, edge scrolling
- [Infinite World](./features/game/infinite-world.md) - Chunk loading from spherical world

### UI Framework
- [UI Component Library](./features/ui-framework/README.md) - Overview of UI system
- [UI Sandbox Application](./features/ui-framework/ui-sandbox.md) - Testing and demo app

### Architecture
- [Multiplayer Architecture](./features/multiplayer/README.md) - Client/server architecture for single and multiplayer
- [HTTP Debug Server](./features/debug-server/README.md) - Real-time debugging and metrics via web interface

### Assets
- [Vector Assets](./features/vector-assets/README.md) - SVG-based asset system with procedural variation

## Requirements

- [Functional Requirements](./requirements/functional.md) - Core functionality
- [Non-Functional Requirements](./requirements/non-functional.md) - Performance, testability, maintainability
- [Performance Requirements](./requirements/performance.md) - Frame rate, load times, memory

## Technical Considerations

For implementation details, see [Technical Design Documents](/docs/technical/INDEX.md)
