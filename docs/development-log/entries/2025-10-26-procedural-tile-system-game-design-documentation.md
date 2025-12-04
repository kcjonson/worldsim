# Procedural Tile System Game Design Documentation

**Date:** 2025-10-26

**Major Architectural Clarification:**

Comprehensive game design documentation for the procedural tile rendering system. **Critical discovery**: Tiles are procedurally generated in code (not SVG-based), with SVG assets serving as decorations and entities placed on top of the procedural ground.

**Core Innovation - Biome Influence Percentage System:**

Tiles don't have a single "type" but instead have percentage influences from multiple biomes:
```
Example: { meadow: 80%, forest: 20% }
- Ground: 80% grass appearance, 20% forest floor appearance
- Entities: 80% wildflower spawn probability, 20% pine tree spawn probability
- Movement speed: Interpolated between meadow and forest values
```

This creates **natural ecotones** (transition zones) hundreds of tiles deep between biomes, with gradual species mixing and visual blending. Every forest-meadow edge looks different, every coastline is unique.

**Game Design Documentation Created** (7 documents):

**Core Tile System** (`/docs/design/`):
1. `visual-style.md` - Overall aesthetic direction: hand-crafted feel, recognizable biomes, organic appearance
2. `biome-ground-covers.md` - Seven ground cover types (grass, forest floor, dirt, sand, rock, wetland, water)
3. `biome-influence-system.md` - Percentage-based biome blending, natural ecotone creation
4. `tile-transitions.md` - Visual appearance of biome transition zones (complete rewrite for percentage system)
5. `procedural-variation.md` - Creating unique tiles while maintaining biome recognizability

**SVG Asset Organization** (`/docs/design/features/vector-graphics/`):
6. `svg-decorations.md` - SVG assets as placed objects (flowers, trees, boulders)
7. `svg-texture-patterns.md` - SVG patterns as fills for code-drawn shapes (brick, wood, concrete)

**Documentation Index Updates**:
- Updated `/docs/design/INDEX.md` with "Visual Design & Procedural Tile System" section
- Updated vector graphics section with organized SVG documentation links

**Three Distinct SVG Use Cases Identified:**

1. **Decorations/Entities**: Placed objects on top of procedural tiles
   - Ground decorations: flowers, pebbles, grass tufts (5-10 per tile)
   - Standing entities: trees, bushes, boulders (0-3 per tile)
   - Placement follows biome influence percentages
   - Biome-appropriate spawning (meadow flowers only in meadow-influenced tiles)

2. **Texture Patterns**: Seamlessly tiling fills for code-drawn shapes
   - Building materials: brick, wood planks, concrete, thatch
   - Terrain features: cobblestone paths, rock formations
   - Any shape/size polygon filled with hand-crafted texture
   - Pattern must tile perfectly (edges match)

3. **Animated Vegetation**: Spline-based deformation (documented previously in animated-vegetation.md)
   - Grass swaying, tree movement, player interaction
   - Real-time Bezier curve manipulation

**Key Terminology Clarifications:**

- **Ground Covers**: Physical surface types (grass, sand, rock) - permanent, code-generated
- **Biomes**: Ecological zones that determine which ground covers appear and which decorations spawn
- **Tiles**: Individual game world cells with multiple biome influence percentages
- **Ecotones**: Natural transition zones between biomes (emergent from percentage system)
- **Decorations**: Small SVG objects (flowers, pebbles)
- **Entities**: Larger SVG objects (trees, boulders, creatures)
- **Texture Patterns**: Seamlessly tiling SVG patterns for filling shapes

**Visual Rendering Layers** (bottom to top):
1. Procedural ground covers (code-generated, biome-blended)
2. SVG texture pattern fills (code-drawn shapes with SVG fills)
3. Ground SVG decorations (flowers, pebbles)
4. Standing SVG entities (trees, structures)
5. Animated SVG elements (swaying grass, trees)

**Deterministic Variation System:**

- Each tile gets deterministic seed from world position (x, y)
- Same position always generates same appearance
- Color variation within biome palette (±10-20% lightness/saturation)
- Entity placement varies (rotation, scale, position)
- Balance: recognizable biomes with endless visual variety
- Multiplayer compatible (all clients see same world)

**Design Philosophy Established:**

- **Hand-crafted illusion**: Player should think "someone carefully designed this", not "obviously procedural"
- **Recognizable not readable**: Biomes must be instantly identifiable by players
- **Variation within coherence**: Every tile unique, but biome identity maintained
- **Performance in technical docs**: Game design docs focus on player experience, not implementation
- **Separation of concerns**: Design docs describe "what/why", technical docs describe "how"

**Iterative Refinement Process:**

Multiple rounds of user feedback refined the documentation:
- Changed "readable" → "recognizable" (it's a game, not text)
- Removed performance concerns from design docs
- Removed prototyping plans from design docs
- Changed "terrain types" → "tile types" → "ground covers" (terminology evolution)
- Removed snow as ground cover (now seasonal overlay system)
- Removed code examples from design docs (belongs in technical docs)
- Split SVG documentation by use case (decorations vs patterns vs animation)

**Open Questions Documented** (for future technical design):

1. Color variation ranges (how much can grass green vary?)
2. Entity density variation (fixed or varied per tile?)
3. Scale variation ranges (trees 80%-120% of base?)
4. Cross-tile decoration continuity (coordinate or independent?)
5. Regional vs per-tile variation (macro trends or purely local?)
6. Pattern library size (how many texture patterns needed?)
7. LOD transition smoothness (fade decorations gracefully?)

**Next Steps:**

1. Create technical design documents for tile system implementation
2. Define edge blending algorithms (geometric, alpha, marching squares)
3. Specify procedural variation implementation (noise functions, seed-based generation)
4. Document terrain layer composition system
5. Specify SVG-tile integration architecture
6. Prototype procedural tile rendering in ui-sandbox


