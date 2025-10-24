# Vector-Based Asset System

Status: Planning
Implementation: [Vector Asset Pipeline Tech Doc](/docs/technical/vector-asset-pipeline.md)

## Overview

All game assets are vector-based, stored as SVG files. Tiles are dynamically rendered with procedural variations and visual interactions with neighboring tiles.

## Requirements

### Must Have

#### SVG Asset Format
- All visual assets stored as SVG files on disk
- Standard SVG 1.1 specification
- Editable in standard tools (Inkscape, Adobe Illustrator, Figma)
- Organized by asset type and theme

#### Dynamic Tile Rendering
- Tiles rendered from vector data at runtime
- Support for multiple zoom levels (LOD)
- Smooth scaling without pixelation
- Performance target: 60 FPS with thousands of visible tiles

#### Procedural Variation
- Each tile instance can have unique visual variation
- Variations include:
  - Color shifts (hue, saturation, brightness)
  - Scale variations (±10%)
  - Rotation variations
  - Pattern/detail variations
- Variations are deterministic (seeded by tile position)
- No two adjacent tiles should look identical

#### Inter-Tile Visual Interactions
- Tiles visually blend with neighbors
- Examples:
  - Grass extends into dirt edges
  - Water fades into shore
  - Forest density transitions smoothly
- Blending is automatic based on tile types
- Transitions appear natural and organic

#### Memory Efficiency
- Support for millions of tile instances
- Intelligent caching of rasterized tiles
- Memory budget configurable (target: 256 MB for tile graphics)
- Automatic eviction of unused rasterized data
- Vector data always retained (small footprint)

### Should Have

#### Multiple Asset Styles
- Different visual themes (e.g., seasons, biomes)
- Easy switching between themes
- Theme-specific variations

#### Artist Tools Integration
- Hot-reload SVG files during development
- Preview tool for variations
- Debug view showing tile boundaries and blending

#### Performance Metrics
- Display cache hit rate
- Show memory usage
- Identify performance bottlenecks
- Profile rasterization time

### Nice to Have

#### Advanced Variations
- Weather effects (rain, snow)
- Time-of-day lighting
- Damage/wear over time
- Seasonal variations

#### SVG Animation
- Animated elements (waving grass, flowing water)
- Procedural animation parameters

#### Custom Blending Rules
- Artist-defined blending behavior
- Per-tile-type transition specifications

## User Stories

### As a Player
- I want tiles to look unique and varied so the world feels organic
- I want smooth visuals when zooming in/out so the experience feels polished
- I want tiles to blend naturally so there are no harsh seams
- I want good performance even with large visible areas

### As an Artist
- I want to create assets in standard SVG tools so I can use my existing workflow
- I want to see my changes immediately so I can iterate quickly
- I want to define variation parameters so tiles have controlled randomness
- I want to specify how tiles blend so transitions look intentional

### As a Developer
- I want vector assets so we can scale to any resolution
- I want small asset files so the game downloads quickly
- I want procedural variation so we don't need thousands of asset files
- I want performant rendering so the game runs well on all hardware

## Technical Considerations

### Asset Structure
```
assets/tiles/
├── terrain/
│   ├── grass.svg
│   ├── dirt.svg
│   ├── stone.svg
│   └── water.svg
├── vegetation/
│   ├── tree_oak.svg
│   ├── bush.svg
│   └── flowers.svg
└── structures/
    ├── road.svg
    └── bridge.svg
```

### SVG Guidelines
- Keep path count reasonable (< 100 paths per tile)
- Use simple shapes when possible
- Avoid complex filters/effects
- Use layers for variation elements
- Include metadata for blending hints

### Variation Metadata
```xml
<svg xmlns="http://www.w3.org/2000/svg">
  <metadata>
    <tile-variation>
      <color-shift min="-0.1" max="0.1"/>
      <scale min="0.9" max="1.1"/>
      <rotation values="0,90,180,270"/>
    </tile-variation>
  </metadata>
  <!-- SVG content -->
</svg>
```

### Performance Targets
- Initial rasterization: < 5ms per tile
- Cache lookup: < 0.1ms
- Memory per tile (vector): < 50 bytes
- Memory per tile (cached raster): ~16 KB (64×64 RGBA)
- Simultaneous visible tiles: 1000+ at 60 FPS

## Open Questions

1. **SVG Parser**: NanoSVG vs LunaSVG vs custom?
   - Need to balance features vs complexity

2. **Rasterization**: CPU vs GPU?
   - CPU: More compatible, easier
   - GPU: Faster, but more complex

3. **Variation Complexity**: How complex can variations be?
   - Simple: Color/scale/rotation
   - Complex: Modify paths, add/remove elements

4. **Blending**: Edge-based vs overlap-based?
   - Edge: Modify tile edges to match neighbors
   - Overlap: Draw blending layer on top

5. **Cache Strategy**: Pre-rasterize vs on-demand?
   - Hybrid approach likely best, but what ratio?

6. **LOD Levels**: How many levels of detail?
   - More levels = better performance
   - Fewer levels = simpler system

## Success Criteria

### Functional
- ✅ Display tile from SVG asset
- ✅ Apply procedural variation to tile
- ✅ Blend adjacent tiles visually
- ✅ Support 1M+ tile instances
- ✅ Zoom in/out with smooth scaling

### Performance
- ✅ 60 FPS with 1000 visible tiles
- ✅ < 256 MB memory for tile graphics
- ✅ < 100ms startup time for asset loading
- ✅ < 16ms frame time (99th percentile)

### Quality
- ✅ No visible seams between tiles
- ✅ Variations look natural, not random noise
- ✅ Scalable to any zoom level without artifacts
- ✅ Artist-approved visual quality

## Related Documentation

- Tech: [Vector Asset Pipeline](../../../technical/vector-asset-pipeline.md)
- Tech: [Renderer Architecture](../../../technical/renderer-architecture.md)
- Tech: [Resource Management](../../../technical/resource-management.md)
