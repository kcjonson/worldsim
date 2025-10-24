# Vector Asset Pipeline

Created: 2025-10-12
Last Updated: 2025-10-12
Status: Active

## Context

**Core Requirement**: All or most game assets are vector-based, stored as SVG files on disk.

**Goals:**
- Dynamic tile rendering with procedural variation
- Visual interactions between adjacent tiles
- Support for millions of vector objects
- Efficient memory usage despite large numbers

This is a **fundamental architectural decision** affecting rendering, memory management, and performance.

## Decision

Implement a hybrid vector/raster pipeline:

1. **Assets stored as SVG** on disk (small, scalable)
2. **Dynamic tessellation** to raster as needed
3. **Aggressive caching** of rasterized results
4. **Procedural variation** applied to base SVG data
5. **Inter-tile blending** for visual cohesion

## Architecture Overview

```
SVG File → Parser → Vector Data → Variation → Tessellation → Raster Cache → GPU
                                      ↓
                                 Tile Adjacency
                                      ↓
                                 Visual Blending
```

## Implementation Strategy

### 1. SVG Loading and Parsing

**Library Choice:**
- **NanoSVG** (header-only, lightweight) - Good for simple SVGs
- **LunaSVG** (full-featured) - Better for complex SVGs
- **Custom parser** - Maximum control, more work

**Recommendation:** Start with NanoSVG, upgrade if needed.

**Location:** `libs/renderer/resources/svg/`

**API:**
```cpp
class SVGAsset {
public:
    static SVGAsset* Load(const char* path);

    void Rasterize(
        uint8_t* buffer,
        int width,
        int height,
        float scale
    );

    BoundingBox GetBounds() const;
    void ApplyVariation(const VariationParams& params);
};
```

### 2. Tile Variation System

**Concept:** Each tile instance can have procedural variations applied to the base SVG.

**Variation Parameters:**
```cpp
struct TileVariation {
    uint32_t seed;              // For deterministic randomness
    float   colorShift;         // Hue shift (-1 to 1)
    float   saturationMod;      // Saturation multiplier
    float   brightnessMod;      // Brightness multiplier
    float   scale;              // Size variation (0.9 to 1.1)
    float   rotation;           // Rotation in degrees
    uint8_t edgeBlendMask;      // Which edges blend with neighbors
};
```

**Application:**
- Load base SVG
- Apply variation transforms
- Cache result with variation hash
- Reuse cached variants when same variation occurs

**Location:** `libs/world/sampling/` or `libs/game-systems/tiles/`

### 3. Vector to Raster Conversion

**Strategies:**

#### Strategy A: Pre-Rasterize Common Tiles
```cpp
// At load time, rasterize common tiles at multiple scales
class TileAtlas {
    void PreRasterize(
        SVGAsset* svg,
        const std::vector<int>& scales  // e.g., [16, 32, 64, 128]
    );

    TextureHandle GetRasterized(
        AssetID asset,
        int scale,
        const TileVariation& variation
    );
};
```

**Benefits:**
- Fast lookup, no runtime tessellation
- Predictable performance

**Drawbacks:**
- High memory usage
- Limited variation

#### Strategy B: On-Demand Rasterization with LRU Cache
```cpp
class VectorCache {
    struct CacheKey {
        AssetID asset;
        int scale;
        TileVariation variation;
    };

    TextureHandle GetOrRasterize(const CacheKey& key);

private:
    LRUCache<CacheKey, RasterData> cache;
    size_t maxCacheSize;  // e.g., 256 MB
};
```

**Benefits:**
- Memory efficient
- Supports unlimited variations

**Drawbacks:**
- Potential frame hitches on cache miss
- Need to manage cache size

#### Strategy C: Hybrid Approach (Recommended)
```cpp
class TileRenderer {
    TileAtlas preRasterized;    // Common tiles, all scales
    VectorCache dynamicCache;   // Variations and rare tiles

    TextureHandle GetTile(
        AssetID asset,
        int scale,
        const TileVariation& variation
    ) {
        // Try pre-rasterized first
        if (IsCommon(asset) && IsDefaultVariation(variation)) {
            return preRasterized.Get(asset, scale);
        }

        // Fall back to dynamic cache
        return dynamicCache.GetOrRasterize(asset, scale, variation);
    }
};
```

**Benefits:**
- Fast for common cases
- Flexible for variations
- Bounded memory usage

### 4. Inter-Tile Visual Interactions

**Goal:** Tiles visually blend with neighbors for cohesive appearance.

**Techniques:**

#### Edge Blending
```cpp
struct TileEdgeData {
    enum Edge { Top, Right, Bottom, Left };

    AssetID neighborAsset[4];
    uint8_t blendMask;  // Which edges to blend
};

void BlendTileEdges(
    SVGAsset* tile,
    const TileEdgeData& edges
) {
    // Modify SVG to blend with neighbors:
    // - Extend grass into dirt
    // - Fade water into shore
    // - Blend forest density
}
```

#### Shared Elements
```cpp
// Place visual elements that span tiles
struct SpanningElement {
    vec2 worldPosition;
    SVGAsset* asset;
    std::vector<TileCoord> affectedTiles;
};

// Examples: large rocks, trees, roads
```

#### Procedural Transitions
```cpp
// Generate transition SVG based on tile types
SVGAsset* GenerateTransition(
    TileType from,
    TileType to,
    TransitionEdge edge
) {
    // Procedurally create blending graphic
    // Cache result for this transition type
}
```

**Location:** `libs/game-systems/tiles/blending/`

### 5. Memory Management

**Challenge:** Millions of tiles, each with vector + raster data.

**Solution: Tiered Memory Strategy**

```cpp
// Tier 1: Vector data (small, always in memory)
struct VectorTileData {
    AssetID baseAsset;
    TileVariation variation;
};  // ~32 bytes per tile

// Tier 2: Rasterized textures (large, cached)
struct RasterTileData {
    TextureHandle texture;
    uint32_t lastAccessFrame;
};  // Only for visible/recent tiles

// Tier 3: Tessellated geometry (medium, cached)
struct TessellatedData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};  // For real-time vector rendering (if we do this)
```

**Cache Eviction:**
```cpp
class TileMemoryManager {
    void EvictOldRasters() {
        // Remove rasters not accessed in N frames
        // Keep vector data always
        // Can regenerate raster from vector + variation
    }

    size_t GetMemoryUsage() const;
    void SetMemoryBudget(size_t bytes);
};
```

**Memory Budget Example:**
- 1 million tiles visible
- Vector data: 32 bytes × 1M = 32 MB
- Raster cache: 64×64 RGBA × 10K tiles = 160 MB
- Total: ~200 MB (reasonable)

### 6. Level of Detail (LOD)

**Concept:** Rasterize at different resolutions based on zoom level.

```cpp
enum LODLevel {
    LOD_0 = 128,  // Fully zoomed in
    LOD_1 = 64,   // Default
    LOD_2 = 32,   // Zoomed out
    LOD_3 = 16    // Far away
};

LODLevel GetLODForZoom(float zoomLevel) {
    if (zoomLevel >= 2.0f) return LOD_0;
    if (zoomLevel >= 1.0f) return LOD_1;
    if (zoomLevel >= 0.5f) return LOD_2;
    return LOD_3;
}
```

**Benefits:**
- Reduced memory usage when zoomed out
- Better performance (fewer pixels)
- Still maintains vector scalability

### 7. Performance Optimizations

#### Async Rasterization
```cpp
class AsyncRasterizer {
    void RequestRasterization(
        const CacheKey& key,
        std::function<void(TextureHandle)> callback
    );

    // Rasterize on worker thread
    // Return placeholder texture immediately
    // Swap in real texture when ready
};
```

#### Instanced Rendering
```cpp
// For identical tiles, use GPU instancing
void RenderTiles(
    TextureHandle texture,
    const std::vector<TileInstance>& instances
) {
    // Single draw call for all instances of same tile
}
```

#### Texture Atlases
```cpp
class TileAtlas {
    // Pack multiple tiles into single texture
    // Reduce texture switches
    // Better batching
    TextureHandle atlas;
    std::map<AssetID, AtlasRect> locations;
};
```

#### Dirty Rectangle Optimization
```cpp
// Only rasterize changed portions of SVG
struct DirtyRegion {
    BoundingBox region;
    bool needsRasterization;
};
```

## Trade-offs

**Pros:**
- **Scalability**: Vector assets scale to any resolution
- **Variation**: Unlimited procedural variations
- **File size**: SVG files are small
- **Artist-friendly**: Standard SVG tools
- **Visual richness**: Complex visuals without large assets

**Cons:**
- **Complexity**: More complex than bitmap pipeline
- **Performance**: Tessellation/rasterization overhead
- **Memory management**: Need careful caching
- **Debugging**: Harder to inspect intermediate state
- **Compatibility**: Need robust SVG parser

## Alternatives Considered

### Option 1: Pure Raster (PNG/JPG)
**Rejected** - No procedural variation, huge file sizes for detail, no scalability

### Option 2: Pure Vector Real-Time
**Rejected** - Too slow to render millions of vector objects per frame

### Option 3: Procedural Pixel Generation
**Rejected** - Harder to author, less artist control

## Implementation Status

- [ ] SVG loading system
- [ ] Vector to raster conversion
- [ ] Tile variation system
- [ ] Raster caching with LRU
- [ ] Inter-tile blending
- [ ] Memory management
- [ ] LOD system
- [ ] Performance profiling and optimization

## Implementation Order

1. **Phase 1: Basic Pipeline**
   - SVG loading (NanoSVG integration)
   - Simple rasterization (no variation)
   - Display single static tile

2. **Phase 2: Caching**
   - LRU cache implementation
   - Pre-rasterize common tiles
   - Memory budget management

3. **Phase 3: Variation**
   - Tile variation parameters
   - Procedural modification of SVG
   - Variation caching with hash keys

4. **Phase 4: Blending**
   - Edge detection between tiles
   - Blending algorithm
   - Transition generation

5. **Phase 5: Optimization**
   - Async rasterization
   - Texture atlases
   - Profiling and tuning

## Related Documentation

- Design Doc: [Vector Assets Feature](/docs/design/features/vector-assets/README.md)
- Tech: [Renderer Architecture](./renderer-architecture.md)
- Tech: [Resource Management](./resource-management.md)
- Code: `libs/renderer/resources/svg/` (once implemented)
- Code: `libs/game-systems/tiles/` (once implemented)

## Notes

**SVG Complexity Limits:**
- Keep SVGs simple for performance
- Limit path complexity
- Avoid filters/effects (rasterize slowly)
- Consider pre-simplifying complex SVGs

**Testing Strategy:**
- Start with simple test SVG (single color square)
- Progress to complex tile (grass with detail)
- Test variation generation
- Profile memory and performance
- Test with millions of tiles

**Future Enhancements:**
- GPU-accelerated tessellation (OpenGL geometry shaders)
- Signed Distance Field (SDF) rendering for very sharp vectors
- Animation support (SMIL or procedural)
