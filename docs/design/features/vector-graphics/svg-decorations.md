# SVG Decorations and Entities

Created: 2025-10-26
Status: Design

## Overview

SVG assets serve as decorations and entities placed on top of procedurally-generated tile ground covers. They add visual richness, gameplay elements, and bring the world to life.

**This Document**: Covers SVG assets as **placed objects** (decorations and entities)

**See Also**: [svg-texture-patterns.md](./svg-texture-patterns.md) for SVG patterns used as fills in code-drawn shapes

## SVG as Placed Objects

**Key Distinction**:
- **Tiles**: Procedurally-generated ground covers (grass, sand, rock)
- **SVG Decorations**: Small visual elements on tiles (flowers, pebbles, grass tufts)
- **SVG Entities**: Interactive gameplay objects (trees, creatures, harvestable resources)

**Rendering**:
- Each SVG is a distinct object placed at specific coordinates
- Has position, rotation, scale
- Can be animated (trees swaying)
- Can be interactive (harvestable, clickable)

## Decoration Categories

### Ground Decorations (Non-Interactive)

**Purpose**: Visual detail, breaking up tile repetition

**Characteristics**:
- Small scale (1/4 tile or less)
- Non-interactive (purely visual)
- Flat or low-profile
- Performance: many per tile (5-10)

**Examples**:
- Wildflowers (multiple species)
- Grass tufts
- Small pebbles
- Mushrooms (non-harvestable varieties)
- Fallen leaves
- Twigs, pinecones
- Small patches of moss

**Meadow Decorations**:
- Wildflowers (daisies, dandelions, poppies)
- Grass tufts (taller grass clumps)
- Small stones
- Clover patches

**Forest Decorations**:
- Mushrooms (decorative, small)
- Ferns (small)
- Fallen leaves
- Pinecones
- Twigs
- Moss patches

**Desert Decorations**:
- Small rocks
- Dead brush
- Bone fragments
- Dry grass wisps

**Beach Decorations**:
- Shells (multiple types)
- Driftwood (small)
- Seaweed
- Pebbles
- Tide pool creatures (starfish, small)

### Entities (Interactive Gameplay Elements)

**Purpose**: Gameplay objects, resources, obstacles

**Characteristics**:
- Larger scale (1/2 tile to multiple tiles)
- Interactive (harvestable, blocking, AI-driven)
- Standing/prominent
- Performance: fewer per tile (0-3)

**Examples**:
- Trees (harvestable wood)
- Large bushes (berries, resources)
- Boulders (obstacles, mining)
- Creatures (animals, colonists)
- Structures (player-built, ruins)

**Forest Entities**:
- Pine trees (boreal)
- Oak trees (temperate deciduous)
- Birch trees (temperate)
- Ferns (large, harvestable)
- Mushroom clusters (harvestable)

**Desert Entities**:
- Cacti (multiple varieties)
- Large rocks
- Dead trees
- Tumbleweeds (might be animated)

**Meadow Entities**:
- Wildflower patches (large, harvestable)
- Berry bushes
- Scattered trees (sparse)
- Grazing animals

## Placement Rules

### Biome-Appropriate Placement

**Decoration Type ↔ Biome Match**:
- Only spawn decorations appropriate to biome
- Meadow biome: meadow decorations + entities
- Forest biome: forest decorations + entities
- Blend tiles: mix based on percentages

**Example Blend Tile** { meadow: 70%, forest: 30% }:
- Wildflowers: 70% spawn chance
- Grass tufts: 70% spawn chance
- Pine trees: 30% spawn chance
- Mushrooms: 30% spawn chance

**Natural Ecosystem**: Decorations and entities match environment

### Avoid Tile Edges

**Safe Zone Strategy**:
- Don't place decorations near tile edges (0.2 tile margin)
- Prevents clustering at chunk boundaries
- Ensures decorations fit within tile visually

**Why**:
- Tile edges might have ground cover transitions
- Visual clarity
- Prevents overlap issues

### Clustering and Natural Distribution

**Not Uniform Grids**:
- Flowers cluster in patches (natural grouping)
- Rocks scattered organically
- Trees cluster in groups

**Clustering Patterns**:
- Some decorations: tight clusters (flowers in patches)
- Some decorations: scattered (trees dispersed)
- Some decorations: uniform avoidance (trees spaced apart)

**Procedural Variation**:
- Same decoration type, different placement per tile
- Deterministic (same tile seed = same placement)
- Unique per location

### Density Control

**Biome Density Settings**:
- Lush meadow: high flower density
- Sparse desert: low decoration density
- Dense forest: moderate decoration density (ground visible)

**Per-Tile Variation**:
- Some tiles denser, some sparser
- Procedural noise determines density multiplier
- Natural variation

## Color and Style Integration

### Matching Tile Appearance

**Color Harmony**:
- Decoration colors complement ground cover
- Meadow flowers: colors that work with green grass
- Forest mushrooms: colors that work with brown forest floor

**Biome Palette Coordination**:
- Meadow palette: bright, colorful
- Forest palette: muted, earthy
- Desert palette: warm, dry colors

### Style Consistency

**Art Style Coherence**:
- All SVG assets match overall visual style
- Consistent line weight, detail level
- Unified aesthetic across decorations

**Hand-Crafted Feel**:
- SVG assets hand-designed (not generated)
- Artistic quality
- Each decoration is an authored asset

## Decoration Examples by Biome

### Temperate Meadow

**Ground Decorations** (5-10 per tile):
- Daisies (white, yellow center)
- Dandelions (yellow)
- Poppies (red)
- Grass tufts (taller grass clumps)
- Clover (small, clustered)
- Small stones

**Entities** (0-2 per tile):
- Berry bushes (harvestable)
- Wildflower patches (large)
- Occasional tree (sparse meadow)

### Boreal Forest

**Ground Decorations** (3-8 per tile):
- Mushrooms (red, brown, white varieties)
- Pinecones
- Moss patches (green)
- Fallen twigs
- Ferns (small)

**Entities** (2-5 per tile in dense areas):
- Pine trees (primary)
- Birch trees (occasional)
- Fern clusters (harvestable)
- Mushroom clusters (harvestable)
- Boulders (sparse)

### Hot Desert

**Ground Decorations** (1-3 per tile):
- Small rocks
- Dead brush
- Bone fragments
- Dry grass wisps

**Entities** (0-1 per tile):
- Cacti (saguaro, barrel, prickly pear)
- Large rocks/boulders
- Dead trees (skeletal)

### Coastal Beach

**Ground Decorations** (3-7 per tile):
- Shells (clam, scallop, conch)
- Driftwood (small pieces)
- Seaweed (washed up)
- Pebbles (smooth, varied)
- Starfish (small)

**Entities** (0-1 per tile):
- Driftwood logs (large)
- Tide pools (might be special case)
- Occasional palm tree (tropical beach)

## Performance Considerations

### Decoration Budget

**Target** [from vector graphics docs]:
- 10,000+ total entities on screen @ 60 FPS
- Includes decorations + entities + creatures

**Per-Tile Budget**:
- Ground decorations: 5-10 (small, simple)
- Entities: 0-3 (larger, more complex)

**LOD Strategy**:
- Close-up: All decorations visible
- Medium distance: Reduce decoration count
- Far distance: No decorations, entities only

### Rendering Approach

**From Vector Graphics System**:
- SVG decorations = Tier 2 (semi-static, cached)
- SVG entities = Tier 2 (static) or Tier 3 (animated)
- Batched rendering for performance

## Open Questions

### Decoration Variety

**1. How Many Decoration Types Per Biome?**:
- 5-10 types per biome?
- More variety = more visual interest
- But more assets to create

**2. Seasonal Decoration Changes?**:
- Flowers bloom in spring, gone in winter?
- Or decorations static year-round?
- Complexity vs realism trade-off

### Placement Tuning

**3. Optimal Density?**:
- How many flowers per meadow tile?
- Too many = cluttered
- Too few = sparse, boring
- Needs visual testing

**4. Clustering vs Scattering?**:
- All decorations cluster, or some scatter?
- Per-decoration-type rules?

### Performance Validation

**5. Can We Hit 10k Entity Target?**:
- With decorations + entities + creatures?
- Need to prototype and profile
- May need to reduce decoration count

**6. LOD Transition Smoothness?**:
- How to fade decorations in/out gracefully?
- Pop-in visible and jarring?

## Related Documentation

**Game Design**:
- [README.md](./README.md) - Overview of all SVG asset uses
- [svg-texture-patterns.md](./svg-texture-patterns.md) - SVG patterns as fills
- [biome-influence-system.md](../game-view/biome-influence-system.md) - Decoration placement probability
- [biome-ground-covers.md](../game-view/biome-ground-covers.md) - Ground layers decorations sit on
- [procedural-variation.md](../game-view/procedural-variation.md) - Decoration placement variation
- [animated-vegetation.md](./animated-vegetation.md) - Animated tree entities

**Technical** (future):
- SVG asset pipeline
- Decoration placement algorithms
- Entity rendering integration
- Performance optimization

## Revision History

- 2025-10-26: Initial SVG decorations and entities design for procedural tile system
