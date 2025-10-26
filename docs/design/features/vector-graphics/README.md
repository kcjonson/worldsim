# Vector Graphics (SVG) Assets - Overview

Created: 2025-10-24
Updated: 2025-10-26
Status: Design

## Overview

SVG (Scalable Vector Graphics) assets are used throughout World-Sim in multiple distinct ways. This document provides an overview of all SVG use cases and directs to specific documentation for each.

**Why SVGs?**:
- Scalable: Crisp visuals at any zoom level
- Flexible: Can be animated, recolored, transformed
- Hand-Crafted Quality: Artists create unique assets
- Performance: Efficient when tessellated and batched

## SVG Use Cases

World-Sim uses SVG assets in three primary ways:

### 1. Decorations and Entities (Placed Objects)

**What**: SVG assets placed as discrete objects in the world

**Examples**:
- Small decorations: Flowers, pebbles, grass tufts, mushrooms
- Large entities: Trees, bushes, boulders
- Interactive objects: Harvestable resources
- Animated elements: Swaying trees, grass blades

**Characteristics**:
- Each SVG has position, rotation, scale
- Placed on top of procedural tile ground covers
- Can be animated (spline deformation)
- Biome-appropriate placement (meadow flowers, desert cacti)

**See**: [svg-decorations.md](./svg-decorations.md) - Complete documentation

### 2. Texture Patterns (Fills for Code-Drawn Shapes)

**What**: SVG patterns used as repeating fills for procedurally-drawn polygons

**Examples**:
- Building materials: Brick, wood planks, concrete, thatch
- Terrain features: Cobblestone paths, rock formations
- UI backgrounds: Parchment, wood grain, metal

**Characteristics**:
- Seamlessly tiling patterns
- Fill any shape/size polygon
- Hand-crafted texture quality
- Flexible (same pattern for any foundation size)

**See**: [svg-texture-patterns.md](./svg-texture-patterns.md) - Complete documentation

### 3. Animated Vegetation and Environmental Effects

**What**: Dynamic SVG assets with real-time spline-based animation

**Examples**:
- Grass blades swaying in wind
- Trees bending and flexing
- Vegetation responding to player movement (trampling)
- Environmental effects (ripples, particles)

**Characteristics**:
- Real-time deformation (Bezier curve manipulation)
- Physics-influenced (wind, player interaction)
- Performance-critical (thousands animating simultaneously)

**See**: [animated-vegetation.md](./animated-vegetation.md) - Grass and tree animation design

## Integration with Procedural Tile System

### Rendering Layers

**Complete Visual Stack** (bottom to top):
1. **Procedural Ground Covers** (grass, sand, rock, etc.) - Code-generated, biome-blended
2. **SVG Texture Pattern Fills** (building foundations, paths) - Code-drawn shapes with SVG fills
3. **Ground SVG Decorations** (flowers, pebbles) - Placed flat on ground
4. **Standing SVG Entities** (trees, structures) - Vertical objects
5. **Animated SVG Elements** (swaying grass, trees) - Dynamic layer

**Result**: Rich, layered world combining procedural efficiency with hand-crafted quality

### Biome-Aware SVG Placement

**Decorations and Entities Follow Biome Percentages**:
```
Tile with { meadow: 70%, forest: 30% }:
- Meadow decorations (flowers): 70% spawn probability
- Forest decorations (mushrooms): 30% spawn probability
- Mixed ecosystem appearance
```

**Natural Ecotones**: Gradual species mixing matches biome transitions

### Performance Architecture

**From Vector Graphics System** [see technical docs]:
- **Tier 1**: Pre-rasterized SVG patterns (texture atlas)
- **Tier 2**: Semi-static SVG entities (cached meshes) - buildings, rocks
- **Tier 3**: Dynamic SVG entities (real-time tessellation) - animated grass, trees

**Target**: 10,000+ visible SVG entities @ 60 FPS

## Artist Workflow (High-Level)

### Creating SVG Assets

**Tools**: Adobe Illustrator, Inkscape, Figma, etc.

**General Guidelines**:
- Keep shapes simple (fewer points = better performance)
- Use solid colors or simple gradients
- Design at intended in-game scale
- Test patterns tile seamlessly (for texture patterns)

**Asset Organization**:
```
/assets/
├── decorations/          → Small placed objects
│   ├── flowers/
│   ├── rocks/
│   └── vegetation/
├── entities/             → Large placed objects
│   ├── trees/
│   ├── bushes/
│   └── structures/
├── patterns/             → Texture fills
│   ├── building_materials/
│   ├── terrain/
│   └── ui/
└── metadata/
    └── asset_config.json
```

**Detailed Workflow**: See individual documents for asset-specific guidelines

### SVG Requirements by Use Case

**Decorations/Entities**:
- Arbitrary shapes OK
- Optimize point count
- Consider animation metadata if needed

**Texture Patterns**:
- MUST tile seamlessly (edges match)
- Square or rectangular pattern unit
- Consistent scale across all patterns

**Animated Assets**:
- Define pivot points (animation metadata)
- Consider deformation behavior
- Simpler shapes for performance

## Visual Style Consistency

**All SVG Assets Must**:
- Match overall game visual style [see: visual-style.md](../../visual-style.md)
- Use consistent line weights and detail levels
- Coordinate with biome color palettes
- Feel hand-crafted, not generic

**Cohesion Across Use Cases**:
- Flower decorations + brick patterns + tree entities = unified aesthetic
- Color palettes harmonize
- Detail level consistent

## Document Index

### Game Design Documentation

**This Folder** (`/docs/design/features/vector-graphics/`):
- **[README.md](./README.md)** (this document) - Overview of all SVG uses
- **[svg-decorations.md](./svg-decorations.md)** - SVG as placed objects (decorations, entities)
- **[svg-texture-patterns.md](./svg-texture-patterns.md)** - SVG as fills for code-drawn shapes
- **[animated-vegetation.md](./animated-vegetation.md)** - Animated grass, trees, environmental interactions
- **[environmental-interactions.md](./environmental-interactions.md)** - Player interaction with SVG vegetation

### Related Game Design

**2D Game View & Tile Rendering** (`/docs/design/features/game-view/`):
- [visual-style.md](../../visual-style.md) - Overall aesthetic direction
- [Game View Overview](../game-view/README.md) - How 2D rendering works
- [biome-ground-covers.md](../game-view/biome-ground-covers.md) - Ground layers SVGs sit on
- [biome-influence-system.md](../game-view/biome-influence-system.md) - SVG placement probability system
- [tile-transitions.md](../game-view/tile-transitions.md) - Visual blending (ground layer)
- [procedural-variation.md](../game-view/procedural-variation.md) - SVG placement variation

### Technical Documentation

**Vector Graphics System** (`/docs/technical/vector-graphics/`):
- [INDEX.md](../../../technical/vector-graphics/INDEX.md) - Technical architecture overview
- [architecture.md](../../../technical/vector-graphics/architecture.md) - Four-tier rendering system
- [svg-parsing-options.md](../../../technical/vector-graphics/svg-parsing-options.md) - SVG parser analysis
- [asset-pipeline.md](../../../technical/vector-graphics/asset-pipeline.md) - Asset import and processing
- [animation-system.md](../../../technical/vector-graphics/animation-system.md) - Spline deformation implementation

## Open Questions

### Cross-Document Questions

**1. SVG vs Procedural Balance**:
- What percentage of visual world is SVG vs procedural?
- Where is the dividing line?
- Performance implications?

**2. Asset Creation Workload**:
- How many SVG assets needed total?
- Decorations: 50+? 100+?
- Patterns: 20+? 50+?
- Entities: 100+? 500+?

**3. Style Convergence**:
- Can artists create cohesive library across all use cases?
- Tools/processes to ensure consistency?
- Style guide documentation needed?

**4. Performance Validation**:
- Can we actually render 10k SVG entities @ 60 FPS?
- Needs prototyping
- May need to reduce decoration density

## Next Steps

**For Further Documentation**:
1. Read specific use case documents for details
2. Review technical docs for implementation approach
3. Prototype SVG rendering in ui-sandbox
4. Create style guide for SVG asset creation

**For Development**:
1. Choose SVG parser (technical decision)
2. Implement basic SVG → tessellation pipeline
3. Build pattern fill system for buildings
4. Test decoration placement with biome system

## Revision History

- 2025-10-26: Complete rewrite as index/overview document for multiple SVG use cases
- 2025-10-24: Initial artist workflow document
