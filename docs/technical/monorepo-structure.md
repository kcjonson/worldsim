# Monorepo Structure

Created: 2025-10-12
Last Updated: 2025-10-12
Status: Active

## Context

World-sim is built as a monorepo containing multiple libraries and applications. Each library is designed to be self-contained and potentially extractable as a standalone open-source project in the future. However, we're avoiding premature optimization - libraries start simple and grow as needed.

## Library Organization

The monorepo is organized by dependency layers, from foundation to high-level:

```
libs/
├── foundation/          # Base layer - math, platform, utilities
├── renderer/            # Graphics layer - OpenGL, 2D/3D rendering
├── ui/                  # UI framework - components, layout, inspector
├── world/               # World generation and 3D-to-2D sampling
├── game-systems/        # Game-specific: chunks, tiles, camera
└── engine/              # Top layer - scenes, config, app lifecycle
```

### Dependency Rules

Each layer can only depend on layers below it:

```
engine
  ↓
game-systems, ui, world
  ↓
renderer
  ↓
foundation
```

**No circular dependencies allowed.** For example:
- ✅ `ui` can depend on `renderer` (layer below)
- ✅ `engine` can depend on `ui` (layer below)
- ❌ `renderer` cannot depend on `ui` (layer above)
- ❌ `world` cannot depend on `game-systems` (same layer)

### Library Descriptions

**foundation/**
- Math utilities (vectors, matrices, noise functions like Perlin)
- Platform abstraction (window creation, OS interfaces)
- Common utilities (logging, string helpers, etc.)
- No external dependencies except standard library

**renderer/**
- OpenGL wrapper/abstraction
- 3D rendering utilities
- 2D rendering (sprites, shapes, text)
- Resource management (textures, shaders, fonts)
- Depends on: `foundation`

**ui/**
- Core UI framework (scene graph, base classes)
- UI components (Button, TextInput, Label, etc.)
- Layout system (Container, ScrollView, flex-like layouts)
- UI inspector for testability (JSON export, debug tools)
- Depends on: `renderer`, `foundation`

**world/**
- World generation interface and implementations
- 3D-to-2D sampling (converting spherical world to flat tiles)
- World data structures
- Depends on: `renderer`, `foundation`

**game-systems/**
- Chunk management (loading/unloading from world data)
- Tile rendering system
- 2D camera with pan/zoom
- Game-specific logic
- Depends on: `world`, `renderer`, `foundation`

**engine/**
- Application lifecycle (init, main loop, shutdown)
- Scene management (loading, switching, transitions)
- Configuration system (JSON loading)
- Input management layer
- Depends on: All layers below

## Applications

```
apps/
├── world-sim/           # Main game application
└── ui-sandbox/          # UI testing and demo application
```

Both applications depend on the libraries they need. The main game uses most/all libraries, while ui-sandbox primarily uses `foundation`, `renderer`, and `ui`.

## CMake Structure

Each library has its own `CMakeLists.txt` and can be built independently. The root `CMakeLists.txt` adds subdirectories in dependency order.

Example library structure:
```
libs/renderer/
├── CMakeLists.txt
├── include/renderer/
│   ├── renderer2d.h
│   └── renderer3d.h
├── src/
│   ├── renderer2d.cpp
│   └── renderer3d.cpp
└── tests/
    └── renderer_tests.cpp
```

## Trade-offs

**Pros:**
- Clear dependency boundaries prevent spaghetti code
- Libraries can be tested independently
- Future extraction to separate repos is straightforward
- AI agents can understand the architecture quickly

**Cons:**
- More boilerplate (CMakeLists.txt per library)
- May feel over-engineered early on
- Need discipline to avoid breaking dependency rules

**Decision:** Accept the upfront structure cost for long-term maintainability.

## Implementation Status

- [x] Structure defined
- [ ] CMake configuration
- [ ] Library skeletons created
- [ ] Build system working

## Related Documentation

- Design Doc: [Project Requirements](/docs/design/requirements/functional.md)
- Tech: [Build System](./build-system.md)
- Code: *To be added once implemented*
