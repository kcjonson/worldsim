# Asset System Architecture

**Status**: Design Phase
**Created**: 2025-11-30
**Last Updated**: 2025-12-03

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
5. **World-Unique Flora**: Each world has unique procedural variants based on its map seed
6. **Self-Contained**: Each asset is a folder containing all its resources

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Asset System Architecture                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        Asset Folder Layer                            │   │
│  │  Each asset is a self-contained folder with XML definition + files   │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                   │   │
│  │  │ GrassBlade/ │  │ MapleTree/  │  │ Boulder/    │                   │   │
│  │  │  ├─ .xml    │  │  ├─ .xml    │  │  ├─ .xml    │                   │   │
│  │  │  └─ .svg    │  │  ├─ .lua    │  │  └─ .svg    │                   │   │
│  │  │             │  │  └─ .svg    │  │             │                   │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘                   │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                    │                                         │
│                                    ▼                                         │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                        Asset Registry (C++)                           │   │
│  │  Scans asset folders, manages catalog, handles hot-reload             │   │
│  │  ┌─────────────────────────────────────────────────────────────────┐ │   │
│  │  │ AssetRegistry::LoadAssets("assets/")                            │ │   │
│  │  │ AssetRegistry::GetAsset("MapleTree") → AssetDefinition          │ │   │
│  │  │ AssetRegistry::GetVariant("MapleTree", seed) → CachedMesh       │ │   │
│  │  └─────────────────────────────────────────────────────────────────┘ │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                    │                                         │
│              ┌─────────────────────┼─────────────────────┐                  │
│              ▼                     ▼                     ▼                  │
│  ┌─────────────────────┐ ┌─────────────────────┐ ┌─────────────────────┐   │
│  │   Simple Loader     │ │ Procedural Generator │ │   Variant Cache     │   │
│  │   (SVG → Mesh)      │ │   (Script→VectorAsset│ │   (Pre-generated)   │   │
│  │                     │ │                      │ │                     │   │
│  │ Load SVG file       │ │ Execute script       │ │ Store N variants    │   │
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

## Placement System

Assets define where they can spawn in the world. The placement system supports:

- **Biome rules**: Which biomes an asset can appear in
- **Tile proximity**: Distance constraints from tile types (e.g., near water)
- **Distribution patterns**: uniform, clumped, or spaced placement
- **Entity relationships**: Spawn near/avoid other entity types
- **Self-declared groups**: Assets declare group membership (e.g., "flower", "tree")

**Key principle**: Modders can add assets to existing biomes, but cannot create new biomes. This keeps world generation stable while allowing unlimited content expansion.

### Quick Example

```xml
<placement>
  <biome name="Forest">
    <spawnChance>0.1</spawnChance>
    <distribution>clumped</distribution>
  </biome>
  <biome name="Wetland" near="Water" distance="2">
    <spawnChance>0.2</spawnChance>
  </biome>
  <groups>
    <group>mushroom</group>
    <group>fungus</group>
  </groups>
  <relationships>
    <requires group="trees" distance="3.0"/>
    <avoids type="same" distance="2.0" penalty="0.3"/>
  </relationships>
</placement>
```

**For complete placement documentation**, see:
- [Entity Placement System](../entity-placement-system.md) - Full specification including groups, relationships, distribution patterns, and C++ data structures

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

Script-generated assets. These require algorithmic generation (branching patterns, leaf placement, growth rules). Pre-generated at load time with N variants cached.

**Examples**: Trees (deciduous, conifer, palm), bushes, complex rock formations

**Characteristics**:
- Script generates VectorAsset programmatically
- Multiple variants pre-generated and cached (e.g., 200 unique oaks)
- Selection by position seed ensures determinism
- Can still use GPU instancing per-variant

## File Structure

Assets use a **3-level hierarchy**: `category/subcategory/Asset/`

```
assets/
├── shared/                          # Shared resources
│   ├── scripts/                     # Shared script modules (registered by name)
│   │   ├── branch_utils.lua        # → require("branch_utils")
│   │   ├── color.lua               # → require("color")
│   │   └── noise.lua               # → require("noise")
│   │
│   └── components/                  # Shared SVG components
│       └── leaf_shapes.svg         # → loadComponent("leaf_shapes")
│
├── world/                           # World generation assets
│   ├── flora/                       # Plants
│   │   ├── GrassBlade/
│   │   │   ├── GrassBlade.xml      # Primary definition
│   │   │   └── blade.svg           # SVG source
│   │   │
│   │   └── MapleTree/
│   │       ├── MapleTree.xml       # Definition with generator config
│   │       ├── generate.lua        # Primary generator script
│   │       └── maple_leaf.svg      # Component SVG
│   │
│   └── terrain/
│       └── Boulder/
│           ├── Boulder.xml
│           └── boulder.svg
│
├── creatures/                       # Living things
│   └── Deer/
│       ├── Deer.xml
│       └── body.svg
│
├── objects/                         # World-placed objects
│   └── crafting/
│       └── Workbench/
│           ├── Workbench.xml
│           └── workbench.svg
│
├── items/                           # Inventory items
│   └── tools/
│       └── Axe/
│           ├── Axe.xml
│           └── axe.svg
│
└── ui/                              # UI assets
    └── icons/
        └── HealthIcon/
            └── HealthIcon.xml
```

**For complete file structure specification**, see:
- [Folder-Based Assets](./folder-based-assets.md) - Self-contained asset folders, path resolution, naming

## World Seed and Procedural Uniqueness

Each world in the game has a unique **map seed** that determines all procedural generation. This seed is used to create world-unique flora variants, ensuring every world is visually distinct.

### Seed Propagation

```
World Seed (e.g., "my-world-123")
       │
       ▼
   hash(seed) → uint64_t base_seed
       │
       ├──► Asset Generation RNG
       │    - Seeded at world load time
       │    - Used by scripts via `math.randomseed()`
       │    - Generates N variants per asset type
       │
       └──► Instance Placement RNG
            - Seeded per-chunk: hash(base_seed, chunk_x, chunk_y)
            - Deterministic variant selection by position
            - Same position → same variant every time
```

### Implementation Approach

**Option A: Session-Seeded RNG (Recommended)**
- When a world loads, seed the script RNG with `hash(world_seed, "flora")`
- All procedural generation during that session uses this RNG
- Variant cache is keyed by world seed, regenerated if seed changes
- Pro: Simple, all `math.random()` calls "just work"
- Con: Variant cache is per-world (more disk space if many worlds)

**Option B: Per-Script Seed Parameter**
- Pass seed explicitly to each generator: `generate(asset_def, seed)`
- Script must call `math.randomseed(seed)` at start
- Pro: More explicit control, cache can be shared across worlds with same parameters
- Con: More boilerplate in every script

### Cache Invalidation

The variant cache filename includes a hash of:
1. World seed
2. Asset definition hash (detects XML changes)
3. Script hash (detects script code changes)

```
.cache/MapleTree/variants_a1b2c3d4.bin
                          ^^^^^^^^
                          combined hash
```

If any input changes, cache is regenerated during the loading screen.

## Related Documents

- [Folder-Based Assets](./folder-based-assets.md) - Self-contained folder structure, path resolution
- [Asset Definition Schema](./asset-definitions.md) - XML schema for asset definitions
- [Scripting API](./lua-scripting-api.md) - API available to generator scripts
- [Variant Cache Format](./variant-cache.md) - Binary format for pre-generated variants
- [Integration with Tiered Renderer](../vector-graphics/animation-performance.md) - Rendering path selection
