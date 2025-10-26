# Procedural Variation

Created: 2025-10-26
Status: Design / Philosophy

## Overview

Procedural variation is the system that ensures no two tiles, coastlines, or forest edges look identical - creating a world that feels hand-crafted and unique at every location. This document defines **why** we need variation, **what** should vary, and **what** must stay consistent.

**Core Philosophy**: Use code to achieve hand-crafted uniqueness at massive scale. Every tile should feel intentionally placed, not obviously generated.

**Context**: Procedural variation adds micro-detail to tiles that already have biome data from the [3D World Generation System](../world-generation/README.md). The 3D world determines "what" (meadow, forest, desert), variation determines "how it looks" (this specific meadow tile's unique appearance).

## Why Procedural Variation Matters

### The Problem We're Solving

**Without Variation**:
- Every grass tile looks identical
- All coastlines have the same curve
- Forest edges are uniform
- Players see repetition everywhere
- World feels artificial, computer-generated
- Screenshots all look the same
- Exploration becomes boring

**With Variation**:
- Each grass tile slightly different color, different decorations
- Every coastline unique and interesting
- Forest edges vary in tree placement and density
- World feels alive, hand-crafted
- Screenshots are unique
- Exploration rewarding

### Player Experience Goals

**Sense of Discovery**:
- "I've never seen this exact coastline before"
- Unique landmarks: "That oddly-shaped peninsula"
- Exploration reveals new sights
- Players want to share screenshots (not generic)

**Hand-Crafted Illusion**:
- Player thinks: "Someone carefully designed this"
- Not: "This is obviously procedurally generated"
- Artistic, intentional feeling
- Variation within aesthetic coherence

**Memorable World**:
- Locations are distinct and memorable
- "The forest edge with the three tall pines"
- "That rocky beach with the tide pools"
- Places become landmarks, not grid coordinates

### Art Production Efficiency

**The Goal** [from visual-style.md]:
- NOT reducing asset count for performance
- IS achieving hand-crafted variety without manual labor

**How**:
- Artists define rules, parameters, palettes
- Code generates infinite variations within those rules
- Quality: hand-crafted aesthetic
- Quantity: procedural scale

**Example**:
- Artist: "Meadow grass is yellow-green to blue-green, with wildflowers"
- Code: Each tile samples different shade, different flower placement
- Result: 10,000 unique meadow tiles, all recognizably "meadow"

## What Should Vary

### Ground Appearance

**Color Variation**:
- Each tile: slightly different shade within biome palette
- Meadow: Some tiles yellow-green, others blue-green
- Forest floor: Some darker brown, others reddish-brown
- Range: Noticeable but subtle (±10-20% lightness/saturation)

**Texture Patterns**:
- Grass blade orientation varies
- Forest floor: different leaf/needle patterns
- Rock: crack patterns unique per tile
- Sand: ripple directions and spacing vary

**Coverage Density**:
- Grass coverage: some tiles lusher, others sparser
- Forest floor: leaf thickness varies
- Not uniform - organic variation

### Entity and Decoration Placement

**Position Variation**:
- Flowers: never in perfect grid
- Trees: clustered naturally, not evenly spaced
- Rocks: scattered organically
- Each tile: different placement pattern

**Rotation Variation**:
- Trees rotated randomly
- Flowers face different directions
- Rocks oriented uniquely
- Natural, not aligned

**Scale Variation**:
- Trees: slight height variation (80%-120% of base)
- Flowers: some larger, some smaller
- Rocks: size variation
- Natural variation, not identical clones

**Selection Variation** (when multiple options exist):
- Meadow might have 5 flower types
- Each meadow tile: different mix of which flowers
- Forest: different tree species mix
- Variety within biome

### Biome Blend Variation

**Same Percentages, Different Appearance**:
```
Tile A: { meadow: 70%, forest: 30% }
Tile B: { meadow: 70%, forest: 30% }

But visually different:
- Tile A: Pine trees clustered on left, grass on right
- Tile B: Pine trees scattered throughout
- Different procedural seed → different pattern
```

**Ecotone Uniqueness**:
- Same biome transition, endless variations
- Every forest-meadow edge looks different
- Coastlines infinitely varied

## What Must Stay Consistent

### Recognizability

**Biomes Must Be Identifiable**:
- All meadow tiles recognizably "meadow" (green grass)
- All forest tiles clearly "forest" (brown floor, trees)
- Variation within bounds - never crosses into ambiguity
- Player instant recognition: "This is grass terrain"

**Color Palette Coherence**:
- Meadow always green range (never purple)
- Desert always tan/orange range (never blue)
- Variation explores palette, doesn't break it

### Gameplay Properties

**Consistent Mechanics**:
- All meadow tiles: same movement speed
- All sand tiles: same building difficulty
- Variation is visual only, not gameplay-affecting
- Exception: Biome blend tiles interpolate properties

**Resource Consistency**:
- Meadow always has grass resources
- Forest always has mushrooms
- Player can rely on biome properties
- Visual variation doesn't affect functionality

### Biome Identity

**Coherent Ecosystem**:
- Meadow decorations: flowers, grass, compatible elements
- Forest decorations: mushrooms, ferns, forest-appropriate
- No desert cacti in meadow (unless blend tile)
- Biome theming maintained

## Layers of Variation

### Per-Tile Variation

**Unique Seed Per Tile**:
- Each tile gets deterministic seed from world position
- Same position always generates same appearance
- Different positions generate different seeds

**Deterministic**:
- Re-visiting same tile: looks identical
- Not random each time
- Consistent world

**Micro-Variation**:
- Color shade
- Decoration placement
- Pattern orientation

### Per-Biome Variation

**Regional Character** (from 3D world context):
- Northern forest: color palette shifts toward cooler tones (influenced by 3D climate data)
- Southern meadow: warmer, yellower grass (influenced by 3D temperature)
- Coastal grass: windswept, sparse (influenced by 3D proximity to ocean)
- Altitude effects: alpine meadow different from lowland (influenced by 3D elevation)

**Macro-Variation**:
- Entire regions have character
- Within consistent rules
- Gradual shifts across world

### Per-Decoration Variation

**Individual Entities**:
- Each tree: unique seed
- Rotation, scale, specific shape variation
- Flower: color variant, size
- Rock: shape, orientation

**Population Diversity**:
- Forest: mix of tree sizes
- Meadow: variety of flower colors
- Natural diversity within species

## Balancing Variety and Coherence

### Too Little Variation

**Symptoms**:
- Tiles look copy-pasted
- Boring, repetitive
- "I've seen this before" feeling
- World feels artificial

**Result**: Poor player experience, no exploration reward

### Too Much Variation

**Symptoms**:
- Chaotic, noisy
- No recognizable patterns
- Visual confusion
- "What biome is this?" unclear

**Result**: Overwhelming, unreadable world

### The Sweet Spot

**Characteristics**:
- Instantly recognizable biomes
- But every tile unique
- Coherent aesthetic
- Endless visual interest
- Natural, organic feeling

**Needs Tuning**:
- Variation ranges need playtesting
- Visual prototypes to find balance
- Iterative refinement

## Procedural Techniques

**Note**: This is design goals - technical implementation in technical docs

### Seed-Based Generation

**Deterministic Randomness**:
- Tile at (x, y) always gets same seed
- Seed generates "random" numbers
- Same seed = same results
- Different seeds = different results

**Benefits**:
- Consistent world
- Networked multiplayer compatible
- Cacheable

### Noise Functions

**Continuous Variation**:
- Sample noise at world coordinates
- Smooth gradients across tiles
- Natural-looking patterns
- Color shifts, density variations

**Types**:
- Perlin noise: Smooth, organic
- Simplex noise: Similar, faster
- Worley noise: Cellular patterns

### Hybrid Approach

**Seed for Discrete Choices**:
- "Which of 5 flower types?"
- Random selection from seed

**Noise for Continuous Values**:
- "How dark is the grass?" (0.8-1.2x base color)
- "How dense is coverage?" (60%-100%)
- Sample noise at position

## Player-Facing Examples

### Coastline Variation

**Same Biome Transition** (ocean → beach → grassland):
- Northern coast: Rocky, dramatic, large boulders
- Southern coast: Sandy, gentle, smooth curves
- Eastern coast: Mixed, moderate variation
- Western coast: Tidal pools, irregular

**All recognizably "coast"**, but infinitely varied

### Forest Edge Variation

**Same Ecotone** (meadow 70%, forest 30%):
- Edge A: Pine trees in tight clusters, open grass between
- Edge B: Pine trees scattered uniformly
- Edge C: Pine trees along irregular line
- Edge D: Mixed clustering and spacing

**All recognizably "forest-meadow edge"**, all unique

### Meadow Variation

**Same Pure Biome** (meadow 100%):
- Meadow A: Bright yellow-green, dense wildflowers, lush
- Meadow B: Blue-green, sparse flowers, mown appearance
- Meadow C: Mixed greens, moderate flowers, natural
- Meadow D: Dry yellow-brown (seasonal or region), few flowers

**All recognizably "meadow"**, visual variety

## Open Questions

### Variation Ranges

**1. Color Variation Range**:
- How much can grass green vary? (±10%? ±30%?)
- Too much = unrecognizable
- Too little = boring
- Needs visual testing

**2. Entity Density Variation**:
- Fixed density per biome, or varies?
- Some meadows denser flowers than others?
- Or consistent density, just placement varies?

**3. Scale Variation Range**:
- Trees: 80%-120% of base size?
- Or wider range?
- Natural variation limits?

### Consistency Requirements

**4. Cross-Tile Continuity**:
- Should adjacent tiles coordinate decorations?
- Or each tile completely independent?
- Prevent clustering at tile edges?

**5. Regional vs Per-Tile**:
- Some variation regional (entire forest darker)?
- Or purely per-tile (each tile independent)?
- Balance between macro and micro variation?

### Performance Constraints

**6. Variation Complexity**:
- How much computation per tile acceptable?
- Simpler variation for distant tiles (LOD)?
- Cache variation results?

## Related Documentation

**3D World Generation** (provides context):
- [World Generation Overview](../world-generation/README.md) - Creates base biome data that variation enhances
- [Data Model](../world-generation/data-model.md) - Environmental data (temperature, climate) that influences regional variation

**2D Game View** (this folder):
- [Game View Overview](./README.md) - How 2D rendering works
- [biome-influence-system.md](./biome-influence-system.md) - Biome blending creates variation
- [biome-ground-covers.md](./biome-ground-covers.md) - What ground covers vary
- [tile-transitions.md](./tile-transitions.md) - Transition appearance variation

**Visual Style**:
- [visual-style.md](../../visual-style.md) - Overall aesthetic goals

**Technical** (future):
- Seed generation algorithms
- Noise function implementation
- Procedural decoration placement
- Performance optimization

## Revision History

- 2025-10-26: Moved to game-view folder, added 3D world context for regional variation
- 2025-10-26: Initial procedural variation philosophy document
