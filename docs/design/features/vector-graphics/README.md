# Vector Graphics Assets - Game Design Overview

Created: 2025-10-24
Status: Design

## Overview

All visual assets in world-sim are created using vector graphics (SVG format) rather than traditional bitmap images. This design decision enables procedural variation, real-time animation, and scalable visuals at any zoom level.

## Player-Facing Benefits

**Visual Variety**:
- Every grass blade, tree, and rock looks slightly different
- No repetitive tiling patterns
- Organic, hand-crafted aesthetic

**Smooth Zooming**:
- Crisp, clear visuals at any zoom level
- No pixelation when zooming in
- Seamless transitions between zoom levels

**Dynamic Animation**:
- Grass sways in the wind
- Trees bend and flex
- Vegetation responds to player movement (trampling, pushing through)

**Responsive Environment**:
- Visual feedback when entities interact with world
- Grass bends as colonists walk through it
- Trees shake when harvested
- Water ripples when disturbed

## Asset Creation Workflow

### For Artists

**Tools**: Any SVG editor (Adobe Illustrator, Inkscape, Figma, etc.)

**Guidelines**:
1. **Keep It Simple**: Fewer points = better performance
   - Target: 10-50 points per shape
   - Avoid complex filters, effects

2. **Use Basic Shapes**: Paths, circles, rectangles
   - Convert text to paths
   - Flatten layers before export

3. **Optimize Curves**: Clean up unnecessary control points
   - Use smooth Bezier curves
   - Avoid self-intersecting paths

4. **Color Palette**: Solid colors preferred
   - Gradients supported but heavier
   - Transparency is fine

5. **Naming Convention**: Descriptive filenames
   - `grass_blade_01.svg`
   - `tree_oak_small.svg`
   - `rock_boulder_03.svg`

6. **Size Guide**: Design at intended game size
   - Grass blade: ~32px tall
   - Small tree: ~128px tall
   - Building: ~256px wide

### Asset Categories

**Static Backgrounds** (Tier 1):
- Tile textures (grass, dirt, stone, water)
- Ground decoration
- Simple, repeating patterns

**Structures** (Tier 2):
- Buildings
- Walls, fences
- Large rocks, boulders
- Non-animated trees

**Animated Entities** (Tier 3):
- Grass blades (individual)
- Swaying trees
- Flags, banners
- Water effects

### Adding Animation Data

For animated assets, add custom metadata to SVG:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 100">
    <metadata>
        <game:animation xmlns:game="http://worldsim.game">
            <type>grass_blade</type>
            <pivot x="16" y="100"/>      <!-- Bottom center -->
            <frequency>2.5</frequency>    <!-- Sway speed -->
            <amplitude>15</amplitude>     <!-- Max bend angle (degrees) -->
            <stiffness>0.8</stiffness>    <!-- How rigid -->
        </game:animation>
    </metadata>

    <!-- Your SVG paths here -->
    <path d="M 16 100 Q 16 50, 18 0" fill="#3a7" stroke="#2a5"/>
</svg>
```

**Animation Types**:
- `grass_blade`: Single-pivot bending
- `tree_sway`: Multi-point deformation (trunk + branches)
- `flag_wave`: Sine wave ripple
- `water_ripple`: Radial distortion

### Adding Collision Shapes

Define simplified collision geometry for physics:

```xml
<metadata>
    <collision>
        <shape type="aabb">
            <min x="12" y="90"/>
            <max x="20" y="100"/>
        </shape>
    </collision>
</metadata>
```

**Shape Types**:
- `aabb`: Axis-aligned bounding box (fastest)
- `circle`: Round objects
- `polygon`: Custom convex shape
- `compound`: Multiple shapes combined

## Procedural Variation

**Automatic Variation** (no artist work required):

Each instance of an asset is slightly modified:
- **Rotation**: ±5-15 degrees randomness
- **Scale**: 90-110% size variation
- **Color**: Hue shift, saturation, brightness tweaks
- **Position**: Micro-offsets to break grid alignment

**Configuration** (per asset type):

```json
{
    "assetID": "grass_blade_01",
    "variation": {
        "rotation": {"min": -10, "max": 10},
        "scale": {"min": 0.9, "max": 1.1},
        "colorHueShift": {"min": -0.05, "max": 0.05},
        "saturation": {"min": 0.9, "max": 1.1}
    }
}
```

## Performance Considerations for Artists

**Good Practices**:
- Fewer paths = better performance
- Simple shapes over complex
- Avoid gradients for high-count objects (grass)
- Use solid colors when possible

**Complexity Budget** (rough guidelines):
- Grass blade: ~10 points, 1 path
- Small tree: ~50 points, 3-5 paths
- Building: ~100 points, 5-10 paths
- Complex structure: ~200 points max

**Testing**:
- Import to game, spawn 1,000 instances
- Check frame rate (should be 60 FPS)
- If slow, simplify asset

## Integration with Game

### Asset Import Process

1. **Create SVG** in art tool
2. **Export** to `/assets/tiles/` or `/assets/entities/`
3. **Game Auto-Detects** new assets on launch
4. **First Use**: Asset is parsed, tessellated, cached
5. **Subsequent Uses**: Loaded from cache

### Asset Hot-Reloading (Development)

1. Edit SVG in external tool, save
2. Game detects file change
3. Asset reloads automatically
4. See changes immediately in-game

## Visual Style Guide

See: [UI Art Style](../../ui-art-style.md) for overall aesthetic ("High tech cowboy")

**Vector Assets Should**:
- Complement the retro-futuristic theme
- Use clean, bold lines
- Limited color palettes per asset
- Avoid photo-realistic detail (stylized instead)

**Examples**:
- Grass: Simple blade shapes, vibrant greens
- Trees: Geometric trunk + stylized foliage
- Buildings: Clean lines, visible structure, industrial feel
- UI Elements: Crisp icons, no gradients

## Related Documentation

**Technical Implementation**:
- [/docs/technical/vector-graphics/INDEX.md](../../../technical/vector-graphics/INDEX.md) - Complete technical documentation
- [/docs/technical/vector-graphics/asset-pipeline.md](../../../technical/vector-graphics/asset-pipeline.md) - Asset pipeline overview
- [/docs/technical/vector-graphics/svg-parsing-options.md](../../../technical/vector-graphics/svg-parsing-options.md) - SVG parser analysis

**Game Design**:
- [animated-vegetation.md](./animated-vegetation.md) - Grass and tree animation behavior
- [environmental-interactions.md](./environmental-interactions.md) - Player interaction with vegetation

## Asset Library Organization

```
/assets/
├── tiles/
│   ├── terrain/
│   │   ├── grass_01.svg
│   │   ├── dirt_01.svg
│   │   └── stone_01.svg
│   ├── vegetation/
│   │   ├── grass_blade_01.svg
│   │   ├── tree_oak_small.svg
│   │   └── bush_01.svg
│   └── structures/
│       ├── wall_wood_01.svg
│       ├── fence_01.svg
│       └── rock_boulder_01.svg
├── ui/
│   ├── icons/
│   └── buttons/
└── metadata/
    └── asset_config.json  (variation parameters, etc.)
```

## Future Enhancements

- **In-Game Asset Editor**: Simple SVG editing within game
- **Procedural Generation**: Generate assets from parameters
- **Asset Marketplace**: Share/download community assets
- **Animation Editor**: Visual tool for defining animation data
