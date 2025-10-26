# Visual Art Direction

Created: 2025-10-26
Status: Design / Open Questions

## Overview

This document establishes the visual aesthetic direction for World-Sim. All technical rendering decisions flow from these artistic goals. The visual style should create a world that feels **hand-crafted, unique, and alive** while remaining **recognizable and intuitive** to players.

## Overall Visual Philosophy

### Core Visual Goals

**Hand-Crafted Feel**
- World should look thoughtfully designed, not obviously procedural
- No two regions should look identical
- Natural variation that feels intentional, not random
- Every coastline, forest edge, and terrain transition unique

**Recognizable and Intuitive**
- Player must instantly recognize terrain types
- Gameplay-relevant information must be visually clear
- Visual beauty never compromises gameplay clarity
- Water looks like water, grass looks like grass

**Organic and Natural**
- Terrain transitions feel natural, not grid-locked
- Edges are irregular (coastlines, forest borders)
- Variation mimics nature (no perfect symmetry)
- Living, breathing world aesthetic

**Unique at Scale**
- Thousands of tiles, each visually distinct
- Infinite exploration without repetition
- Screenshots feel unique, not generic
- Players share discoveries of beautiful locations

### Why Procedural Generation?

**Art Production Efficiency**
- Goal: Hand-crafted aesthetic without manually painting thousands of tiles
- Leverage code to create variety and uniqueness
- NOT about reducing asset count for performance
- NOT about parametric design minimalism
- IS about achieving artisanal quality through algorithmic artistry

**Creative Vision**
- Artists define rules, code executes at scale
- Define "what makes a beautiful coastline," code generates infinite variations
- Define terrain palettes, code creates seamless biomes
- Define decoration placement rules, code populates worlds

## Art Style Direction

### Style Keywords

**Visual Character** [OPEN QUESTION - needs visual exploration]:
- Painterly vs Geometric
- Realistic vs Stylized
- Clean vs Textured
- Bright vs Muted
- Soft vs Sharp

**Reference Games** [To be documented]:
- Games with similar top-down visual goals
- Specific elements we want to emulate
- Terrain rendering we admire

**NOT Our Style**:
- Perfect grids (avoid obvious tile boundaries)
- Pixel art (using vector graphics instead)
- Photorealistic (stylistic choice)
- Heavily stylized/cartoony (unless decided otherwise)

## Color Palette Philosophy

### Global Palette Approach

**Color Harmony**
- Cohesive palette across all terrain types
- Biomes can shift hue/saturation within bounds
- Seasonal variation possible (future)
- Consistent mood and atmosphere

**Contrast & Recognition**
- Sufficient contrast between terrain types
- Water clearly different from land at a glance
- Grass/dirt/sand distinguishable instantly
- No ambiguity in terrain identification

**Mood & Atmosphere** [OPEN QUESTION]:
- Vibrant and cheerful?
- Muted and realistic?
- Warm and inviting?
- Cool and mysterious?
- Lighting: always bright daylight, or time-of-day variation?
- Weather impact on colors (rain = darker, snow = desaturated)?

### Terrain Type Palettes

**Water**
- Color range: [OPEN QUESTION: Deep blue to turquoise? Murky green-brown?]
- Shallow vs deep: Lighter shallows, darker depths
- Variation: Wave patterns, foam, reflections, sparkle

**Grass**
- Color range: [OPEN QUESTION: Yellow-green to blue-green? Single green tone?]
- Seasonal: Spring (bright green), summer (yellower), autumn (brown-green)
- Variation: Per-tile color shifts, biome-based hue changes
- Lushness: Healthy saturated vs dry desaturated

**Sand**
- Color range: [OPEN QUESTION: White beach sand? Yellow desert sand? Both?]
- Context: Beach sand (light, wet) vs desert sand (warm, dry)
- Variation: Grain patterns, color speckling, ripples
- Wetness: Darker when adjacent to water

**Stone/Rock**
- Color range: [OPEN QUESTION: Gray granite? Brown sandstone? Varied by biome?]
- Variation: Cracks, lichen, moss, weathering
- Context: Mountain stone vs scattered boulders vs rocky shore
- Age: Fresh stone vs weathered vs ancient

**Dirt**
- Color range: [OPEN QUESTION: Rich brown? Reddish clay? Dark loam?]
- Variation: Moisture levels, organic content, clay vs soil
- Context: Farmland (rich) vs bare earth vs paths (packed, lighter)
- State: Wet (dark) vs dry (lighter)

## Tile Visual Goals

### Appearance at Different Zoom Levels

**Zoomed In (1-2 tiles visible)**
- Rich detail: textures, decorations, subtle variations
- Individual grass tufts, pebbles, cracks visible
- SVG decorations prominent and clear
- Procedural noise patterns create texture
- Hand-crafted feel most important

**Medium Zoom (10-20 tiles visible)**
- Terrain types clearly distinguishable
- Transitions and edges are visual focal points
- Decorations add visual interest and life
- Overall terrain "feel" and character readable
- Composition of terrain types creates landscapes

**Zoomed Out (50+ tiles visible)**
- Terrain as color masses and biomes
- Coastlines and biome boundaries define composition
- Individual tile detail less critical
- Overall world geography visible
- Beauty in macro patterns

### Visual Complexity & Detail

**Detail Density** [OPEN QUESTION]:
- How "busy" should tiles be?
- Smooth and clean vs detailed and textured
- Balance: visually interesting without overwhelming
- Varies by terrain type (water simpler, grass richer)

**Decoration Density** [OPEN QUESTION]:
- How many flowers/pebbles per grass tile?
- Sparse (occasional elegant accents) vs dense (lush abundance)
- Varies by terrain type and biome
- Clustered naturally vs scattered uniformly

**Texture Complexity** [OPEN QUESTION]:
- Subtle color variation vs strong visible patterns
- Grass: uniform green vs visible blade directionality
- Stone: smooth vs heavily cracked
- Water: flat color vs ripple/wave patterns
- Sand: smooth vs grain texture

## Edge and Transition Visual Goals

### Coastlines (Water/Land)

**Visual Goals**:
- Irregular, natural shorelines - no straight lines
- Each segment of coast visually unique (procedural variation)
- Organic curves, natural-looking randomness
- Beach transitions: sand layer between water and grass
- Foam/wave patterns at water's edge (accent detail)
- Beautiful enough to be landmarks ("that peninsula," "that cove")

**Visual References** [To be collected]:
- Real coastlines from aerial/satellite view
- Game examples with beautiful shorelines
- How irregular? Gentle curves vs jagged complexity?

**Player Recognition**:
- Clear visual boundary: this is water, this is land
- No ambiguity about traversability
- Beauty doesn't obscure function

### Forest Edges (Grass/Trees)

**Visual Goals**:
- Trees cluster naturally, organic placement
- Gradual density transition (sparse → medium → dense)
- Undergrowth visible at forest edge
- Irregular boundary, some trees isolated in grass
- Inviting, not intimidating (unless dark forest)

**Visual References** [To be collected]:
- Forest edges from aerial view
- How do natural forests meet grasslands?

### Rocky Terrain (Grass/Stone)

**Visual Goals**:
- Scattered rocks increasing in density toward stone areas
- Not a hard line - gradual natural transition
- Rock clusters, not uniform distribution
- Grass tufts persist among rocks
- Feels like natural erosion/weathering

### Cliff Edges (Elevation Change)

**Visual Goals**:
- Sharp, dramatic visual boundary
- Clear "you cannot pass" communication
- Dangerous, impressive appearance
- Irregular edge (natural rock formation)
- Shadow/lighting to show depth (future enhancement)

## World Cohesion

### Visual Continuity

**Seamless Chunks**
- Tile boundaries invisible to player
- Noise and patterns flow across chunk borders
- Decorations don't cluster or gap at edges
- World feels like one continuous surface

**Biome Transitions**
- Gradual palette shifts between biomes
- Temperate → desert: green fades to yellow-brown over distance
- Forest → plains: tree density decreases naturally
- Snow line: altitude-based gradual appearance
- No hard biome boundaries (unless dramatic like cliff)

**Terrain Consistency**
- Same terrain type recognizable everywhere
- Grass in desert (dry, yellow) vs grass in temperate (lush, green)
- Water in swamp (murky) vs water in ocean (clear blue)
- Variation within recognizable bounds

### Variety vs Repetition

**Healthy Variety**
- No "I've seen this exact tile 100 times" feeling
- Every coastline interesting and unique
- Decorations varied (multiple flower types, rock sizes, colors)
- Exploration reveals new beautiful compositions
- Players want to share screenshots

**Acceptable Repetition**
- Open plains can be visually similar (intentional calm emptiness)
- Consistent terrain recognition (all grass fundamentally similar)
- Predictable visual language (water = blue, grass = green range)
- Decorative elements can repeat (same flower species)

**Avoiding Visual Fatigue**
- Long-distance travel should reveal new sights
- Biomes provide macro-scale variation
- Procedural variation provides micro-scale uniqueness
- Occasional landmarks and special features
- Terrain composition creates memorable landscapes

## Open Questions

### Critical Visual Decisions Requiring Exploration

**1. Overall Art Style**
- Which visual style best serves game goals?
- Painterly vs geometric vs hybrid approach?
- Color palette: vibrant vs muted vs realistic?
- Reference games to emulate?

**2. Edge Visual Treatment**
- How irregular should coastlines be?
- Geometric crisp edges vs soft alpha blended?
- Different edge styles for different transitions?
- Visual examples needed for comparison

**3. Detail and Decoration Density**
- How many decorations per tile?
- How much color variation per tile?
- Texture complexity level?
- Balance point: interesting vs overwhelming

**4. Terrain Palettes**
- Specific color values for each terrain type
- Variation ranges (how much lighter/darker)
- Biome palette shifts
- Seasonal color changes

**5. Noise and Pattern Scales**
- How large/small should procedural patterns be?
- Visual scale of variation (subtle vs obvious)
- Relationship to tile size
- Zoom level considerations

## Related Documentation

**Game Design**:
- [terrain-types.md](./terrain-types.md) - Catalog of terrain types with visual descriptions
- [terrain-transitions.md](./terrain-transitions.md) - How different terrains visually blend
- [procedural-variation.md](./procedural-variation.md) - Philosophy of variation and uniqueness
- [features/tiles/svg-decorations.md](./features/tiles/svg-decorations.md) - SVG decoration visual role

**Technical Implementation**:
- [/docs/technical/tiles/architecture.md](../technical/tiles/architecture.md) - How to implement visual goals
- [/docs/technical/tiles/edge-blending-options.md](../technical/tiles/edge-blending-options.md) - Technical approaches to visual transitions

## Revision History

- 2025-10-26: Initial visual direction document establishing aesthetic goals and open questions
