# Ground Texture System

Created: 2025-12-13
Status: Ready for Implementation

## Overview

This document specifies a **hybrid ground rendering system** that combines:
- **Tier 1 (Rasterized)**: Dense micro-detail (small grass stubble, dirt speckles, color variation) baked into tile textures
- **Tier 3 (Vector)**: Larger grass blades that remain as live vectors for wind animation and trampling response

This approach achieves Rimworld-quality visual richness while maintaining our SVG-first philosophy and animation capabilities.

## Problem Statement

### Current State

Ground tiles render as **solid colors**:
```cpp
Soil:   #4a7c3f  (flat green)
Dirt:   #725a40  (flat brown)
```

Grass entities are placed on top but the underlying ground looks artificial and "video game flat."

### Visual Goal

From [visual-style.md](../design/visual-style.md):
- "Rich detail: textures, decorations, subtle variations"
- "Procedural noise patterns create texture"
- "Hand-crafted feel most important"

We want ground that looks like **earth** - with tiny grass stubble, dirt speckles, color variation, and organic imperfection.

## Solution: Hybrid Tier 1 + Tier 3

```
┌─────────────────────────────────────────┐
│                                         │
│    🌿  🌿      🌿       🌿   🌿          │  ← TIER 3: Live vector grass
│       🌿    🌿     🌿                    │     - Fewer, larger blades
│  🌿        🌿   🌿       🌿     🌿       │     - Wind animation
│                                         │     - Trampling response
├─────────────────────────────────────────┤
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │  ← TIER 1: Rasterized texture
│ ░ ,,, ,, , ,,, , ,, ,,, ,, , ,,, , ,, ░ │     - Tiny grass stubble
│ ░ , , ,, ,,, ,, , ,,, , ,, ,,, ,, , , ░ │     - Dirt speckles
│ ░ ,,, , , ,, ,,, ,, , ,,, , ,, ,,, ,, ░ │     - Color variation
│ ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ │     - Static, high density
└─────────────────────────────────────────┘
```

### Why This Works

| Layer | Content | Count | Animation | Source |
|-------|---------|-------|-----------|--------|
| Tier 1 Texture | Micro grass stubble, speckles | Hundreds per tile | None (static) | SVG → Rasterized |
| Tier 3 Vectors | Large prominent grass blades | 10-30 per tile | Wind, trampling | SVG → Tessellated |

- **Visual continuity**: Stubble and blades share visual language (same SVG style)
- **Performance**: Texture handles density, vectors handle animation
- **Existing system**: Grass entities already work, just tune placement density

## Architecture

### Data Flow

```
STARTUP (once):
[SVG Tile Patterns]              assets/tiles/surfaces/Soil/pattern.svg
        ↓
[NanoSVG Parser]                 Parse SVG paths
        ↓
[Tessellator]                    Convert to triangles
        ↓
[Render-to-Texture]              Draw to FBO → capture as texture
        ↓
[Texture Atlas]                  Pack all surfaces into 4096×4096 (GPU)

CHUNK GENERATION (per chunk):
[TileAdjacency]                  Compute neighbor surfaces (existing)
        ↓
[TileAdjacency]                  Compute hardEdgeMask per tile (NEW)

RENDER (per frame):
[ChunkRenderer]                  Pass surface + hardEdgeMask to shader
        ↓
[Uber Shader]                    Sample atlas, render edges based on mask
        ↓
[Entity Renderer]                Draw vector grass ON TOP
```

### CPU vs GPU Work

| Operation | Where | When |
|-----------|-------|------|
| SVG parse + tessellate | CPU | Startup |
| Render-to-texture | GPU | Startup |
| Texture atlas storage | GPU | Persistent |
| Neighbor surface computation | CPU | Chunk generation |
| Hard edge mask computation | CPU | Chunk generation |
| Edge mask upload | CPU → GPU | Per vertex (1 byte) |
| Texture sampling | GPU | Per pixel |
| Edge type decision | GPU | Per pixel (read mask) |
| Blend/edge rendering | GPU | Per pixel |

**Key insight**: CPU pre-computes edge masks during chunk generation (extending existing TileAdjacency). GPU just reads the mask - no neighbor lookups needed in shader.

### What Changes

| Component | Current | New |
|-----------|---------|-----|
| TileAdjacency | Neighbor surfaces only | + hardEdgeMask (8 bits) |
| ChunkRenderer | Solid color per tile | Sample texture atlas |
| Tile vertex data | screenPos, color | screenPos, worldPos, surface, hardEdgeMask |
| Uber shader | Color passthrough | Texture sample + edge rendering |
| Grass entities | Many small blades | Fewer larger blades |

### What Stays the Same

- NanoSVG parsing
- Tessellation pipeline
- Entity placement system
- Grass blade SVG assets (just tuned for size/density)
- BatchRenderer / EntityRenderer

## Surface Family System

### The Problem with Uniform Blending

Rimworld's soft blending at ALL tile transitions looks bad. Some boundaries should be **crisp and hard** to communicate important information:
- Traversability boundaries (water's edge)
- Material state changes (solid vs liquid)
- Impassable terrain (cliff faces)

Vector graphics gives us the precision to render crisp edges where appropriate.

### Three-Family Hierarchy

Surfaces are organized into **families**. Edge behavior is determined by family membership:

```
SURFACE FAMILY (Hard edges BETWEEN families)
│
├── Ground (soft edges WITHIN) ─────── traversable natural terrain
│   ├── Soil
│   ├── ShortGrass
│   ├── TallGrass
│   ├── MossyGround
│   ├── FloweryMeadow
│   ├── ForestFloor
│   ├── Dirt
│   ├── Mud
│   ├── Sand
│   └── Gravel
│
├── Water (hard edge to Ground) ─────── impassable
│   ├── ShallowWater
│   ├── DeepWater
│   └── MurkyWater
│
└── Rock (hard edge to Ground) ──────── impassable cliffs
    ├── CliffFace
    ├── BoulderTop
    └── RockyOutcrop
```

**Not in this system:**
- **Built/flooring**: Handled as entities/structures, not tiles
- **Snow**: Seasonal overlay attribute on any tile, not its own surface

### Edge Behavior Rules

| Transition | Edge Type | Why |
|------------|-----------|-----|
| Ground ↔ Ground | **SOFT** | Natural variation (grass→dirt, sand→gravel) |
| Water ↔ Water | **SOFT** | Depth variation (shallow→deep) |
| Rock ↔ Rock | **SOFT** | Surface variation (cliff→boulder) |
| Ground ↔ Water | **HARD** | Shoreline - traversability boundary |
| Ground ↔ Rock | **HARD** | Cliff edge - impassability boundary |
| Water ↔ Rock | **HARD** | Rocky coast - both impassable |

### Data Model

```cpp
enum class SurfaceFamily : uint8_t {
    Ground,  // All traversable natural terrain
    Water,   // Impassable water
    Rock     // Impassable cliffs/boulders
};

enum class Surface : uint8_t {
    // Ground family (traversable)
    Soil, ShortGrass, TallGrass, MossyGround, FloweryMeadow,
    ForestFloor, Dirt, Mud, Sand, Gravel,

    // Water family (impassable)
    ShallowWater, DeepWater, MurkyWater,

    // Rock family (impassable)
    CliffFace, BoulderTop, RockyOutcrop
};

constexpr SurfaceFamily getFamily(Surface s) {
    if (s >= Surface::ShallowWater && s <= Surface::MurkyWater)
        return SurfaceFamily::Water;
    if (s >= Surface::CliffFace)
        return SurfaceFamily::Rock;
    return SurfaceFamily::Ground;
}

constexpr bool isHardEdge(Surface a, Surface b) {
    return getFamily(a) != getFamily(b);
}
```

### TileAdjacency Extension

Extend existing TileAdjacency to pre-compute hard edge mask during chunk generation:

```cpp
// In TileAdjacency.h - add to existing structure
struct TileAdjacency {
    uint64_t neighborData;   // Existing: 8 directions × 6 bits
    uint8_t hardEdgeMask;    // NEW: 1 bit per direction (1 = hard edge)

    // Direction bit positions (matching existing Direction enum)
    // Bit 0 = NW, Bit 1 = W, Bit 2 = SW, Bit 3 = S
    // Bit 4 = SE, Bit 5 = E, Bit 6 = NE, Bit 7 = N

    void computeHardEdgeMask(Surface mySurface) {
        hardEdgeMask = 0;
        SurfaceFamily myFamily = getFamily(mySurface);

        for (int dir = 0; dir < 8; dir++) {
            Surface neighbor = getNeighbor(static_cast<Direction>(dir));
            if (getFamily(neighbor) != myFamily) {
                hardEdgeMask |= (1 << dir);
            }
        }
    }

    bool hasHardEdge(Direction dir) const {
        return (hardEdgeMask & (1 << static_cast<int>(dir))) != 0;
    }
};
```

### Vertex Data

```cpp
// Tile vertex includes pre-computed edge mask
struct TileVertex {
    glm::vec2 screenPos;
    glm::vec2 worldPos;
    uint8_t surface;        // Surface type (for atlas lookup)
    uint8_t hardEdgeMask;   // Pre-computed: which edges are hard
    uint8_t padding[2];     // Alignment
};
```

## Tile Texture Patterns

### SVG Pattern Design

Each surface type gets a tileable SVG pattern containing micro-detail:

```xml
<!-- assets/tiles/surfaces/Soil/pattern.svg -->
<!-- 512×512 pattern covering 8×8 game tiles -->
<svg viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <!-- Base soil color with subtle variation -->
  <rect width="512" height="512" fill="#4a7c3f"/>

  <!-- Color variation patches (organic, not geometric) -->
  <ellipse cx="100" cy="80" rx="60" ry="40" fill="#527f45" opacity="0.25"/>
  <ellipse cx="350" cy="200" rx="45" ry="35" fill="#3d6b32" opacity="0.3"/>
  <ellipse cx="200" cy="400" rx="70" ry="50" fill="#4f8244" opacity="0.2"/>

  <!-- Dirt speckles (dozens, scattered) -->
  <circle cx="50" cy="120" r="4" fill="#725a40" opacity="0.5"/>
  <circle cx="200" cy="300" r="3" fill="#725a40" opacity="0.6"/>
  <circle cx="400" cy="80" r="5" fill="#6b5238" opacity="0.4"/>
  <!-- ... 30-50 more speckles ... -->

  <!-- Micro grass stubble (tiny strokes) -->
  <path d="M60,200 l2,-8" stroke="#5a8c4f" stroke-width="1.5" opacity="0.4"/>
  <path d="M65,205 l1,-6" stroke="#4d7a42" stroke-width="1" opacity="0.5"/>
  <path d="M63,198 l-1,-7" stroke="#5a8c4f" stroke-width="1" opacity="0.3"/>
  <!-- ... hundreds of tiny grass marks ... -->

  <!-- Small pebbles (occasional) -->
  <ellipse cx="300" cy="150" rx="6" ry="4" fill="#8a8a8a" opacity="0.3"/>
</svg>
```

### Pattern Guidelines

- **Size**: 512×512 SVG covering 8×8 game tiles (64px per tile at 1:1 zoom)
- **Tileable**: Must seamlessly repeat - edges wrap to opposite side
- **Density**: High detail count (100+ elements) - this IS the texture
- **Subtlety**: No pronounced features that become obvious when repeated
- **Style**: Match grass blade visual language for continuity

### Surface Types by Family

**Ground Family** (traversable, soft edges between):

| Surface | Base Color | Micro Detail |
|---------|------------|--------------|
| Soil | Green #4a7c3f | Base grass, dirt speckles |
| ShortGrass | Light green #5a9c4f | Dense short stubble |
| TallGrass | Green #4a8c3f | Longer grass marks, seed heads |
| MossyGround | Teal-green #3a7c4f | Moss patches, damp appearance |
| FloweryMeadow | Green #4a7c3f | Tiny flower dots, varied colors |
| ForestFloor | Brown-green #5a6c3f | Leaf litter, pine needles, twigs |
| Dirt | Brown #725a40 | Pebbles, dried grass bits, cracks |
| Mud | Dark brown #593f26 | Wet patches, footprint hints, debris |
| Sand | Tan #d1b578 | Grain texture, shell fragments, ripples |
| Gravel | Gray-brown #7a7a6a | Small stones, varied colors |

**Water Family** (impassable, hard edge to Ground):

| Surface | Base Color | Micro Detail |
|---------|------------|--------------|
| ShallowWater | Light blue #3a7c9a | Visible bottom, ripples |
| DeepWater | Dark blue #1a4c7a | Darker, wave patterns |
| MurkyWater | Brown-blue #4a5c5a | Murky, organic debris |

**Rock Family** (impassable cliffs, hard edge to Ground):

| Surface | Base Color | Micro Detail |
|---------|------------|--------------|
| CliffFace | Gray #6b6b6b | Cracks, sharp edges, lichen |
| BoulderTop | Gray #7a7a7a | Rounded wear, moss spots |
| RockyOutcrop | Dark gray #5a5a5a | Jagged, exposed stone |

**Note:** Snow is a seasonal overlay, not a surface type. It renders on top of any Ground surface.

## Render-to-Texture System

### Framebuffer Object Wrapper

```cpp
// libs/renderer/RenderToTexture.h
class RenderToTexture {
public:
    RenderToTexture(int width, int height);
    ~RenderToTexture();

    void begin();           // Bind FBO, set viewport
    void end();             // Unbind, restore state
    GLuint getTexture();    // Get resulting texture ID

private:
    GLuint fbo_;
    GLuint texture_;
    int width_, height_;
    GLint previousViewport_[4];
    GLint previousFBO_;
};
```

### Rasterization Process

```cpp
GLuint rasterizeSVGPattern(const std::string& svgPath, int outputSize) {
    // 1. Parse SVG with nanosvg
    auto* image = nsvgParseFromFile(svgPath.c_str(), "px", 96.0f);

    // 2. Tessellate all shapes to triangles
    std::vector<Triangle> triangles = tessellate(image);

    // 3. Create render target
    RenderToTexture target(outputSize, outputSize);

    // 4. Render to texture
    target.begin();

    // Set orthographic projection matching SVG viewBox
    auto projection = glm::ortho(0.f, 512.f, 512.f, 0.f);
    batchRenderer.setProjection(projection);

    // Draw all triangles
    for (const auto& tri : triangles) {
        batchRenderer.addTriangle(tri);
    }
    batchRenderer.flush();

    target.end();

    nsvgDelete(image);
    return target.getTexture();
}
```

## Texture Atlas

### Structure

```cpp
class TileTextureAtlas {
public:
    static constexpr int ATLAS_SIZE = 4096;
    static constexpr int PATTERN_SIZE = 512;
    static constexpr int TILE_COVERAGE = 8;  // Pattern covers 8×8 tiles

    void addSurface(Surface surface, GLuint texture);
    void build();

    GLuint getAtlasTexture() const;
    glm::vec4 getUVRect(Surface surface) const;  // xy=offset, zw=size

private:
    GLuint atlasTexture_;
    std::array<glm::vec4, 8> uvRects_;
};
```

### Layout

4096×4096 atlas with 512×512 patterns = 64 slots (8×8 grid)

```
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│  Soil  │  Dirt  │  Sand  │  Rock  │ Water  │  Snow  │  Mud   │ future │
├────────┼────────┼────────┼────────┼────────┼────────┼────────┼────────┤
│                         (variants / future)                           │
└────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┘
```

**Memory**: 4096×4096×4 = 64 MB VRAM

## Shader Integration

### Uber Shader Extension

```glsl
// New uniforms
uniform sampler2D u_tileAtlas;
uniform vec4 u_atlasUV[8];      // Per-surface UV rects
uniform float u_tileCoverage;   // Tiles per pattern (8.0)

// New render mode: -3.0 = tile texture
if (v_renderMode < -2.5) {
    int surface = int(v_color.r * 255.0);  // Surface encoded in red channel
    vec4 uvRect = u_atlasUV[surface];

    // World position → pattern UV (repeating)
    vec2 patternUV = fract(v_worldPos / u_tileCoverage);

    // Pattern UV → atlas UV
    vec2 atlasUV = uvRect.xy + patternUV * uvRect.zw;

    fragColor = texture(u_tileAtlas, atlasUV);
}
```

### ChunkRenderer Changes

```cpp
void ChunkRenderer::renderTile(const TileData& tile, glm::vec2 screenPos) {
    // Encode surface type and world position for shader
    vertices.push_back({
        .position = screenPos,
        .worldPos = tile.worldPosition,
        .color = glm::vec4(float(tile.surface) / 255.f, 0, 0, 1),
        .renderMode = -3.0f  // Tile texture mode
    });
}
```

## Grass Entity Tuning

### Current vs New Placement

| Parameter | Current | New |
|-----------|---------|-----|
| Blades per tile | 50-100 | 10-30 |
| Blade height | 0.2-0.5m | 0.3-0.8m |
| Visual role | All grass coverage | Prominent accent blades |

The micro-stubble in the texture handles the "grass coverage" feeling. The vector blades become accent pieces that add depth and animation.

### Asset Definition Update

```xml
<!-- assets/world/flora/GrassBlade/GrassBlade.xml -->
<asset defName="GrassBlade" type="procedural">
  <!-- Reduced density since texture provides base coverage -->
  <placement>
    <biomes>Grassland, Forest</biomes>
    <spawnChance>0.15</spawnChance>  <!-- Was 0.4 -->
    <clumping>0.6</clumping>
  </placement>

  <!-- Larger, more prominent blades -->
  <generator>
    <heightRange>0.3, 0.8</heightRange>  <!-- Was 0.2, 0.5 -->
    <widthRange>0.08, 0.15</widthRange>
  </generator>
</asset>
```

## Edge Rendering

Edge rendering behavior depends on whether adjacent tiles are in the **same family** (soft blend) or **different families** (hard edge). The `hardEdgeMask` is pre-computed on CPU and passed to the shader.

### Shader Implementation

```glsl
// Vertex inputs (from TileVertex)
flat in uint v_surface;         // Surface type for atlas lookup
flat in uint v_hardEdgeMask;    // Pre-computed: which edges are hard (8 bits)

// Direction bit masks
const uint EDGE_N  = 0x80u;  // Bit 7
const uint EDGE_NE = 0x40u;  // Bit 6
const uint EDGE_E  = 0x20u;  // Bit 5
const uint EDGE_SE = 0x10u;  // Bit 4
const uint EDGE_S  = 0x08u;  // Bit 3
const uint EDGE_SW = 0x04u;  // Bit 2
const uint EDGE_W  = 0x02u;  // Bit 1
const uint EDGE_NW = 0x01u;  // Bit 0

void main() {
    // Sample tile texture
    vec4 baseColor = texture(u_tileAtlas, atlasUV);

    // Position within tile (0-1)
    vec2 tilePos = fract(v_worldPos);

    // Distance from each edge
    float distN = 1.0 - tilePos.y;
    float distS = tilePos.y;
    float distE = 1.0 - tilePos.x;
    float distW = tilePos.x;

    // Check pre-computed hard edge mask
    bool hardN = (v_hardEdgeMask & EDGE_N) != 0u;
    bool hardE = (v_hardEdgeMask & EDGE_E) != 0u;
    bool hardS = (v_hardEdgeMask & EDGE_S) != 0u;
    bool hardW = (v_hardEdgeMask & EDGE_W) != 0u;

    fragColor = baseColor;

    // Hard edge rendering (crisp boundary with shadow)
    float hardEdgeWidth = 0.03;  // 3% of tile
    float hardShadow = 0.7;

    if (hardN && distN < hardEdgeWidth) fragColor.rgb *= hardShadow;
    if (hardE && distE < hardEdgeWidth) fragColor.rgb *= hardShadow;
    if (hardS && distS < hardEdgeWidth) fragColor.rgb *= hardShadow;
    if (hardW && distW < hardEdgeWidth) fragColor.rgb *= hardShadow;

    // Soft edge rendering (gradient blend) - only if NOT hard edge
    // Note: requires neighbor surface info for blending (Phase 2)
    float softBlendZone = 0.15;  // 15% of tile
    // ... soft blend implementation in later phase
}
```

### Hard Edge Rendering

For transitions between families (shoreline, cliff edge):

- **No texture blending** across boundary
- **Crisp boundary** with 1-3px shadow for depth
- Pre-computed mask means **zero GPU neighbor lookups**

```glsl
// Simple hard edge: darken near boundary
if (hardN && distN < 0.03) {
    fragColor.rgb *= 0.7;
}
```

### Soft Edge Rendering (Future Phase)

For transitions within a family (grass→dirt), we need neighbor surface info:

**Option A: Additional vertex data**
```cpp
struct TileVertex {
    // ... existing fields
    uint8_t neighborSurfaceN;  // For soft blending
    uint8_t neighborSurfaceE;
    uint8_t neighborSurfaceS;
    uint8_t neighborSurfaceW;
};
```

**Option B: Surface map texture**
```glsl
// Sample neighbor surface from texture, then sample their atlas UV
int neighborSurf = texelFetch(u_surfaceMap, tileCoord + ivec2(0,1), 0).r;
vec4 neighborColor = texture(u_tileAtlas, getAtlasUV(neighborSurf, neighborPatternUV));
fragColor = mix(neighborColor, baseColor, smoothstep(0.0, 0.15, distN));
```

Soft blending is deferred to a later implementation phase.

### Visual Comparison

```
SOFT EDGE (Grass → Dirt)          HARD EDGE (Grass → Water)
┌──────────┬──────────┐           ┌──────────┬──────────┐
│ Grass    │▓▓▒▒░░    │           │ Grass    │          │
│          │▓▓▒▒░░Dirt│           │         ▒║ Water    │
│          │▓▓▒▒░░    │           │         ▒║          │
└──────────┴──────────┘           └──────────┴──────────┘
   gradual blend                     crisp + shadow (▒║)
```

### Implementation Priority

1. **Phase 1**: Textured tiles, no edge handling
2. **Phase 2**: Hard edge rendering (read mask, darken edges)
3. **Phase 3**: Soft edge blending (add neighbor data, gradient blend)

## Implementation Phases

### Phase 1: Render-to-Texture Infrastructure
- [ ] Create `RenderToTexture` class (FBO wrapper)
- [ ] Test: render colored quad to texture, verify output
- [ ] Test: render existing SVG asset to texture

### Phase 2: Tile Pattern Assets
- [ ] Create Soil pattern SVG (prototype with stubble + speckles)
- [ ] Define asset XML format for tile patterns
- [ ] Verify pattern tiles seamlessly

### Phase 3: Atlas Builder
- [ ] Create `TileTextureAtlas` class
- [ ] Rasterize pattern to atlas slot
- [ ] Test with single surface type

### Phase 4: Shader Integration (Textured Tiles)
- [ ] Add tile texture render mode to uber shader
- [ ] Update `TileVertex` struct with surface type
- [ ] Update ChunkRenderer to pass world position + surface
- [ ] Test: Soil tiles render with texture

### Phase 5: Surface Family + Hard Edge Mask
- [ ] Implement `SurfaceFamily` enum and `getFamily()` lookup
- [ ] Extend `TileAdjacency` with `hardEdgeMask` field
- [ ] Call `computeHardEdgeMask()` during chunk generation
- [ ] Add `hardEdgeMask` to `TileVertex` struct

### Phase 6: Hard Edge Rendering
- [ ] Pass `hardEdgeMask` to shader as vertex attribute
- [ ] Implement edge shadow rendering (read mask, darken near edge)
- [ ] Test: shorelines and cliff edges render with crisp boundaries

### Phase 7: All Surface Types
- [ ] Create patterns for Ground family (10 surfaces)
- [ ] Create patterns for Water family (3 surfaces)
- [ ] Create patterns for Rock family (3 surfaces)
- [ ] Build complete atlas

### Phase 8: Grass Entity Tuning
- [ ] Reduce grass blade density in placement
- [ ] Increase blade size range
- [ ] Visual balance: texture + vectors feel unified

### Phase 9: Soft Edge Blending (Optional Enhancement)
- [ ] Decide approach: vertex data vs surface map texture
- [ ] Pass neighbor surface info to shader
- [ ] Implement alpha gradient blend for same-family transitions
- [ ] Test: grass→dirt, shallow→deep water blend smoothly

### Phase 10: Polish
- [ ] Moisture influence on texture color
- [ ] LOD: simpler textures when zoomed out
- [ ] Optional: disk caching of atlas
- [ ] Optional: decorative shoreline foam, cliff edge details

## Open Questions

1. **Pattern authoring**: Hand-draw SVGs or generate procedurally via Lua?

2. **Water treatment**: Static texture pattern or separate animated water shader?

3. **Variants**: Single pattern per surface or multiple variants for more variety?

4. **Disk caching**: Cache rasterized atlas to skip render-to-texture on load?

## Performance Budget

| Operation | Cost | When |
|-----------|------|------|
| SVG parse + tessellate | ~50ms | Startup (once) |
| Render-to-texture (7 patterns) | ~15ms | Startup (once) |
| Atlas texture memory | 64 MB | Persistent |
| Per-frame texture sampling | ~0.1ms | Every frame |

## Related Documentation

- [Vector Graphics Architecture](./vector-graphics/INDEX.md) - Tier 1/3 system
- [Visual Style](../design/visual-style.md) - Art direction
- [Entity Placement System](./entity-placement-system.md) - Grass placement
- [Asset System](./asset-system/README.md) - Asset definitions

## Rimworld Research Sources

- [Rimworld-style tilemap shader (Godot)](https://godotshaders.com/shader/rimworld-style-tilemap-shader-with-tutorial-video/)
- [RimWorld Wiki - Mod Textures](https://rimworldwiki.com/wiki/Modding_Tutorials/Textures)
