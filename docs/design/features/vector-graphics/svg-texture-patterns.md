# SVG Texture Patterns

Created: 2025-10-26
Status: Design

## Overview

SVG assets can be used as **texture patterns** to fill code-drawn shapes with hand-crafted materials. Instead of placing each SVG as a discrete object, the SVG serves as a repeating tile pattern that fills polygons.

**This Document**: Covers SVG assets as **fill patterns** for procedural shapes

**See Also**: [svg-decorations.md](./svg-decorations.md) for SVG assets as placed objects

## SVG as Texture Fills

### The Concept

**Code-Drawn Shape + SVG Pattern Fill**:
- Game code draws geometric shape (rectangle, polygon, etc.)
- Shape is filled with tiling SVG pattern
- Pattern repeats seamlessly to cover any size shape

**Example - Building Foundation**:
```
Code draws: Rectangular polygon (10x15 tiles)
Fill with: "concrete_texture.svg" (seamless pattern)
Result: Foundation with realistic concrete appearance
```

**Benefits**:
- Flexible shapes (any size, any configuration)
- High-quality textures (hand-drawn SVG)
- Scalable (vector patterns tile perfectly at any zoom)
- Reusable (same pattern for all concrete foundations)

## Use Cases

### Building Structures

**Foundations**:
- Concrete foundation pattern
- Stone foundation pattern
- Wood foundation pattern (for piers)

**Walls**:
- Brick wall pattern (multiple brick types)
- Wood planks pattern (vertical, horizontal)
- Stone wall pattern (fitted stones)
- Adobe/clay wall pattern
- Thatch wall pattern

**Roofs**:
- Thatch roof pattern
- Wood shingle pattern
- Clay tile pattern
- Stone tile pattern
- Slate roof pattern

**Floors**:
- Wood plank floor
- Stone tile floor
- Dirt floor (packed earth)
- Carpet/rug patterns

### Terrain Features (Code-Drawn)

**Rock Formations**:
- Cliff face texture (layered rock)
- Boulder surface texture
- Mountain rock patterns

**Constructed Paths**:
- Cobblestone path pattern
- Brick path pattern
- Gravel path pattern
- Wooden boardwalk pattern

**Water Features** (if code-drawn):
- Water surface pattern (ripples, waves)
- Ice pattern (cracks, crystals)

### UI Elements

**In-Game UI Backgrounds**:
- Parchment texture (for scrolls, menus)
- Wood grain (for UI panels)
- Metal plate texture
- Stone tablet texture

## Pattern Requirements

### Seamless Tiling

**Must Tile Perfectly**:
- Pattern edges match (left edge = right edge, top = bottom)
- No visible seam when repeated
- Continuous appearance

**SVG Design Constraint**:
- Artists must design with tiling in mind
- Test pattern at 2x2, 3x3 to verify seamlessness
- Edge pixels carefully matched

### Scale Consistency

**Pattern Resolution**:
- Define "pattern unit size" (e.g., 1 pattern unit = 0.5 tiles)
- All patterns use consistent scale
- Brick size similar across all brick patterns

**Zoom Independence**:
- Vector patterns scale with zoom
- No pixelation or blur
- Consistent appearance at all camera distances

### Visual Cohesion

**Match Overall Art Style**:
- Pattern art style matches game aesthetic
- Line weight consistent with other SVG assets
- Color palette coordination

**Material Believability**:
- Brick patterns look like bricks
- Wood grain looks like wood
- Not overly stylized (unless that's the art direction)

## Pattern Catalog Examples

### Stone/Masonry Patterns

**Cobblestone**:
- Irregular rounded stones
- Gaps filled with mortar/dirt
- Variation in stone size and color

**Brick (Red)**:
- Standard rectangular bricks
- Mortar lines
- Subtle color variation per brick

**Brick (Adobe)**:
- Tan/brown adobe bricks
- Rough texture
- Weathered appearance

**Fitted Stone**:
- Irregular shaped stones fitted together
- Castle/fortress walls
- Natural stone appearance

**Slate**:
- Flat layered stones
- Roof tiles or wall cladding
- Gray-blue color

### Wood Patterns

**Plank (Horizontal)**:
- Wood grain horizontal
- Plank boundaries visible
- Knots, variation

**Plank (Vertical)**:
- Wood grain vertical
- Fence/wall boards
- Knots, variation

**Rough Timber**:
- Unfinished wood
- Visible grain, knots
- Natural color variation

**Wood Shingles**:
- Overlapping roof shingles
- Weathered wood
- Irregular edges

### Natural Material Patterns

**Thatch**:
- Woven straw/reed
- Textured, organic
- Yellowish-brown

**Packed Earth**:
- Dirt floor texture
- Small stones, compressed soil
- Brown, natural

**Gravel**:
- Small stones
- Varied sizes and colors
- Gaps between stones

### Functional Patterns

**Concrete**:
- Smooth but slightly textured
- Gray, subtle variation
- Modern material

**Metal Plate**:
- Brushed metal appearance
- Rivets or seams
- Industrial look

**Parchment**:
- Aged paper texture
- Yellowed, fiber visible
- UI backgrounds

## Pattern Variation

### Color Variants

**Same Pattern, Different Colors**:
- Brick: red brick, brown brick, gray brick
- Wood: light oak, dark walnut, weathered gray
- Stone: gray granite, brown sandstone, white marble

**Biome-Appropriate Coloring**:
- Desert buildings: tan/adobe brick
- Temperate: red brick, dark wood
- Coastal: weathered wood, light stone

### Weathering States

**Age Variants**:
- Fresh/new: clean, vibrant
- Weathered: faded, worn
- Ancient: cracked, moss-covered, damaged

**Example - Wood Planks**:
- New planks: rich brown, clear grain
- Weathered planks: gray, faded, some cracks
- Ancient planks: dark gray, heavy cracking, moss

### Damage States

**Progressive Damage**:
- Intact pattern
- Slightly damaged (cracks, chips)
- Heavily damaged (large gaps, broken)
- Ruined (barely recognizable)

**Example - Stone Wall**:
- Intact: all stones fitted perfectly
- Damaged: some stones missing, cracks
- Ruined: many gaps, crumbling

## Pattern Application

### How Code Uses Patterns

**Building Construction**:
1. Player places building foundation
2. Code determines foundation shape (rectangular polygon)
3. Code determines material (player choice or biome default)
4. Code fills polygon with corresponding pattern (concrete_pattern.svg)
5. Pattern tiles to fill entire shape

**Dynamic Structures**:
- Any shape foundation (L-shaped, complex)
- Pattern fills automatically
- No need for pre-made foundation sprites

**Flexibility**:
- 10x10 foundation uses same pattern as 20x5 foundation
- Pattern scales to fit
- Infinite building configurations

### Integration with Procedural System

**Layering**:
1. Procedural ground cover (grass, dirt)
2. Code-drawn building shape filled with SVG pattern (foundation)
3. Code-drawn walls filled with SVG pattern (brick, wood)
4. Code-drawn roof filled with SVG pattern (thatch, tile)
5. SVG decorations/entities on top (doors, windows as separate SVGs)

**Result**: Buildings with procedural shape flexibility and hand-crafted material quality

## Performance Considerations

### Pattern Caching

**Rasterize or Vector**:
- Option A: Rasterize patterns to texture atlas (GPU texture)
- Option B: Tessellate vector patterns on CPU, batch render
- Trade-off: Memory vs CPU/GPU load

**Reuse**:
- Same pattern used many times
- Single pattern definition, many applications
- Efficient memory use

### Rendering Strategy

**From Vector Graphics System**:
- Patterns likely Tier 1 (pre-rasterized to atlas)
- Or Tier 2 (tessellated once, cached)
- Buildings are semi-static (rarely change)

## Open Questions

### Pattern Library Size

**1. How Many Patterns Needed?**:
- 10-15 core material patterns for MVP?
- 50+ for full material variety?
- Balance: variety vs asset creation time

**2. Variation vs Distinct Patterns?**:
- Multiple brick patterns, or one brick with color variants?
- Procedural color shifting vs separate assets?

### Pattern Resolution

**3. Pattern Unit Size?**:
- How large is one tile of the pattern?
- Affects visual detail and tiling frequency
- Needs visual testing

**4. Zoom Behavior?**:
- Pattern size fixed in world space (scales with zoom)?
- Or fixed in screen space (always same visual size)?

### Technical Implementation

**5. Rasterize or Tessellate?**:
- Pre-rasterize patterns to textures?
- Or real-time vector tessellation?
- Performance implications

**6. Pattern Distortion?**:
- Patterns on non-rectangular shapes (roofs, angles)?
- UV mapping strategy?
- Stretching vs proper alignment?

## Related Documentation

**Game Design**:
- [README.md](./README.md) - Overview of all SVG asset uses
- [svg-decorations.md](./svg-decorations.md) - SVG assets as placed objects
- [/docs/design/visual-style.md](../../visual-style.md) - Overall art direction
- [biome-ground-covers.md](../game-view/biome-ground-covers.md) - Ground covers that patterns sit on top of

**Technical** (future):
- SVG pattern rasterization
- Texture atlas generation
- UV mapping for code-drawn polygons
- Pattern tiling implementation

## Revision History

- 2025-10-26: Initial SVG texture patterns design document
