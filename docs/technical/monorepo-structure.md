# Monorepo Structure

Created: 2025-10-12
Last Updated: 2026-10-31
Status: Active

## Context

World-sim is built as a monorepo containing multiple libraries and applications. Each library is designed to be self-contained and potentially extractable as a standalone open-source project in the future. However, we're avoiding premature optimization - libraries start simple and grow as needed.

## Library Organization

The monorepo is organized by dependency layers, from foundation to high-level:

```
libs/
├── geometry/            # Exact integer-mm 2D geometry (predicates, arrangement, booleans, offsetting)
├── theme/               # Worldsim's design system on salal's ui: the theme's tokens, the icon set
├── worldgen/            # Planet generation and 3D-to-2D sampling
├── planet-view/         # The planet globe's rendering
├── world/               # Chunks, tiles, terrain, the world camera
├── assets/              # Asset definitions, registries, Lua, placement
├── sim/                 # The ECS and its systems, navigation, vision, construction
└── world-view/          # Top layer - the world's rendering

salal (external, build/_deps/salal-src/ after a configure)
└── libs/
    ├── foundation/      # Base layer - math, platform, utilities
    ├── renderer/        # Graphics layer - OpenGL, 2D/3D rendering
    ├── ui/              # salal's ui: components, layout, theme, icons, which app hosts
    └── app/             # The window, the frame, input, the scene stack, the dev server
```

### Engine libraries from salal

`foundation` and `renderer` were extracted to [salal](https://github.com/kcjonson/salal) and come back in through CMake FetchContent, pinned to a tag by `GIT_TAG` in the root `CMakeLists.txt`; targets link them as `salal::foundation` and `salal::renderer`, and the include spellings (`<utils/Log.h>`, `<primitives/Primitives.h>`) are unchanged. salal's `app` (`salal::app`, `<application/App.h>`, `<scene/Scene.h>`) is what every GUI app runs on: the window, the frame, input, the scene stack and the dev server, which each GUI app links; every GUI app's scenes are on salal's ui. The layering below still holds, three of its layers just live in another repo. Building against a local salal checkout instead of the tag is covered in `CLAUDE.md`.

### Dependency Rules

Each layer can only depend on layers below it:

```
world-view
  ↓
sim
  ↓
assets
  ↓
world, planet-view, theme, app (salal)
  ↓
worldgen, ui (salal)
  ↓
renderer (salal), geometry
  ↓
foundation (salal)
```

The game's own libraries keep the simulation away from the screen: `world` and `sim` link neither `renderer` nor `ui`, and `world-view` is the side that draws. `assets` links `renderer` for SVG tessellation and its thumbnails, so `sim` still reaches `renderer` through it; that edge is the one left to cut.

**No circular dependencies allowed.** For example:
- ✅ `theme` can depend on `ui` (layer below)
- ✅ `sim` can depend on `world` (layer below)
- ❌ `renderer` cannot depend on `ui` (layer above)
- ❌ `sim` cannot depend on `world-view` (layer above)

### Library Descriptions

**foundation/** (salal)
- Math utilities (vectors, matrices, noise functions like Perlin)
- Platform abstraction (window creation, OS interfaces)
- Common utilities (logging, string helpers, etc.)
- No external dependencies except standard library

**geometry/**
- Exact 2D geometry on integer-millimeter coordinates (construction, navigation, vision substrate)
- Exact predicates, polygon constraint primitives, planar arrangement + face extraction
- Ring booleans, wall band offsetting with junction trimming
- Depends on: `foundation`

**renderer/** (salal)
- OpenGL wrapper/abstraction
- 3D rendering utilities
- 2D rendering (sprites, shapes, text)
- Resource management (textures, shaders, fonts)
- Depends on: `foundation`


**theme/**
- Worldsim's design system on salal's ui: its theme as a Design Tokens file and the icon set as filled SVGs, both compiled in by salal's `salal_tokens()` and `salal_icons()` (`<theme/Tokens.h>`, `<theme/Icons.h>`), and its own components on salal's, Panel with its kicker and corner ticks and Avatar (`<theme/Panel.h>`, `<theme/Avatar.h>`)
- Both are written by the scripts in `docs/ui-prototype/scripts/`, from the prototype's tokens and icons
- Depends on: `ui` (salal)

**worldgen/**
- The planet pipeline: plates, crust, terrain, erosion, climate, biomes (namespace `worldgen`)
- 3D-to-2D sampling (converting the spherical world to flat tiles)
- Depends on: `foundation`

**planet-view/**
- The planet globe: its mesh, colors, orbit camera and renderer (namespace `planetview`)
- Depends on: `worldgen`, `renderer`, `foundation`

**ui/** (salal)
- Components, layout, the theme loaded from Design Tokens, icons from an MSDF atlas, each scene's ui context
- Depends on: `renderer`, `foundation`

**app/** (salal)
- The window, the frame and its pacing, device input, the scene stack and overlays, the clipboard, settings and saves, the dev server
- Depends on: `ui` (salal), `renderer`, `foundation`

The game's libraries each keep their include root at the library folder with one folder inside it, so `libs/sim/ecs/World.h` is `<ecs/World.h>` and `libs/assets/assets/AssetRegistry.h` is `<assets/AssetRegistry.h>`.

**world/**
- Chunks and their streaming, tiles, terrain polygons and distance fields, biome generation, the world camera (namespace `world`)
- Depends on: `worldgen`, `geometry`, `foundation`

**assets/**
- Asset definitions and registries, the Lua generators, config (work, recipes, task chains), placement (namespace `assets`)
- `assets/placement/` also holds the CPU halves of the world view that placement needs at placement time: `BakedEntityMesh`, `WorldDepthSort` and `RenderContext`
- Depends on: `world`, `geometry`, `renderer`, `foundation`

**sim/**
- The ECS and its systems (`ecs`), navigation (`nav`), vision, construction (`construction`)
- Depends on: `assets`, `world`, `geometry`, `foundation`

**world-view/**
- The world's rendering: the ground pass and its shaders, the entity renderers, the ECS system that draws dynamic entities, and salal's base shader with the game's hooks (namespace `worldview`)
- Depends on: `sim`, `assets`, `world`, `renderer`, `foundation`

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

## Related Documentation

- Design Doc: [Project Requirements](/docs/design/requirements/functional.md)
- Tech: [Build System](./build-system.md)
- Code: *To be added once implemented*
