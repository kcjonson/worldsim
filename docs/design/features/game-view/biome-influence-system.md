# Biome Influence System

Created: 2025-10-26
Status: Design / Core Innovation

## Overview

The biome influence system is the core innovation that creates natural, realistic ecosystems in World-Sim. Instead of tiles having discrete types, **each tile has percentage influences from nearby biomes**, creating gradual transition zones (ecotones) hundreds of tiles deep.

**Key Innovation**: Tiles at biome boundaries aren't "forest" or "meadow" - they're "80% meadow, 20% forest" with properties blending naturally.

## Data Source: 3D World Generation

### Where Biome Influences Come From

The biome influence system is a **rendering technique** for 2D tiles. The actual biome data comes from the [3D World Generation System](../world-generation/README.md).

**Data Flow**:
```
3D Spherical World
  ↓ (spherical tiles ~30km across, each with single definitive biome)
Sampling at 2D position
  ↓ (query: "what biomes are near coordinates X, Y?")
Biome Influence Calculation
  ↓ (result: biome percentages for this 2D tile)
2D Tile Rendering
```

**Key Architectural Point**: The 3D world stores sharp biome boundaries (Forest tile next to Grassland tile). The 2D rendering system creates smooth transitions by **blending** those sharp boundaries.

### Hybrid Approach: Two-Scale Transitions

The game uses a **two-scale system** for natural transitions:

**Primary: 3D Spherical Boundaries** (~500m transitions):
- Spherical tiles have single definitive biomes (sharp boundaries in 3D data)
- When 2D position is near a spherical tile boundary, blend the two biomes
- Distance to boundary determines blend percentage
- Example: 200m from Forest/Grassland boundary → 60% Grassland, 40% Forest

**Secondary: 2D Micro-Variation** (extends to 2-10km total):
- Within pure biome regions, add procedural noise for natural irregularity
- Extends visual transitions beyond immediate 3D boundaries
- Creates organic ecotone feel without overwhelming 3D structure
- Prevents vast uniform regions, adds hand-crafted appearance

**Result**:
- Major biome zones from realistic 3D world geography (coherent climate/geology)
- Enhanced with 2D detail for beautiful, natural-looking transitions
- Ecotone depth is the COMBINED effect of both scales

See [World Generation Data Model](../world-generation/data-model.md) for complete details on 3D→2D sampling.

## The Problem with Discrete Tile Types

### Traditional Approach (What We're NOT Doing)

**Hard Boundaries**:
- Each tile belongs to exactly one biome
- Forest tiles next to meadow tiles with sharp line between
- Unrealistic: nature doesn't have hard edges
- Repetitive: all forest tiles identical
- No transition zones

**Player Experience**:
- Crossing from meadow to forest = instant change
- Obvious grid structure
- World feels artificial

## The Biome Influence Solution

### Percentage-Based Influences

**Instead of Discrete Types**:
- Tiles don't "belong to" a single biome
- Each tile influenced by ALL nearby biomes
- Influence strength based on distance

**Example Transition**:
```
As you travel from meadow toward forest:

Deep Meadow: { meadow: 100%, forest: 0% }
              Pure meadow, no trees

Edge: { meadow: 95%, forest: 5% }
      Meadow with occasional pine tree

Ecotone: { meadow: 80%, forest: 20% }
         Mostly meadow, scattered pine trees, some forest floor patches

Mid-Transition: { meadow: 60%, forest: 40% }
                Mixed ecosystem, both biomes visible

Forest Edge: { meadow: 20%, forest: 80% }
             Mostly forest, occasional meadow clearing

Deep Forest: { meadow: 0%, forest: 100% }
             Pure forest
```

**Natural Result**: Hundreds of tiles of gradual blending

## How Influences Determine Properties

### Ground Cover Appearance

**Visual Rendering** = weighted blend of biome ground covers:

**Pure Biome**:
- Meadow (100%): All grass ground cover
- Forest (100%): All forest floor ground cover

**Blended Ecotone**:
- { meadow: 70%, forest: 30% }
- Ground: 70% grass appearance, 30% forest floor appearance
- Color: Green with brownish tint
- Texture: Grass dominant with pine needle patches

**Player Sees**: Natural-looking transition, not obvious blend

### Entity Placement (Trees, Plants, Decorations)

**Spawn Probability** = biome influence percentage:

**Meadow Biome Entities**:
- Wildflowers
- Grass tufts
- Small rocks

**Forest Biome Entities**:
- Pine trees
- Mushrooms
- Ferns

**At Ecotone** { meadow: 80%, forest: 20% }:
- Wildflowers: 80% chance to spawn → common
- Pine trees: 20% chance to spawn → sparse
- Visual result: Meadow with scattered pine trees

**At Deeper Transition** { meadow: 40%, forest: 60% }:
- Wildflowers: 40% chance to spawn → occasional
- Pine trees: 60% chance to spawn → moderate density
- Visual result: Mixed forest-meadow

**Player Experience**: Gradual ecosystem change

### Gameplay Properties

**Movement Speed**:
- Meadow biome: Fast (grass is easy terrain)
- Forest biome: Slightly slower (forest floor, trees)
- Ecotone { meadow: 70%, forest: 30% }: Between the two, closer to meadow speed

**Resource Availability**:
- Pure meadow: Abundant grass, no mushrooms
- Pure forest: Abundant mushrooms, no grass
- Ecotone: BOTH resources available (weighted by percentage)

**Building Suitability**:
- Meadow: Ideal for building
- Forest: Good but must clear trees
- Ecotone: Depends on dominant influence

## Transition Zones (Ecotones)

### What Are Ecotones?

**Definition**: Natural transition zones between ecosystems where species from both mix

**In World-Sim**:
- Not a single line of tiles
- Hundreds of tiles deep
- Gradual percentage shifts
- Realistic natural boundaries

### Ecotone Depth

**Transition Width** - Combined effect of two-scale system:

**At 3D Spherical Boundaries** (~50 tiles / 500m):
- Where spherical world tiles meet
- Core transition driven by distance to 3D boundary
- Sharp in 3D data, smooth in 2D rendering

**With 2D Micro-Variation** (200-1000 tiles / 2-10km total):
- Procedural noise extends transitions
- Creates natural irregularity and organic patterns
- Prevents abrupt pure→transition edges
- Final ecotone depth is this combined width

**Tunable Parameters**:
- 3D blend distance: Fixed at ~500m
- 2D variation range: Tunable (determines total ecotone depth)
- Different biome pairs can have different 2D variation ranges
  - Forest ↔ Meadow: Wide 2D variation (10km ecotone)
  - Ocean ↔ Beach: Minimal 2D variation (stays near 500m)

**Design Goal**: Wide enough for realistic ecotones, preserving distinct pure biome regions from 3D world

### Variable Transition Width

**Possible Enhancement**:
- Different biome pairs have different transition widths
- Forest ↔ Meadow: Wide, gradual (400 tiles)
- Ocean ↔ Beach: Narrow, dramatic (50 tiles)
- Mountain ↔ Lowland: Narrow, elevation-based

**Benefit**: Different ecosystem boundaries feel unique

## Player Experience

### Exploration and Discovery

**Traveling Through Ecotones**:
1. **Departure**: Player leaves meadow settlement
2. **Early Transition** (100 tiles): First pine trees appear, still feels like meadow
3. **Mid Transition** (200 tiles): Mixed environment, both ecosystems visible
4. **Late Transition** (300 tiles): Feels like forest now, occasional meadow clearing
5. **Arrival**: Deep forest, distinct from meadow

**Journey Takes Time**: Not instant biome change, realistic travel experience

**Memorable Transitions**:
- "We traveled through the ecotone for days before reaching the deep forest"
- Players remember the journey, not just the destination
- Transitions become gameplay spaces

### Navigation and Orientation

**Biome Purity as Landmark**:
- Deep forest core: Densest trees, darkest
- Pure meadow: Open grassland, bright
- Players orient: "We're getting closer to pure forest" or "Forest is thinning"

**Gradual Change Feedback**:
- Player sees ecosystem changing
- Can turn back before committing to new biome
- Not surprised by sudden environment shift

### Resource Strategy

**Ecotone Advantages**:
- Access to resources from MULTIPLE biomes
- Meadow-forest edge: Grass for grazing AND wood from trees
- Mushrooms (forest) AND wildflowers (meadow)
- Strategic settlement location

**Biome Purity Trade-offs**:
- Deep forest: No grass, hard to raise livestock
- Pure meadow: No trees, limited wood
- Players must choose: purity vs variety

**Gameplay Decision**:
- Settle in ecotone: Mixed resources, moderate in everything
- Settle in pure biome: Specialized, must trade or travel for other resources

## Multi-Biome Influences

### More Than Two Biomes

**Tiles Can Be Influenced By 3+ Biomes**:
```
Tile near desert-savanna-grassland junction:
{ grassland: 50%, savanna: 30%, desert: 20% }

Visual: Mostly grass, some dry patches, occasional sand
Entities: Mix of grassland flowers, savanna trees, desert cacti
```

**Creates Complex Ecosystems**: Rich biodiversity at biome junctions

### Influence Thresholds

**Ignore Weak Influences** [design choice]:
- Influences <5% might be ignored
- Simplifies rendering and gameplay
- Prevents "noisy" biome blending

**Example**:
```
Raw: { meadow: 78%, forest: 18%, desert: 4% }
Simplified: { meadow: 81%, forest: 19% } (desert removed, renormalized)
```

**Benefit**: Cleaner, more understandable tile properties

## Biome Compatibility and Transitions

### Natural Adjacencies

**Realistic Transitions** (feel natural):
- Ocean → Beach → Grassland → Forest
- Desert → Savanna → Grassland
- Grassland → Forest → Mountain → Alpine
- Lowland → Hill → Mountain (elevation-based)

**Unrealistic Adjacencies** (avoid):
- Tropical rainforest directly bordering arctic tundra
- Desert immediately next to swamp
- Ocean in middle of desert

**3D World Generation Ensures**: Only plausible biome combinations neighbor each other (see [Biome Types](../world-generation/biomes.md))

### Sharp vs Gradual Transitions

**Some Transitions Should Be Sharp**:
- Coastline (ocean to land): Narrow transition zone
- Cliff face (elevation drop): Dramatic change
- Lava field edge: Extreme hazard boundary

**Most Transitions Should Be Gradual**:
- Forest to meadow: Wide ecotone
- Desert to savanna: Very gradual moisture change
- Grassland to tundra: Slow temperature/vegetation change

**Design Challenge**: Balance realism with dramatic geography

## Seasonal Variations

### Biome Influences Don't Change with Seasons

**Stable Structure**:
- A tile's biome percentages remain constant
- Meadow-forest ecotone doesn't shift in winter

**Seasonal Changes Are Overlays** (separate system):
- Snow coverage in winter (on top of ground)
- Frost effects
- Vegetation color changes (grass brown in summer drought)
- Water freezing

**Benefit**: Simpler system, predictable world structure

## Open Questions

### Transition Tuning

**1. Ecotone Width**:
- How many tiles deep should transitions be?
- 200 tiles? 500 tiles?
- Does it vary by biome pair?
- Needs playtesting to feel right

**2. Influence Falloff**:
- How quickly does influence decrease with distance?
- Linear, curved, exponential?
- Affects ecotone character (gradual vs sharp)

**3. Multiple Biome Complexity**:
- Allow 2? 3? 5+ biomes per tile?
- More = richer but more complex
- Performance and rendering implications

### Biome Catalog

**4. How Many Biomes?**:
- 10-15 for MVP?
- 30+ for full Earth-like diversity?
- Balance: variety vs overwhelming complexity

**5. Biome Granularity**:
- Separate "Pine Forest" vs "Oak Forest" biomes?
- Or one "Temperate Forest" with tree variation?
- Affects influence system complexity

### Gameplay Balance

**6. Ecotone Resource Advantage**:
- Is settling in ecotone too advantageous (access to multiple biomes)?
- Should pure biomes have benefits to balance?
- Playtesting needed

**7. Transition Navigation**:
- Can players easily understand where they are in transition?
- Visual feedback clear enough?
- UI indicators needed?

## Visual Goals

### What Players Should See

**Gradual Change**:
- Walking from meadow to forest feels like a journey
- Each tile slightly different from last
- No obvious "grid line" where biomes meet

**Natural Mixing**:
- Pine trees scattered in meadow grass (ecotone)
- Forest floor patches in grass
- Color gradients smooth, not banded

**Hand-Crafted Feel**:
- Ecotones feel intentionally designed
- Not obviously algorithmic
- Unique per location (procedural variation)

**Recognizable Biomes**:
- Despite blending, player can identify "this is mostly forest"
- Pure biomes clearly distinct
- Transitional state obvious but not confusing

## Related Documentation

**3D World Generation** (data source):
- [World Generation Overview](../world-generation/README.md) - Complete 3D system
- [Data Model](../world-generation/data-model.md) - How 2D samples 3D world (critical reference)
- [Biome Types](../world-generation/biomes.md) - Biome catalog and classification

**2D Game View** (this folder):
- [Game View Overview](./README.md) - How 2D rendering works
- [biome-ground-covers.md](./biome-ground-covers.md) - Ground cover types used by biomes
- [tile-transitions.md](./tile-transitions.md) - Visual appearance of transitions
- [procedural-variation.md](./procedural-variation.md) - How biome blends vary per tile

**Visual Style**:
- [visual-style.md](../../visual-style.md) - Overall visual aesthetic

**Technical** (future):
- How biome influences are calculated from 3D world data
- Rendering biome-blended tiles
- Entity placement algorithms
- Performance optimization

## Revision History

- 2025-10-26: Moved to game-view folder, added 3D world context, documented hybrid approach
- 2025-10-26: Initial biome influence system design - removed technical details, focused on player experience and game design
