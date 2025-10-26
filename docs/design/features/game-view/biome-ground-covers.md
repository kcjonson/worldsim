# Biome Ground Covers

Created: 2025-10-26
Status: Design / Initial Catalog

## Overview

This document defines the **ground cover types** that form the visual and gameplay foundation of tiles. Ground covers are the base layers (what's underfoot) used by different biomes.

**Critical Distinction**:
- **Ground Covers** (this doc): Permanent physical ground surface types - grass, dirt, sand, rock, water
- **Biomes** (separate system): Ecological zones that use ground covers - meadow, boreal forest, tundra, desert
- **Tiles** (rendering): Have percentage influence from multiple biomes, which determines ground cover blend
- **Seasonal Overlays** (separate): Snow, frost, wetness - temporary coverage on top of ground covers

## Architecture Concept

### Data Source: From 3D World to Ground Covers

**The Flow**:
```
3D World Generation
  ↓ (assigns biomes to spherical tiles)
2D Sampling
  ↓ (queries biome at coordinates, calculates biome percentages)
Biome Influence System
  ↓ (tiles get biome percentages)
Ground Cover Rendering (this document)
  ↓ (biomes map to ground covers, percentages determine blend)
Visual Display
```

**Key Point**: Ground covers are determined by biome percentages, which come from sampling the [3D World Generation System](../world-generation/README.md). This document describes how those biomes translate into visual ground surfaces.

See [biome-influence-system.md](./biome-influence-system.md) for complete details on how tiles get biome percentages.

### How Ground Covers Work

**Tiles Don't Have Single Types**:
- Each tile has percentage influences from nearby biomes (from 3D world sampling)
- Example: `{ meadow: 80%, borealForest: 20% }`
- Biomes define which ground covers they use
- Tile ground appearance = blend of biome ground covers weighted by percentages

**Example Tile Rendering**:
```
Tile at forest edge:
- Meadow influence: 80% → uses grass ground cover
- Boreal forest influence: 20% → uses forest floor ground cover
- Visual result: 80% grass appearance, 20% pine needle/moss appearance
- Ground color: interpolated between meadow green and forest brown
```

**Entity Placement**:
```
Same tile:
- Meadow wildflowers: 80% spawn chance
- Pine trees: 20% spawn chance (sparse)
- Result: Mostly meadow with occasional pine tree, natural ecotone
```

### Ground Cover vs Biome vs Seasonal Overlay

**Ground Cover**: Permanent physical material
- Grass, dirt, sand, forest floor, rock, water
- Doesn't change with seasons
- Rendered appearance base layer

**Biome**: Ecological zone using ground covers (from 3D world)
- Meadow biome → uses grass ground cover
- Boreal forest biome → uses forest floor ground cover
- Desert biome → uses sand ground cover
- See [3D World Biomes](../world-generation/biomes.md) for complete biome catalog

**Seasonal Overlay**: Temporary coverage (separate system)
- Snow coverage: 0-100% in winter
- Frost, ice, wetness
- Renders on top of ground cover
- Example: Grass ground + 80% snow overlay = snowy meadow

## Ground Cover Types

### Grass Cover

**Physical Description**:
- Living grass blades, herbaceous plants
- Soft, organic surface
- Green color range (varies by biome/climate)

**Visual Appearance** [OPEN QUESTION - palette needed]:
- Color: Green (yellow-green to blue-green range)
- Texture: Soft, living, organic
- Decorations: Grass tufts, small flowers
- Variation: Coverage density, health, color shifts

**Gameplay Properties**:
- Movement: Normal speed (1.0x)
- Traversability: Fully passable
- Building: Excellent foundation

**Used By Biomes**:
- Temperate meadow
- Temperate grassland (prairie)
- Savanna (dry grass variant)
- Tundra (short grass variant)
- Any biome clearings/edges

**Biome Variations**:
- Temperate: Lush green, tall
- Savanna: Dry yellow-brown, sparse
- Tundra: Short, brown-green, hardy
- Alpine meadow: Short, vibrant green

### Forest Floor Cover

**Physical Description**:
- Decomposing organic matter (leaf litter, pine needles)
- Moss, low plants, undergrowth
- Darker, richer soil visible

**Visual Appearance** [OPEN QUESTION - palette needed]:
- Color: Brown to dark brown, green moss patches
- Texture: Organic, layered, damp
- Decorations: Fallen leaves, twigs, mushrooms, ferns
- Variation: Leaf type (deciduous vs coniferous), moss coverage

**Gameplay Properties**:
- Movement: Slightly slow (0.9x, soft ground)
- Traversability: Fully passable
- Building: Good (clear trees first)

**Used By Biomes**:
- Temperate deciduous forest
- Temperate coniferous forest
- Boreal forest (pine needle variant)
- Tropical rainforest (thick litter variant)

**Variants by Forest Type**:
- Deciduous: Colorful fallen leaves, rich brown
- Coniferous: Pine needles, reddish-brown, aromatic
- Tropical: Thick green undergrowth, wet appearance, vibrant

### Dirt/Bare Soil Cover

**Physical Description**:
- Exposed soil, minimal vegetation
- Clay, loam, or sandy soil
- Compacted or loose

**Visual Appearance** [OPEN QUESTION - palette needed]:
- Color: Brown range (tan to dark brown)
- Texture: Earthy, bare, mineral
- Decorations: Small rocks, rare hardy weeds
- Variation: Soil composition (clay/loam/sand), moisture, compaction

**Gameplay Properties**:
- Movement: Normal speed (1.0x)
- Traversability: Fully passable
- Building: Good

**Used By Biomes**:
- Sparse vegetation areas
- Eroded regions
- Badlands
- Transitional zones

**Context-Dependent Appearance**:
- Path dirt: Compacted, lighter color, worn
- Farmland dirt (tilled): Rich dark brown, furrows
- Barren dirt: Dusty, lifeless, cracked
- Player-created: Cleared land, construction sites

### Sand Cover

**Physical Description**:
- Fine granular material (silica, mineral grains)
- Shifting, soft surface
- Dry or wet

**Visual Appearance** [OPEN QUESTION - palette needed]:
- Color: White to tan (beach) or yellow to orange (desert)
- Texture: Fine grain, ripple patterns
- Decorations: Shells (beach), dunes (desert), sparse plants
- Variation: Grain size, wetness, ripple patterns

**Gameplay Properties**:
- Movement: Slower (0.7x, soft shifting surface)
- Traversability: Passable but difficult
- Building: Poor (requires foundations)

**Used By Biomes**:
- Desert biome (hot)
- Coastal biome (beach)
- Dune biome
- Cold desert

**Variants**:
- Beach sand: White/tan, wet at water edge, shells, smooth
- Desert sand: Yellow/orange, dry, dune formations, ripples
- Volcanic sand: Black/dark gray, coarse

### Rock/Stone Cover

**Physical Description**:
- Exposed bedrock, boulders, rocky surface
- Hard, mineral, weathered
- Little to no soil

**Visual Appearance** [OPEN QUESTION - palette needed]:
- Color: Gray to brown (depends on geology)
- Texture: Hard, angular, cracked
- Decorations: Lichen, moss, loose stones
- Variation: Rock type, weathering, crack patterns

**Gameplay Properties**:
- Movement: Normal (1.0x) on flat rock, impassable on cliffs/boulders
- Traversability: Formation-dependent
- Building: Excellent if flat, impossible on cliffs

**Used By Biomes**:
- Mountain biome
- Rocky desert
- Badlands
- Cliff faces
- Alpine (above tree line)

**Variants**:
- Flat rock: Walkable, lichen-covered
- Boulder field: Scattered large rocks, gaps
- Cliff face: Vertical, impassable
- Scree: Loose stones, unstable

### Wetland/Swamp Cover

**Physical Description**:
- Waterlogged soil, standing water, organic muck
- Saturated, soft, unstable
- Rich vegetation (reeds, moss)

**Visual Appearance** [OPEN QUESTION - palette needed]:
- Color: Dark green-brown, murky
- Texture: Wet, organic, tangled
- Decorations: Reeds, lily pads, moss, insects
- Variation: Water visibility, vegetation density

**Gameplay Properties**:
- Movement: Very slow (0.4x, trudging through water/muck)
- Traversability: Passable but difficult
- Building: Impossible (unstable ground)

**Used By Biomes**:
- Swamp biome
- Wetland biome
- River floodplains
- Marsh biome
- Bog biome

**Variants**:
- Swamp: Standing water, trees (via entities)
- Marsh: Grasses, reeds, seasonal flooding
- Bog: Peat, moss, acidic, spongy

### Water Surface

**Physical Description**:
- Liquid water, not ground per se
- Depth variable (shallow to deep)

**Visual Appearance** [OPEN QUESTION - palette needed]:
- Color: Light blue (shallow) to dark blue (deep)
- Texture: Ripples, waves, reflective
- Decorations: Foam, fish, water plants
- Variation: Clarity, depth, wave patterns

**Gameplay Properties**:
- Movement: Slow (0.5x shallow, wading) to impassable (deep)
- Traversability: Depth-dependent
- Building: Impossible (except docks at edge)

**Used By Biomes**:
- Ocean biome
- Lake biome
- River biome
- Coastal biome

**Variants**:
- Shallow water: Light blue, bottom visible, wading
- Deep water: Dark blue, opaque, impassable
- Murky water: Green-brown (swamp), low visibility
- Clear water: Turquoise (tropical), high visibility

## Ground Cover Blending

### How Biome Percentages Affect Ground

**Single Dominant Biome** (>90% influence):
```
Tile: { meadow: 95%, forest: 5% }
Ground: Almost pure grass cover
Visual: Meadow grass appearance, maybe slight color shift toward forest
```

**Balanced Blend** (60/40 or similar):
```
Tile: { meadow: 60%, borealForest: 40% }
Ground: Mixed grass + forest floor
Visual: Grass with patches of pine needles, moss
Color: Green-brown blend
```

**Transition Zone** (80/20):
```
Tile: { meadow: 80%, borealForest: 20% }
Ground: Primarily grass, hints of forest floor
Visual: Grass dominant, occasional pine needle patches
Natural ecotone appearance
```

### Visual Blending Strategies

**Color Interpolation**:
- Blend ground cover colors by biome percentages
- Meadow green (80%) + forest brown (20%) = slightly brownish green

**Texture Mixing**:
- Procedural noise determines which cover appears where
- 80% meadow = 80% of tile surface shows grass texture
- 20% forest = 20% shows forest floor texture
- Organic mixing pattern, not uniform

**Decoration Weighting**:
- Meadow decorations (flowers): 80% spawn probability
- Forest decorations (mushrooms): 20% spawn probability
- Mixed ecosystem appearance

## Biome Ground Cover Assignments

### Temperate Meadow
- Primary: Grass cover (100%)

### Boreal Forest
- Primary: Forest floor cover (pine needles, 80%)
- Secondary: Grass cover (clearings, 20%)

### Temperate Deciduous Forest
- Primary: Forest floor cover (leaf litter, 70%)
- Secondary: Grass cover (clearings, 30%)

### Tropical Rainforest
- Primary: Forest floor cover (thick litter, 90%)
- Secondary: Wetland cover (very humid, 10%)

### Desert (Hot)
- Primary: Sand cover (90%)
- Secondary: Rock cover (10%)

### Tundra
- Primary: Grass cover (short/brown, 60%)
- Secondary: Rock cover (40%)
- Note: Snow overlay in winter (separate seasonal system)

### Savanna
- Primary: Grass cover (dry grass, 80%)
- Secondary: Dirt cover (bare patches, 20%)

### Alpine/Mountain
- Primary: Rock cover (70%)
- Secondary: Grass cover (alpine meadows, 30%)
- Note: Snow overlay at high elevation (separate seasonal system)

### Coastal/Beach
- Primary: Sand cover (70%)
- Secondary: Grass cover (dunes, 20%)
- Tertiary: Rock cover (rocky shores, 10%)

### Swamp/Marsh
- Primary: Wetland cover (80%)
- Secondary: Water surface (shallow, 20%)

### Badlands
- Primary: Dirt cover (exposed soil, 60%)
- Secondary: Rock cover (eroded, 40%)

## Gameplay Properties by Ground Cover

### Movement Speed Modifiers

- **Grass**: 1.0x (normal)
- **Forest Floor**: 0.9x (slightly soft)
- **Dirt**: 1.0x (normal)
- **Sand**: 0.7x (slow, shifting)
- **Rock**: 1.0x (if flat), 0.0x (impassable if cliff)
- **Wetland**: 0.4x (very slow, trudging)
- **Shallow Water**: 0.5x (wading)
- **Deep Water**: 0.0x (impassable)

**Blended Tiles**: Interpolate modifiers
- 80% grass (1.0x) + 20% wetland (0.4x) = 0.88x speed

### Building Suitability

- **Grass**: Excellent
- **Forest Floor**: Good (clear trees first)
- **Dirt**: Good
- **Sand**: Poor (requires foundations)
- **Rock**: Excellent (if flat), impossible (if cliff)
- **Wetland**: Impossible (too soft)
- **Water**: Impossible (except docks at edge)

**Blended Tiles**: Require dominant buildable cover
- 70% grass + 30% wetland = Can build (grass dominant)
- 40% grass + 60% wetland = Cannot build (wetland dominant)

### Resource Availability

Determined by biome, not ground cover directly, but influenced:
- **Grass cover** → Foraging, grazing, farmable
- **Forest floor** → Mushrooms, wood nearby (from tree entities)
- **Sand** → Minimal resources, glass-making
- **Rock** → Mining (stone, ore, gems)
- **Wetland** → Unique plants, fishing, peat
- **Water** → Fishing

## Seasonal Overlay System

**Note**: This section is informational - detailed design in separate document.

### Snow Coverage (Winter)

**Not a ground cover** - overlays existing covers:
```
Ground: Grass cover (permanent)
Snow overlay: 80% coverage (seasonal)
Visual: Mostly white with grass patches showing through
Spring: Snow melts, grass visible again
```

**Coverage Percentage**:
- 0%: Bare ground visible
- 50%: Patchy snow, ground showing through
- 100%: Fully covered, white blanket

**Varies By**:
- Season (winter = high, summer = 0%)
- Elevation (higher = more snow)
- Climate zone (arctic = permanent, temperate = seasonal)

### Other Seasonal Effects

**Frost** (autumn/winter):
- Grass cover appears frost-tipped (white edges)
- Ground slightly lighter color
- Temporary morning effect

**Wetness** (rain/spring):
- Ground covers appear darker when wet
- Puddles on dirt/rock
- Temporary visual effect

## Open Questions

### Visual Palette Decisions

**Ground Cover Colors**:
- Specific color values for each cover type?
- How much variation within each type?
- Biome-specific color shifts (tropical grass vs temperate grass)?

**Blending Appearance**:
- How should 60/40 blends look visually?
- Sharp transitions or smooth gradients between covers?
- Procedural noise scale for mixing textures?

### Ground Cover Variants

**Sub-Types Needed?**:
- Multiple grass types (lush, dry, short, tall)?
- Or handle through procedural variation + biome context?
- Forest floor: deciduous vs coniferous as distinct types or variants?

### Seasonal Dynamics

**Dynamic Appearance**:
- Does grass turn brown in summer drought?
- Wetland flooding/drying with seasons?
- Dirt moisture appearance changes?

### Blending Complexity

**Rendering Performance**:
- How many biome influences per tile (max 2? 3? 5?)?
- Performance cost of blending multiple covers?
- Simplification for distant tiles (LOD)?

**Visual Quality**:
- Transition zone width (hundreds of tiles)?
- How smooth should percentage gradients be?
- Edge irregularity in blends?

## Related Documentation

**3D World Generation** (data source):
- [World Generation Overview](../world-generation/README.md) - Complete 3D system
- [Biome Types](../world-generation/biomes.md) - Biome catalog and which ground covers they use
- [Data Model](../world-generation/data-model.md) - How 2D samples biome data from 3D world

**2D Game View** (this folder):
- [Game View Overview](./README.md) - How 2D rendering works
- [biome-influence-system.md](./biome-influence-system.md) - How tiles get biome percentages
- [tile-transitions.md](./tile-transitions.md) - How ground covers transition visually
- [procedural-variation.md](./procedural-variation.md) - Ground cover variation

**Visual Style**:
- [visual-style.md](../../visual-style.md) - Overall visual aesthetic

**Future Documentation**:
- [seasonal-systems.md](./seasonal-systems.md) - Snow and seasonal overlays (planned)

**Technical** (future):
- Tile rendering implementation
- Ground cover blending techniques
- Texture and decoration systems

## Revision History

- 2025-10-26: Moved to game-view folder, added 3D world context, fixed outdated references
- 2025-10-26: Removed snow as ground cover type, now treated as seasonal overlay
- 2025-10-26: Complete rewrite from tile-types to ground covers with biome percentage blend system
