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
5. **World-Unique Flora**: Each world has unique procedural variants based on its map seed

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

## Biome and Placement System

Assets declare which biomes they can appear in. **Biomes are engine-defined enums; assets reference them.**

### Design Rationale

```
┌─────────────────────────────────────────────────────────────────┐
│                    WHY ASSETS → BIOMES                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  BIOMES (Engine Code - C++ enum)          NOT MODDABLE         │
│  ┌─────────────────────────────┐                               │
│  │ enum class Biome {          │  - Hardcoded in world gen     │
│  │   Grassland,                │  - Tile type determines biome │
│  │   Forest,                   │  - Fixed, enumerated list     │
│  │   Desert,                   │                               │
│  │   Tundra,                   │                               │
│  │   Wetland,                  │                               │
│  │   Mountain,                 │                               │
│  │   Beach,                    │                               │
│  │   Ocean                     │                               │
│  │ };                          │                               │
│  └─────────────────────────────┘                               │
│              ▲                                                  │
│              │ references                                       │
│              │                                                  │
│  ASSETS (XML Definitions)                  MODDABLE            │
│  ┌─────────────────────────────┐                               │
│  │ <Asset name="Daisy">        │  - Modders add new flowers    │
│  │   <Biomes>                  │  - Each asset lists its valid │
│  │     <Biome>Grassland</Biome>│    biomes by name             │
│  │     <Biome>Forest</Biome>   │  - Unknown biome names =      │
│  │   </Biomes>                 │    warning, asset ignored     │
│  │ </Asset>                    │                               │
│  └─────────────────────────────┘                               │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

**Key principle**: Modders can add assets to existing biomes, but cannot create new biomes. This keeps world generation stable while allowing unlimited content expansion.

### Asset Definition Example

```xml
<AssetDefinition name="GrassBlade" type="simple">
  <Source>svg/flora/grass_blade.svg</Source>

  <!-- Placement rules -->
  <Placement>
    <Biomes>
      <Biome>Grassland</Biome>
      <Biome>Forest</Biome>
      <Biome>Wetland</Biome>
    </Biomes>

    <!-- Spawn probability: 0.0-1.0, checked per potential spawn point -->
    <SpawnChance>0.3</SpawnChance>

    <!-- Distribution pattern -->
    <Distribution>clumped</Distribution>  <!-- uniform, clumped, spaced -->

    <!-- Clump settings (only used when Distribution=clumped) -->
    <Clumping>
      <ClumpSize min="3" max="12"/>       <!-- instances per clump -->
      <ClumpRadius min="0.5" max="2.0"/>  <!-- tiles -->
      <ClumpSpacing min="3" max="8"/>     <!-- tiles between clump centers -->
    </Clumping>

    <!-- Spacing settings (only used when Distribution=spaced) -->
    <Spacing>
      <MinDistance>2.0</MinDistance>      <!-- minimum tiles between instances -->
    </Spacing>
  </Placement>

  <!-- Variation ranges -->
  <Variation>
    <Scale min="0.8" max="1.5"/>
    <Rotation min="-15" max="15"/>   <!-- degrees from vertical -->
    <ColorShift hue="0.05" saturation="0.1" lightness="0.1"/>
  </Variation>
</AssetDefinition>
```

### Placement Parameters

#### SpawnChance (0.0 - 1.0)
Probability that an asset spawns at each potential spawn point. Lower values = rarer assets.

| Value | Description              | Example Use               |
|-------|--------------------------|---------------------------|
| 0.01  | Very rare                | Special flowers, boulders |
| 0.1   | Uncommon                 | Mushrooms, ferns          |
| 0.3   | Common                   | Grass, small rocks        |
| 0.7   | Abundant                 | Ground cover              |
| 1.0   | Always (density limited) | Background fill           |

#### Distribution Patterns

```
UNIFORM                    CLUMPED                    SPACED
(random placement)         (patches/groups)           (regular spacing)

  •    •   •               •••    ••                     •       •
    •       •              ••••  •••                  •       •
  •   •  •    •              ••     •••                  •       •
     •    •                     ••••                  •       •
  •      •   •             •••   ••                     •       •

Good for:                  Good for:                  Good for:
- Grass                    - Flower patches           - Trees
- Small debris             - Mushroom rings           - Large bushes
- Sand particles           - Berry bushes             - Cacti
```

| Pattern  | Description                                        |
|----------|----------------------------------------------------|
| uniform  | Random placement within tile, no clustering        |
| clumped  | Groups together in patches (configurable size)     |
| spaced   | Maintains minimum distance between instances       |

#### Clumping Parameters

When `Distribution=clumped`:

| Parameter    | Description                                    |
|--------------|------------------------------------------------|
| ClumpSize    | Number of instances in each clump (min-max)    |
| ClumpRadius  | How spread out instances are within clump      |
| ClumpSpacing | Distance between clump centers (tiles)         |

#### Spacing Parameters

When `Distribution=spaced`:

| Parameter   | Description                                     |
|-------------|-------------------------------------------------|
| MinDistance | Minimum tiles between any two instances         |

This uses Poisson disk sampling - each new instance is placed at least MinDistance from all existing instances.

### Examples by Asset Type

```xml
<!-- Grass: abundant, clumped in patches -->
<Placement>
  <SpawnChance>0.5</SpawnChance>
  <Distribution>clumped</Distribution>
  <Clumping>
    <ClumpSize min="5" max="15"/>
    <ClumpRadius min="0.3" max="1.0"/>
    <ClumpSpacing min="1" max="3"/>
  </Clumping>
</Placement>

<!-- Wildflowers: patches of color -->
<Placement>
  <SpawnChance>0.15</SpawnChance>
  <Distribution>clumped</Distribution>
  <Clumping>
    <ClumpSize min="3" max="8"/>
    <ClumpRadius min="1.0" max="2.5"/>
    <ClumpSpacing min="5" max="12"/>
  </Clumping>
</Placement>

<!-- Trees: evenly spaced, not too close -->
<Placement>
  <SpawnChance>0.4</SpawnChance>
  <Distribution>spaced</Distribution>
  <Spacing>
    <MinDistance>3.0</MinDistance>
  </Spacing>
</Placement>

<!-- Small rocks: scattered randomly -->
<Placement>
  <SpawnChance>0.08</SpawnChance>
  <Distribution>uniform</Distribution>
</Placement>

<!-- Rare boulder: very uncommon, isolated -->
<Placement>
  <SpawnChance>0.005</SpawnChance>
  <Distribution>spaced</Distribution>
  <Spacing>
    <MinDistance>10.0</MinDistance>
  </Spacing>
</Placement>
```

### Query API

```cpp
// Engine queries assets by biome at runtime
std::vector<AssetDef*> assets = registry.GetAssetsForBiome(Biome::Grassland);

// Filter by type
auto flora = registry.GetAssetsForBiome(Biome::Grassland, AssetType::Flora);
auto trees = registry.GetAssetsForBiome(Biome::Forest, AssetType::Tree);
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
       │    - Used by Lua scripts via `math.randomseed()`
       │    - Generates N variants per asset type
       │
       └──► Instance Placement RNG
            - Seeded per-chunk: hash(base_seed, chunk_x, chunk_y)
            - Deterministic variant selection by position
            - Same position → same variant every time
```

### Implementation Approach

**Option A: Session-Seeded RNG (Recommended)**
- When a world loads, seed the Lua RNG with `hash(world_seed, "flora")`
- All procedural generation during that session uses this RNG
- Variant cache is keyed by world seed, regenerated if seed changes
- Pro: Simple, all Lua `math.random()` calls "just work"
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
3. Script hash (detects Lua code changes)

```
cache/flora/tree_oak_variants_a1b2c3d4.bin
                              ^^^^^^^^
                              combined hash
```

If any input changes, cache is regenerated during the loading screen.

## Related Documents

- [Asset Definition Schema](./asset-definitions.md) - XML/JSON schema for asset definitions
- [Lua Scripting API](./lua-scripting-api.md) - API available to generator scripts
- [Variant Cache Format](./variant-cache.md) - Binary format for pre-generated variants
- [Integration with Tiered Renderer](../vector-graphics/animation-performance.md) - Rendering path selection
