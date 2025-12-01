# Asset System Architecture

**Status**: Design Phase
**Created**: 2025-11-30
**Last Updated**: 2025-11-30

## Overview

The asset system manages loading, generation, and caching of game assets with support for:
- **Simple assets**: Designer-authored SVG files (flowers, mushrooms, rocks)
- **Procedural assets**: Script-generated assets (trees, bushes, complex flora)
- **Moddability**: All asset definitions exposed on disk for modder access
- **Pre-generation**: Variant caching for procedural assets at load time

## Design Goals

1. **Data-Driven**: Separate asset definitions (data) from generation logic (scripts) and engine code (C++)
2. **Moddable**: Modders can add/replace assets without touching C++ code
3. **Performant**: Pre-generate expensive assets at load time, render with instancing
4. **Flexible**: Support both hand-crafted and procedurally generated assets

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Asset System Architecture                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        Asset Definition Layer                         │   │
│  │  XML/JSON files on disk defining what assets exist and their props    │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                   │   │
│  │  │ flora.xml   │  │ terrain.xml │  │ entities.xml│                   │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                   │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                    │                                         │
│                                    ▼                                         │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        Asset Registry (C++)                           │   │
│  │  Parses definitions, manages asset catalog, handles hot-reload        │   │
│  │  ┌─────────────────────────────────────────────────────────────────┐ │   │
│  │  │ AssetRegistry::LoadDefinitions("assets/definitions/")          │ │   │
│  │  │ AssetRegistry::GetAsset("Tree_Oak") → AssetDefinition          │ │   │
│  │  │ AssetRegistry::GetVariant("Tree_Oak", seed) → CachedMesh       │ │   │
│  │  └─────────────────────────────────────────────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                    │                                         │
│              ┌─────────────────────┼─────────────────────┐                  │
│              ▼                     ▼                     ▼                  │
│  ┌─────────────────────┐ ┌─────────────────────┐ ┌─────────────────────┐   │
│  │   Simple Loader     │ │ Procedural Generator │ │   Variant Cache     │   │
│  │   (SVG → Mesh)      │ │   (Lua → VectorAsset)│ │   (Pre-generated)   │   │
│  │                     │ │                      │ │                     │   │
│  │ Load SVG file       │ │ Execute Lua script   │ │ Store N variants    │   │
│  │ Parse paths         │ │ Generate curves      │ │ Select by seed      │   │
│  │ Tessellate once     │ │ Tessellate result    │ │ Ready for instancing│   │
│  └─────────────────────┘ └─────────────────────┘ └─────────────────────┘   │
│              │                     │                     │                  │
│              └─────────────────────┼─────────────────────┘                  │
│                                    ▼                                         │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        Tiered Renderer                                │   │
│  │  Selects rendering path based on asset complexity and distance        │   │
│  │  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐    │   │
│  │  │ Tier 1: GPU      │  │ Tier 2: CPU      │  │ Tier 3: Hybrid   │    │   │
│  │  │ Instanced        │  │ Tessellated      │  │ (distance-based) │    │   │
│  │  └──────────────────┘  └──────────────────┘  └──────────────────┘    │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Asset Types

### Simple Assets

Hand-crafted SVG files created by designers. These are loaded directly, tessellated once at load time, and rendered via GPU instancing with per-instance variation (color, scale, rotation).

**Examples**: Flowers, mushrooms, small rocks, ferns, grass tufts

**Characteristics**:
- Fixed geometry (no runtime generation)
- Color/scale/rotation variation applied at render time
- GPU instanced rendering (100,000+ instances possible)
- Low CPU overhead at runtime

### Procedural Assets

Script-generated assets using Lua. These require algorithmic generation (branching patterns, leaf placement, growth rules). Pre-generated at load time with N variants cached.

**Examples**: Trees (deciduous, conifer, palm), bushes, complex rock formations

**Characteristics**:
- Lua script generates VectorAsset programmatically
- Multiple variants pre-generated and cached (e.g., 200 unique oaks)
- Selection by position seed ensures determinism
- Can still use GPU instancing per-variant

## File Structure

```
assets/
├── definitions/                    # Asset definition files (modder-editable)
│   ├── flora/
│   │   ├── flowers.xml
│   │   ├── trees.xml
│   │   ├── bushes.xml
│   │   └── grass.xml
│   ├── terrain/
│   │   ├── rocks.xml
│   │   └── water-features.xml
│   └── entities/
│       └── creatures.xml
│
├── svg/                            # Simple asset source files
│   ├── flora/
│   │   ├── flowers/
│   │   │   ├── daisy.svg
│   │   │   ├── poppy.svg
│   │   │   └── tulip.svg
│   │   └── mushrooms/
│   │       ├── red_cap.svg
│   │       └── puffball.svg
│   └── terrain/
│       └── rocks/
│           ├── small_rock.svg
│           └── boulder.svg
│
├── generators/                     # Procedural generation scripts (Lua)
│   ├── trees/
│   │   ├── deciduous.lua          # Oak, maple, birch
│   │   ├── conifer.lua            # Pine, spruce, fir
│   │   └── palm.lua               # Tropical palms
│   ├── bushes/
│   │   └── berry_bush.lua
│   └── shared/
│       ├── branch_utils.lua       # Common branching algorithms
│       └── leaf_placement.lua     # Leaf distribution utilities
│
└── cache/                          # Generated variant cache (can be deleted)
    └── flora/
        ├── tree_oak_variants.bin
        └── tree_pine_variants.bin
```

## Related Documents

- [Asset Definition Schema](./asset-definitions.md) - XML/JSON schema for asset definitions
- [Lua Scripting API](./lua-scripting-api.md) - API available to generator scripts
- [Variant Cache Format](./variant-cache.md) - Binary format for pre-generated variants
- [Integration with Tiered Renderer](../vector-graphics/animation-performance.md) - Rendering path selection
