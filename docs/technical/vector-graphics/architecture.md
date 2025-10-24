# Vector Graphics Rendering Architecture

Created: 2025-10-24
Last Updated: 2025-10-24
Status: Design

## Overview

This document defines the architecture for vector graphics rendering in world-sim. The system must support **static tile backgrounds**, **semi-static structures**, and **thousands of dynamically animated entities** (grass, trees, water) all rendered from vector (SVG) sources at 60 FPS.

**Key Innovation**: Unlike traditional tile-based games that use static sprites, this system enables:
- Real-time spline-based deformation (trees swaying, grass bending)
- Per-entity procedural variation from base assets
- Interactive visual feedback (trampling, wind, harvesting)
- Scalable rendering at any zoom level
- Memory-efficient storage (vector data + caching)

## Design Principles

1. **Hybrid Multi-Tier**: Different rendering strategies for different update frequencies
2. **Cache Aggressively**: Pre-compute what doesn't change, stream what does
3. **Batch Everything**: Minimize draw calls through intelligent batching
4. **Separate Concerns**: Render geometry ≠ collision geometry
5. **Desktop First**: Leverage OpenGL 3.3+ features without mobile constraints
6. **Defer Complexity**: Start with proven CPU techniques before GPU compute

## Four-Tier Rendering Architecture

The system uses four distinct rendering tiers based on update frequency and complexity:

### Tier 1: Static Tile Backgrounds

**What**: Unchanging tile terrain (grass texture, dirt, stone, etc.)

**Rendering Strategy**:
```
SVG Source → Pre-Rasterize (multiple LODs) → Texture Atlas → Static Quad Rendering
```

**Characteristics**:
- **Update Frequency**: Never (after initial load)
- **Tessellation**: Offline, during asset import or first use
- **GPU Storage**: Texture atlas (2D textures)
- **Rendering**: Simple textured quads, batched
- **Memory**: ~200 MB budget for texture atlases

**Use Cases**:
- Tile ground textures (grass, dirt, stone)
- Terrain base layers
- Static decorative elements

**Performance**:
- Single draw call per atlas (instanced rendering)
- Zero CPU cost during gameplay
- Minimal GPU cost (textured quad fillrate)

**Implementation Notes**:
- Rasterize at 4 LOD levels: 16px, 32px, 64px, 128px
- Pack into texture atlases using rect-packing algorithm
- Generate mipmaps for smooth LOD transitions
- UV coordinates stored per tile type

### Tier 2: Semi-Static Structures

**What**: Objects that don't change shape but move/rotate (buildings, rocks, non-animated trees, fences)

**Rendering Strategy**:
```
SVG Source → CPU Tessellation → Cached GPU Mesh → Transform-Only Rendering
```

**Characteristics**:
- **Update Frequency**: Only position/rotation changes (transform matrix)
- **Tessellation**: Once at load time or first use
- **GPU Storage**: Vertex Buffer Objects (VBOs), cached
- **Rendering**: Batched geometry with per-instance transforms
- **Memory**: ~100 MB budget for cached meshes

**Use Cases**:
- Buildings and structures
- Large rocks and boulders
- Non-animated trees
- Fences, walls, roads

**Performance**:
- Tessellate once, render many times
- Batched by material/texture
- Transform updates via uniform buffer or instancing
- Minimal CPU cost (just transform calculations)

**Implementation Notes**:
- Cache tessellated meshes in hash map (SVG hash → mesh)
- Use instanced rendering for identical objects
- Support color tinting via uniform
- Collision shapes generated separately (simpler)

### Tier 3: Dynamic Animated Entities

**What**: Objects with real-time shape changes (grass blades, swaying trees, water ripples, animated creatures)

**Rendering Strategy**:
```
SVG Source + Animation Data → CPU Tessellation (per frame) → Streaming VBO → Batched GPU Rendering
```

**Characteristics**:
- **Update Frequency**: Every frame or as needed (60 Hz)
- **Tessellation**: Real-time, on-demand
- **GPU Storage**: Streaming VBOs (double/triple buffered)
- **Rendering**: Batched by material, single draw call per batch
- **Memory**: ~50 MB budget for streaming buffers

**Use Cases**:
- Grass blades (bending, trampling, swaying)
- Animated tree leaves/branches
- Water surfaces with ripples
- Animated creatures
- Particle-like effects

**Performance**:
- **Critical Path**: Must tessellate efficiently
- Re-tessellate only visible, changed entities
- Batch all entities of same material
- Use spatial partitioning for visibility culling
- Stream geometry updates to GPU efficiently

**Implementation Notes**:
- **Spline Deformation**: Modify Bezier control points before tessellation
- **Delta Updates**: Only update changed vertices if possible
- **Memory Arenas**: Use frame-local allocators for tessellation scratch memory
- **Multi-threading**: Consider parallel tessellation for large entity counts
- **Dirty Tracking**: Only re-tessellate entities that changed

**Example Data Flow** (Grass Blade):
```
1. Base SVG path (single blade shape)
2. Animation state (bend angle, time offset)
3. Physics influence (entity nearby? apply trampling force)
4. Spline deformation (modify control points)
5. Tessellate deformed path → triangles
6. Add triangles to batch buffer
7. GPU renders entire grass batch in one call
```

### Tier 4: GPU Compute (Future)

**What**: Advanced effects that benefit from massive parallelization

**Rendering Strategy**:
```
Vector Primitives → GPU Compute Shaders → Tessellation/Rasterization → Framebuffer
```

**Characteristics**:
- **Update Frequency**: Every frame, GPU-side
- **Tessellation**: GPU compute shaders (prefix-sum, scan algorithms)
- **GPU Storage**: Compute shader buffers, textures
- **Rendering**: Direct GPU rasterization (Vello-style)
- **Memory**: Variable, depends on implementation

**Use Cases** (Deferred):
- Massive particle systems (thousands of complex shapes)
- Complex procedural effects (fire, smoke, magic)
- GPU-accelerated curve tessellation
- Advanced shading effects (gradients, filters)

**Implementation Notes**:
- **Deferred**: Not needed for initial implementation
- Requires OpenGL 4.3+ (compute shaders)
- Reference implementation: Vello renderer
- Consider only if Tier 3 becomes bottleneck
- Significant complexity increase

## System Data Flow

### High-Level Pipeline

```
┌─────────────────┐
│   SVG Assets    │
│  (on disk)      │
└────────┬────────┘
         │
         ├──────────────┐
         │              │
    [Import Time]  [Runtime]
         │              │
         ▼              ▼
┌────────────────┐ ┌──────────────────┐
│ Pre-Rasterize  │ │  Load SVG        │
│ (Tier 1)       │ │  Parse → Paths   │
└────────┬───────┘ └────────┬─────────┘
         │                  │
         ▼                  │
┌──────────────────┐        ├─────────────────┐
│  Texture Atlas   │        │                 │
│  (GPU Textures)  │        ▼                 ▼
└──────────────────┘  ┌─────────────┐  ┌─────────────────┐
                      │ Tessellate  │  │ Animation Data  │
                      │ Once        │  │ (Splines, Keys) │
                      │ (Tier 2)    │  └────────┬────────┘
                      └──────┬──────┘           │
                             │                  │
                             ▼                  ▼
                       ┌────────────┐    ┌───────────────┐
                       │  Cached    │    │  Per-Frame    │
                       │  VBO       │    │  Deformation  │
                       └──────┬─────┘    └───────┬───────┘
                              │                  │
                              │                  ▼
                              │          ┌──────────────┐
                              │          │  Tessellate  │
                              │          │  (Tier 3)    │
                              │          └───────┬──────┘
                              │                  │
                              │                  ▼
                              │          ┌──────────────┐
                              │          │  Streaming   │
                              │          │  VBO         │
                              │          └───────┬──────┘
                              │                  │
                              └──────────┬───────┘
                                         │
                                         ▼
                                  ┌─────────────┐
                                  │   Batch     │
                                  │   Renderer  │
                                  └──────┬──────┘
                                         │
                                         ▼
                                  ┌─────────────┐
                                  │   GPU       │
                                  │   Display   │
                                  └─────────────┘
```

### Runtime Frame Flow

**Every Frame @ 60 FPS:**

1. **Game Logic Updates** (ECS)
   - Entity positions change
   - Animation states update
   - Physics applies forces (wind, trampling)

2. **Visibility Culling**
   - Frustum culling based on camera
   - Chunk-based visibility for tile world
   - Distance-based LOD selection

3. **Animation Updates** (Tier 3 only)
   - For each visible animated entity:
     - Update spline control points (deformation)
     - Check if entity is dirty (state changed)
     - Mark for re-tessellation if needed

4. **Tessellation Pass** (Tier 3 only)
   - Parallel tessellation of dirty entities
   - Output triangles to frame-local memory arena
   - Accumulate into batch buffers by material

5. **GPU Streaming** (Tier 3 only)
   - Upload streaming VBO data (`glBufferSubData` or mapped buffers)
   - Update texture atlases if needed (rare)

6. **Rendering Pass**
   - **Tier 1**: Draw textured quads (static backgrounds)
   - **Tier 2**: Draw cached geometry with transforms
   - **Tier 3**: Draw streaming batches
   - Minimize state changes, batch by material/texture

7. **Debug Overlay** (Development only)
   - Draw collision shapes
   - Visualize tessellation quality
   - Show performance metrics

## Integration with Existing Architecture

### ECS Integration

**Entity Components**:
```cpp
// Tier 1: Static Background (no entity needed, part of chunk data)

// Tier 2: Semi-Static Structure
struct VectorRenderComponent {
    AssetID svgAsset;           // Which SVG to render
    VectorMeshHandle mesh;      // Cached tessellated mesh
    Transform2D transform;      // Position, rotation, scale
    Color tint;                 // Color modulation
};

// Tier 3: Dynamic Animated Entity
struct AnimatedVectorComponent {
    AssetID svgAsset;           // Base SVG
    AnimationState animation;   // Current animation state
    SplineDeformation deform;   // Current deformation
    bool isDirty;               // Needs re-tessellation?
    uint32_t batchID;           // Which batch to render in
};

// Optional: Collision shape (both Tier 2 and 3)
struct VectorCollisionComponent {
    CollisionShapeHandle shape; // Simplified collision geometry
};
```

**Systems**:
```cpp
// Updates animation states, marks entities dirty
class AnimationUpdateSystem : public System {
    void Update(float deltaTime) override;
};

// Re-tessellates dirty entities, builds batches
class VectorTessellationSystem : public System {
    void Update(float deltaTime) override;
};

// Uploads batches to GPU, renders
class VectorRenderSystem : public System {
    void Update(float deltaTime) override;
};
```

### Renderer Integration

The vector graphics system integrates with the existing OpenGL renderer:

**New Components**:
- `VectorBatchRenderer` - Manages streaming VBOs and batched rendering
- `VectorTessellator` - Converts vector paths to triangles
- `VectorMeshCache` - Caches Tier 2 meshes
- `TextureAtlasManager` - Manages Tier 1 texture atlases

**Integration Points**:
```cpp
// In libs/renderer/
class Renderer {
    VectorBatchRenderer* m_vectorBatchRenderer;
    TextureAtlasManager* m_atlasManager;

    void RenderVectorGraphics(const Camera& camera) {
        // Tier 1: Static backgrounds
        RenderTileBackgrounds(camera);

        // Tier 2: Semi-static structures
        m_vectorBatchRenderer->RenderCachedMeshes(camera);

        // Tier 3: Dynamic entities
        m_vectorBatchRenderer->RenderStreamingBatches(camera);
    }
};
```

### Game-Systems Integration

**Tile Rendering**:
```cpp
// In libs/game-systems/chunks/
class ChunkRenderer {
    void RenderChunk(const Chunk& chunk) {
        // Tier 1: Render tile backgrounds from atlas
        for (auto& tile : chunk.tiles) {
            RenderStaticTileBackground(tile);
        }

        // Tier 2 & 3: ECS handles structures and entities
    }
};
```

**Animation System**:
```cpp
// In libs/game-systems/animation/ (or engine/animation/)
class VectorAnimationController {
    void UpdateGrassAnimation(Entity entity, float wind, float trampling);
    void UpdateTreeSway(Entity entity, float windSpeed, vec2 windDirection);
};
```

## Memory Architecture

### Memory Budget Breakdown

**Total Budget**: ~350 MB for vector rendering

| Tier | Purpose | Budget | Storage Type |
|------|---------|--------|--------------|
| Tier 1 | Texture atlases | ~200 MB | GPU textures (4 LODs × multiple atlases) |
| Tier 2 | Cached meshes | ~100 MB | GPU VBOs (persistent, rarely updated) |
| Tier 3 | Streaming buffers | ~50 MB | GPU VBOs (double/triple buffered, updated per frame) |
| Scratch | Tessellation temp | ~10 MB | CPU memory arena (per-frame, reset) |

### Cache Management

**Tier 1 - Static Atlases**:
- Pre-generated at asset import or first load
- Never evicted during gameplay
- Stored in `TextureAtlasManager`
- Multiple atlases for different tile types

**Tier 2 - Mesh Cache**:
```cpp
class VectorMeshCache {
    // Key: SVG asset hash + variation parameters
    // Value: GPU VBO handle + vertex count
    std::unordered_map<CacheKey, MeshData> m_cache;

    // LRU eviction when budget exceeded
    size_t m_maxCacheSize = 100 * 1024 * 1024; // 100 MB

    MeshData* GetOrTessellate(const CacheKey& key);
    void EvictLRU();
};
```

**Tier 3 - Streaming Buffers**:
```cpp
class StreamingVBO {
    // Double or triple buffering to avoid GPU stalls
    GLuint m_buffers[3];
    size_t m_currentBuffer = 0;
    size_t m_bufferSize = 16 * 1024 * 1024; // 16 MB per buffer

    // Persistent mapped buffers (OpenGL 4.4+) for zero-copy updates
    void* m_mappedMemory;

    void BeginFrame();
    void AppendGeometry(const Vertex* vertices, size_t count);
    void EndFrame(); // Upload to GPU
};
```

**Scratch Memory**:
```cpp
// Per-frame linear allocator for tessellation temporary data
class TessellationArena {
    MemoryArena m_arena;
    size_t m_arenaSize = 10 * 1024 * 1024; // 10 MB

    void* Allocate(size_t bytes) {
        return m_arena.Allocate(bytes);
    }

    void ResetFrame() {
        m_arena.Reset(); // Free all, zero cost
    }
};
```

## Performance Targets

### Target Metrics

| Metric | Target | Stretch Goal |
|--------|--------|--------------|
| Frame Rate | 60 FPS | 120 FPS |
| Animated Entities | 10,000 | 50,000 |
| Tessellation Time | <2ms/frame | <1ms/frame |
| GPU Draw Calls | <100/frame | <50/frame |
| Memory Usage | <350 MB | <250 MB |
| Chunk Load Time | <16ms | <8ms |

### Performance Budget Per Frame (16.67ms @ 60 FPS)

| Phase | Budget | Notes |
|-------|--------|-------|
| Game Logic | ~4ms | ECS updates, physics, AI |
| Animation Updates | ~1ms | Update spline deformations |
| Tessellation | ~2ms | Convert vectors → triangles |
| GPU Upload | ~1ms | Stream VBO updates |
| Rendering | ~6ms | GPU draw calls, shading |
| Other | ~2ms | Profiling, debug, overhead |

### Optimization Strategies

**Tessellation Bottleneck Mitigation**:
1. **Spatial Partitioning**: Only tessellate visible entities
2. **Dirty Tracking**: Only re-tessellate changed entities
3. **LOD**: Reduce triangle count for distant objects
4. **Multi-threading**: Parallel tessellation across CPU cores
5. **Adaptive Quality**: Reduce tessellation accuracy when over budget

**Draw Call Reduction**:
1. **Batching**: Group by material/texture
2. **Texture Atlases**: Reduce texture switches
3. **Instancing**: Identical objects (limited use case)
4. **Uber Shaders**: Minimize shader switches

**Memory Optimization**:
1. **Memory Arenas**: Eliminate per-frame allocations
2. **LRU Eviction**: Cache only recently used meshes
3. **Compression**: Quantize vertex data (half-floats, 16-bit indices)
4. **Streaming**: Upload only visible geometry

## Platform Considerations

### OpenGL Version

**Target**: OpenGL 3.3 Core Profile (widely supported on desktop)

**Features Used**:
- Vertex Array Objects (VAOs)
- Vertex Buffer Objects (VBOs)
- Uniform Buffer Objects (UBOs) - for batch transforms
- Multiple Render Targets (MRTs) - for deferred rendering (optional)
- Texture Arrays - for atlasing

**Optional (OpenGL 4.4+)**:
- Persistent Mapped Buffers - for zero-copy VBO streaming
- Multi-Draw Indirect - for advanced batching

**Not Used** (avoid for complexity):
- Tessellation Shaders (OpenGL 4.0+) - CPU tessellation is sufficient
- Compute Shaders (OpenGL 4.3+) - defer to Tier 4 (future)
- Geometry Shaders - usually slower than CPU preprocessing

### Platform-Specific Optimizations

**macOS**:
- Use persistent mapped buffers if available
- Profile Metal backend performance (MoltenVK translation)
- Test on integrated vs discrete GPUs

**Windows**:
- Leverage NVIDIA/AMD driver optimizations
- Test on various GPU generations
- Consider DirectX 11 backend (future)

**Linux**:
- Test on Mesa drivers (open source)
- Test on NVIDIA proprietary drivers
- Validate on various desktop environments

## Extensibility & Future Work

### Planned Extensions

1. **GPU Tessellation (Tier 4)**
   - If CPU tessellation becomes bottleneck
   - Implement Vello-style compute shader pipeline
   - Requires OpenGL 4.3+ (compute shaders)

2. **Advanced Shading**
   - Gradient fills (radial, linear)
   - SVG filters (blur, drop shadow)
   - Stroke patterns (dashed, dotted)

3. **Animation Compression**
   - Keyframe interpolation
   - Quantized spline data
   - Delta encoding for updates

4. **Procedural Generation**
   - Generate SVG assets at runtime
   - Combine primitive shapes
   - Noise-based deformation

### Research Topics

- **Signed Distance Fields**: For simple shapes, SDF rendering might outperform tessellation
- **Hybrid CPU/GPU**: Use GPU for simple shapes, CPU for complex
- **Neural Rendering**: AI-based upscaling/enhancement (experimental)

## Trade-Offs & Alternatives Considered

### Why Multi-Tier vs Single Approach?

**Considered**: Single rendering strategy for all objects

**Rejected Because**:
- Performance: Static tiles don't need per-frame tessellation
- Memory: Caching everything is wasteful
- Complexity: One-size-fits-all is actually more complex

**Chosen**: Multi-tier architecture
- Each tier optimized for its use case
- Clear performance characteristics
- Easier to optimize specific bottlenecks

### Why CPU Tessellation (Tier 3) vs GPU Tessellation Shaders?

**Considered**: Use OpenGL tessellation shaders for dynamic geometry

**Rejected Because**:
- Complexity: Tessellation shaders are harder to debug
- Flexibility: CPU code easier to modify
- Compatibility: Requires OpenGL 4.0+
- Performance: CPU tessellation is fast enough with batching

**Chosen**: CPU tessellation with batched GPU rendering
- Proven approach (Godot, Unity, Phaser)
- More control over triangle quality
- Easier to implement and optimize
- Can upgrade to GPU later if needed

### Why Not Full Vello Integration Now?

**Considered**: Use Vello renderer (GPU compute-centric)

**Rejected for Initial Implementation Because**:
- Complexity: Compute shader pipeline is advanced
- Maturity: Vello is cutting edge, still evolving
- Overkill: CPU approach likely sufficient for targets
- Integration: Rust FFI adds complexity

**Deferred to Tier 4**: Keep as future optimization if needed

## Related Documentation

- [tessellation-options.md](./tessellation-options.md) - Comparison of tessellation libraries
- [svg-parsing-options.md](./svg-parsing-options.md) - SVG parser comparison
- [rendering-backend-options.md](./rendering-backend-options.md) - Rendering approach comparison
- [batching-strategies.md](./batching-strategies.md) - GPU batching techniques
- [animation-system.md](./animation-system.md) - Spline deformation and animation details
- [collision-shapes.md](./collision-shapes.md) - Collision geometry generation
- [lod-system.md](./lod-system.md) - Level of detail implementation
- [memory-management.md](./memory-management.md) - Memory architecture details
- [performance-targets.md](./performance-targets.md) - Detailed performance requirements

## Revision History

- 2025-10-24: Initial architecture design based on research phase
