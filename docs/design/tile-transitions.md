# Tile Transitions

Created: 2025-10-26
Status: Design

## Overview

This document defines the visual appearance of tiles in transition zones (ecotones) where multiple biomes blend. With the biome influence system, transitions aren't hard edges between tile types - they're **gradual visual blending** over hundreds of tiles where ground covers, colors, and entities mix naturally.

**Core Philosophy**: Transitions should be beautiful, natural, and tell the story of where ecosystems meet. Every coastline, forest edge, and biome boundary should be unique and hand-crafted feeling.

## Understanding Transitions in the Biome System

### Not Tile Type Edges

**Traditional System** (NOT ours):
- Forest tile meets grass tile
- Hard edge, one tile is forest, next is grass
- Visual challenge: how to render the boundary

**Our System**:
- Tiles have biome percentages: { meadow: 70%, forest: 30% }
- No hard edge - gradual percentage shift over distance
- Visual challenge: how to render **blended** tiles

### What Creates a Transition

**Gradual Biome Percentage Change**:
```
Tile A: { meadow: 95%, forest: 5% }    Almost pure meadow
Tile B: { meadow: 90%, forest: 10% }   Mostly meadow, hint of forest
Tile C: { meadow: 80%, forest: 20% }   Meadow-dominant blend
Tile D: { meadow: 70%, forest: 30% }   Clear mixing
Tile E: { meadow: 50%, forest: 50% }   Balanced ecotone
...continues for hundreds of tiles...
```

**Player Sees**: Smooth ecosystem transition, not tile-by-tile changes

## Visual Components of Transitions

### Ground Cover Blending

**Pure Biome Ground**:
- Meadow (100%): All grass ground cover, green color
- Forest (100%): All forest floor cover, brown color

**Blended Ground** { meadow: 70%, forest: 30% }:
- **Color**: 70% meadow green + 30% forest brown = greenish-brown
- **Texture**: Predominantly grass, with patches of forest floor (pine needles, moss)
- **Pattern**: Organic mixing, not uniform - procedural noise determines where each shows

**Visual Result**: Natural-looking mixed ground, not obviously algorithmic

### Entity Distribution

**Entity Spawning by Percentage**:

**Meadow Entities** (spawn at meadow influence %):
- Wildflowers, grass tufts, small rocks

**Forest Entities** (spawn at forest influence %):
- Pine trees, mushrooms, ferns

**At Tile** { meadow: 80%, forest: 20% }:
- Wildflowers: 80% spawn chance → abundant
- Pine trees: 20% spawn chance → scattered, sparse
- **Visual**: Meadow with occasional pine tree

**Player Experience**: Gradual increase in tree density as they travel toward forest

### Color Palette Transitions

**Biome Color Palettes**:
- Meadow: Bright greens, yellow-greens, flower colors
- Boreal Forest: Dark greens, browns, gray-greens
- Desert: Yellows, oranges, tans
- Tundra: Gray-greens, browns, muted colors

**Blending Palettes**:
- Smooth color interpolation based on percentages
- Meadow (80%) + Forest (20%) = Meadow palette with brownish tint
- Not abrupt color change

**Gradient Effect**:
- Hundreds of tiles = very gradual palette shift
- Player barely notices per-tile color change
- Macro effect: "The world is getting darker/greener/browner"

## Specific Transition Examples

### Meadow → Forest

**Visual Journey** (traveling toward forest):

**Starting Point** { meadow: 100% }:
- Pure grass ground, bright green
- Abundant wildflowers
- No trees
- Open, bright feeling

**Early Transition** { meadow: 85%, forest: 15% }:
- Mostly grass, slight brownish tint
- First pine trees appear (sparse)
- Occasional forest floor patch
- Still feels like meadow

**Mid Transition** { meadow: 60%, forest: 40% }:
- Mixed grass and forest floor
- Moderate tree density
- Color: Green-brown blend
- Ambiguous - "is this meadow or forest?"

**Late Transition** { meadow: 25%, forest: 75% }:
- Mostly forest floor, grass in clearings
- Dense tree coverage
- Dark brown-green color
- Feels like forest now

**Deep Forest** { forest: 100% }:
- Pure forest floor, rich brown
- Very dense trees
- Mushrooms, ferns
- Dark, enclosed feeling

**Player Experience**: Days of travel, gradual ecosystem change, memorable journey

### Desert → Savanna

**Visual Journey**:

**Deep Desert** { desert: 100% }:
- Pure sand ground, yellow-orange
- Dunes, ripples
- Sparse cacti
- Hot, barren

**Desert Edge** { desert: 70%, savanna: 30% }:
- Mostly sand, patches of dry grass appearing
- Sand color shifting toward tan
- Occasional acacia tree (savanna entity, 30% spawn)

**Savanna Transition** { desert: 40%, savanna: 60% }:
- Mixed sand and dry grass
- Color: Tan-brown
- Scattered trees increasing

**Deep Savanna** { savanna: 100% }:
- Dry grass ground, brown-green
- Scattered acacia trees
- Warmer, life returning

**Player Experience**: Desert giving way to grassland, hope of resources

### Ocean → Beach → Grassland

**Narrow Transition** (coastal):

**Ocean** { ocean: 100% }:
- Deep blue water
- Waves, foam
- Impassable

**Shallow Water** { ocean: 80%, coastal: 20% }:
- Light blue water
- Bottom visible (sand)
- Wading depth

**Beach** { coastal: 100% }:
- Sand ground, white-tan
- Wet at water edge (darker)
- Shells, driftwood
- Narrow zone (50-100 tiles)

**Dune** { coastal: 70%, grassland: 30% }:
- Mostly sand, grass tufts appearing
- Dune formations
- Beach grass (sparse)

**Grassland** { grassland: 100% }:
- Green grass
- Away from coast

**Player Experience**: Dramatic coastal transition, sharp but natural

## Transition Visual Goals

### Organic and Irregular

**Not Uniform**:
- Ground cover mixing isn't 70% evenly distributed
- Clusters, patches, organic patterns
- Procedural variation makes each tile unique

**Natural Patterns**:
- Forest floor patches cluster (not scattered uniformly)
- Trees cluster in groups (not perfect grid)
- Color variation within percentages (not flat average)

**Hand-Crafted Illusion**:
- Player thinks "someone carefully placed these trees"
- Not obviously procedural
- Artistic, intentional feeling

### Gradual and Smooth

**Per-Tile Changes Subtle**:
- Adjacent tiles: percentage difference small (1-3% typically)
- Human eye barely perceives single-tile change
- Macro effect over many tiles obvious

**No Banding**:
- Avoid visible "rings" of same percentage
- Smooth gradient, not stepped
- Natural variation breaks up patterns

### Recognizable and Clear

**Despite Blending, Biomes Identifiable**:
- 70% meadow tile still reads as "meadow"
- 80% forest tile clearly "forest"
- Player not confused about environment

**Transition State Obvious**:
- Mid-transition (50/50) feels transitional
- Not mistaken for a third biome type
- "Mixed forest-meadow" clear

### Beautiful and Memorable

**Aesthetic Priority**:
- Transitions should be beautiful
- Coastlines worth exploring
- Forest edges inviting
- Desert boundaries dramatic

**Landmark Quality**:
- Ecotones are destinations, not just passages
- Players remember specific transition areas
- "That beautiful meadow-forest edge with the scattered pines"

## Visual Variation Within Transitions

### Procedural Uniqueness

**Same Biome Blend, Different Appearance**:
- Two tiles both { meadow: 70%, forest: 30% }
- But visually distinct due to procedural variation
- Different random seed → different tree placement, patch patterns

**Prevents Repetition**:
- Traveling through ecotone, every tile looks different
- No "I've seen this exact transition before"
- Exploration stays interesting

### Micro-Variation

**Within a Single Tile**:
- Color variation (not flat single color)
- Texture noise (grass blades, forest floor detail)
- Decoration placement variation

**Between Adjacent Similar Tiles**:
- Tree placement different
- Ground patch patterns unique
- Creates organic flow

## Multi-Biome Transition Complexity

### Three-Way Blends

**When Three Biomes Meet**:
```
Tile: { grassland: 50%, savanna: 30%, desert: 20% }
```

**Visual Challenge**:
- Blend three ground covers
- Three color palettes
- Three entity sets

**Visual Result**:
- Complex, rich ecosystem
- Unique three-way ecotone
- Difficult but rewarding to render beautifully

**Player Experience**: Rare, special locations where three biomes junction

### Dominant vs Minor Influences

**Simplification Strategy** [possible]:
- Ignore influences <10%
- Render only top 2 influences

**Example**:
```
Raw: { meadow: 73%, forest: 22%, desert: 5% }
Simplified: { meadow: 77%, forest: 23% } (desert dropped)
```

**Benefit**: Cleaner visuals, less rendering complexity

## Open Questions

### Visual Blending Technique

**1. How to Mix Ground Covers Visually?**:
- Procedural noise mask determines which cover where?
- Alpha blending of colors?
- Geometric pattern (Voronoi cells)?
- Needs visual prototyping

**2. Entity Clustering vs Uniform Distribution**:
- 20% pine trees: scattered uniformly or in natural clusters?
- Clustering feels more natural
- But how to determine cluster positions?

### Color Blending

**3. Interpolation Method**:
- Linear color interpolation?
- Perceptual color space (LAB)?
- Just mix RGB values?
- Affects how natural gradients look

**4. Palette Consistency**:
- Should biome palettes be designed to blend well?
- Or handle any combination?
- Some biome pairs might clash visually

### Complexity vs Performance

**5. How Many Biome Influences to Render**:
- Support 2? 3? 5+ blends per tile?
- More = richer but more complex/slower
- Needs performance testing

**6. LOD for Transitions**:
- Distant tiles: simplify blending?
- Close-up: full detail blending?
- How to transition between LOD levels smoothly?

### Seasonal Variation

**7. Do Transition Colors Change Seasonally**:
- Meadow-forest in autumn: brown grass + orange leaves?
- Or biome percentages stay same, only overlays (snow) change?
- Affects visual system complexity

## Related Documentation

**Game Design**:
- [biome-influence-system.md](./biome-influence-system.md) - How tiles get biome percentages
- [biome-ground-covers.md](./biome-ground-covers.md) - Ground cover types that blend
- [visual-style.md](./visual-style.md) - Overall aesthetic goals
- [procedural-variation.md](./procedural-variation.md) - How each tile varies

**Technical** (future):
- Ground cover blending techniques
- Color interpolation implementation
- Entity placement algorithms
- Performance optimization

## Revision History

- 2025-10-26: Complete rewrite for biome influence system - transitions are now percentage blends, not tile type edges
