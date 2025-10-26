# 2D Game View & Tile Rendering

Created: 2025-10-26
Status: Design

## Overview

This folder contains game design documentation for the **2D game view** - how the world appears during gameplay. Players interact with a top-down 2D tile-based interface that samples data from the 3D spherical world model to create infinite, seamless terrain.

**Relationship to 3D World Generation**:
- The [World Generation System](/docs/design/features/world-generation/README.md) creates a complete 3D spherical planet
- The 2D game view samples this 3D data to render tiles in the play area
- This folder documents how that sampled data is **rendered visually** for the player

## Architecture: 3D Source → 2D Rendering

### Data Flow

```
3D World Generation
  ↓ (spherical tiles with definitive biomes)
Sampling Layer
  ↓ (converts 3D coordinates to 2D position data)
Chunk Loading
  ↓ (loads chunks containing 512×512 tiles each)
2D Game View (this folder)
  ↓ (renders ground covers, entities, decorations)
Visual Display
```

### Chunk-Based Streaming

The 2D world is divided into **chunks** - fixed-size regions that enable infinite world exploration:

**What is a Chunk?**
- Square region of **512 × 512 tiles** (512m × 512m)
- Basic unit for loading, rendering, and simulation
- Each tile is 1 meter across
- Covers 2-4 screens at high resolution (comfortable for players)
- Contains 262,144 individual tiles

**Why Chunks?**
- **Infinite world**: Only load nearby chunks, unload distant ones
- **Seamless exploration**: New chunks stream in as player moves
- **Memory efficiency**: Typical 5×5 loaded area uses only 5-100 KB
- **Terraforming support**: Save only player modifications, regenerate pristine terrain from seed

**Pure vs Boundary Chunks**:

Most chunks (~64%) are **pure chunks**:
- Entire chunk has single biome (all 262k tiles identical)
- Minimal memory (just chunk metadata)
- Very fast to generate and render

Some chunks (~36%) are **boundary chunks**:
- Cross spherical tile boundaries
- Tiles have blended biome percentages
- Slightly more memory for interpolation data
- Generate smooth transitions between biomes

**Player Experience**:
- World feels infinite (chunks load on-demand from planet data)
- Smooth exploration (no loading screens or visible boundaries)
- Consistent world (revisiting locations shows identical terrain)
- Small save files (only player modifications saved, pristine chunks regenerate)

See [Data Model - Chunk Loading](/docs/design/features/world-generation/data-model.md#chunk-loading-and-optimization) for complete details.

### Hybrid Approach to Biome Transitions

The game uses a **two-scale system** for natural-looking biome transitions:

**Major Zones (from 3D World)**:
- Spherical tiles (~5km across) each have a single definitive biome
- At spherical tile boundaries, the 2D game blends neighboring biomes
- Primary transition width: ~500m at boundaries
- Creates coherent large-scale biome regions

**Micro-Variation (2D Rendering)**:
- Within pure biome regions, add procedural variation for natural feel
- Extends visual transitions beyond 3D boundaries
- Total effective transition depth: 2-10km
- Creates organic ecotones without overwhelming the 3D structure

**Result**: Major biome zones from realistic 3D world generation, enhanced with 2D procedural detail for hand-crafted feel.

## Documents in This Folder

### Core Rendering Systems

**[biome-influence-system.md](./biome-influence-system.md)** - How tiles blend multiple biomes
- Percentage-based influence from nearby biomes
- Natural ecotones (transition zones) hundreds of tiles deep
- How biome percentages determine tile properties
- Relationship to 3D world: Major influences from spherical boundaries, minor from 2D variation

**[biome-ground-covers.md](./biome-ground-covers.md)** - Physical ground surface types
- Grass, sand, rock, water, forest floor, etc.
- How biomes map to ground cover types
- Visual appearance and gameplay properties
- How ground covers blend in transition zones

### Visual Design

**[tile-transitions.md](./tile-transitions.md)** - Visual appearance of biome blending
- How percentage-based biomes create gradual transitions
- Two-scale transitions: Major (3D boundaries) + Minor (2D variation)
- Examples: meadow→forest, desert→savanna, ocean→beach
- Visual goals: organic, irregular, beautiful, memorable

**[procedural-variation.md](./procedural-variation.md)** - Making every tile unique
- Why variation matters (hand-crafted feel at scale)
- What varies: colors, textures, entity placement, scales
- What stays consistent: biome identity, gameplay properties
- Per-tile, per-biome, and per-decoration variation

## Key Concepts

### Biome Influence System

Instead of discrete tile types ("this is forest, that is grass"), tiles have **percentage influences** from multiple biomes:

```
Example tile at forest-meadow edge:
{ meadow: 70%, forest: 30% }

Visual result:
- Ground: 70% grass, 30% forest floor
- Entities: 70% chance wildflowers, 30% chance pine trees
- Color: Green with brownish tint
```

This creates **ecotones** - natural transition zones where ecosystems blend gradually over hundreds of tiles.

### Two-Scale Transitions

**Scale 1: 3D World Boundaries**
- Where spherical tiles meet (~every 5km)
- Sharp boundary in 3D data
- Smooth ~500m blend in 2D rendering
- Creates major biome zones

**Scale 2: 2D Micro-Variation**
- Within and around 3D boundaries
- Procedural noise adds natural irregularity
- Extends transitions to 2-10km total width
- Creates organic, hand-crafted appearance

**Combined Effect**: Realistic large-scale geography with beautiful local detail.

### Ground Covers vs Biomes

**Ground Covers** (physical surface):
- Permanent material types: grass, sand, rock, forest floor
- Visual base layer
- Determined by biome influences

**Biomes** (ecological zones):
- Use specific ground covers
- Meadow → grass cover
- Desert → sand cover
- Forest → forest floor cover

**Tiles** (what players see):
- Blend multiple ground covers based on biome percentages
- Example: 70% meadow tile shows 70% grass, 30% forest floor

## Data Sources

### From 3D World Generation

The 2D game receives this data by sampling the spherical world:

**Biome Information**:
- Primary biome at sampled location
- Neighboring biomes at spherical boundaries
- Distance to boundaries (determines blend percentages)

**Environmental Data**:
- Elevation (terrain height)
- Temperature (climate)
- Precipitation (moisture)
- Rivers and water bodies

**Seasonal Context**:
- Current season
- Snow coverage patterns
- Temperature variations

See [World Generation Data Model](/docs/design/features/world-generation/data-model.md) for complete sampling details.

### Generated by 2D System

**Micro-Detail** (not in 3D world):
- Individual entity placement (trees, rocks, flowers)
- Procedural color variation
- Texture patterns
- Per-tile uniqueness seeds

**Result**: Macro-scale coherence from 3D world + micro-scale variety from 2D rendering

## Player Experience

### What Players See

**Coherent World**:
- Biomes make sense (climatic zones, logical transitions)
- Features have explanations (why is there a desert here? Rain shadow from mountains)
- Walking reveals gradual ecosystem changes

**Hand-Crafted Feel**:
- Every tile looks unique
- Transitions are beautiful and organic
- No obvious repetition or grid patterns
- Coastlines, forest edges, and ecotones are memorable landmarks

**Infinite Exploration**:
- World wraps around (spherical)
- Consistent terrain when revisiting locations
- New chunks load seamlessly
- Exploration reveals endless variety within coherent geography

### Gameplay Impact

**Resource Distribution**:
- Biomes determine available resources
- Ecotones provide mixed resources from multiple biomes
- Strategic settlement decisions (pure vs mixed regions)

**Movement and Building**:
- Ground covers affect movement speed
- Building suitability varies by surface type
- Terrain challenges and opportunities

**Visual Feedback**:
- Clear biome recognition
- Obvious transition states
- Beautiful environments worth exploring

## Related Documentation

### 3D World Generation (Data Source)

This 2D rendering system depends on data from:
- [World Generation Overview](/docs/design/features/world-generation/README.md) - Complete 3D system
- [Data Model](/docs/design/features/world-generation/data-model.md) - How 2D samples 3D world
- [Biomes](/docs/design/features/world-generation/biomes.md) - Biome types in 3D world
- [Generation Phases](/docs/design/features/world-generation/generation-phases.md) - How biomes are assigned

### Visual Style

Overall aesthetic direction:
- [Visual Style](/docs/design/visual-style.md) - Color philosophy, aesthetic goals
- [UI Art Style](/docs/design/ui-art-style.md) - "High tech cowboy" theme

### Technical Implementation

Technical design documents:
- [Chunk Management System](/docs/technical/chunk-management-system.md) - Chunk structure, loading, caching, sparse storage
- [3D to 2D Sampling](/docs/technical/3d-to-2d-sampling.md) - Coordinate transformation, biome sampling, procedural generation

Future technical docs will cover:
- Tile rendering pipeline
- Entity placement systems
- Performance optimization strategies

## Success Criteria

A successful 2D game view will:

✅ **Feel coherent** - World makes geographic/climatic sense (from 3D generation)
✅ **Look hand-crafted** - No obvious procedural patterns or repetition
✅ **Be readable** - Clear biome identification, recognizable terrain
✅ **Be beautiful** - Transitions worth exploring, memorable landmarks
✅ **Perform well** - Smooth rendering, efficient sampling, fast chunk loading
✅ **Be consistent** - Same location looks identical when revisited

## Revision History

- 2025-10-26: Initial documentation created, organized 2D rendering docs separate from 3D world generation
