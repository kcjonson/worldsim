# Vector Graphics Validation Plan

Created: 2025-10-27
Status: Active - Phase 0 in progress

## Philosophy: Bottom-Up Validation

**Critical Insight:** The rendering pipeline is the risk, not data loading.

We must validate that we can **render and animate thousands of vector shapes at 60 FPS** before investing in asset pipelines, SVG parsing, or production features. SVG loading is trivial compared to proving the core rendering architecture works.

### What We're Really Testing

1. **Tessellation Performance**: Can we convert paths to triangles fast enough? (<2ms for 10k shapes)
2. **Batching Effectiveness**: Can we minimize draw calls? (<100 per frame)
3. **Animation Viability**: Can we re-tessellate deformed shapes in real-time? (60 FPS)
4. **Memory Efficiency**: Does it fit in budget? (~350 MB)

### What We're NOT Testing Yet

- SVG file parsing (data loading is orthogonal to rendering)
- Asset pipelines (can be added later)
- Production features (LOD, culling, etc. - optimize after validation)

## Validation Phases

### Phase 0: Single Hardcoded Star (CURRENT)

**Goal:** Prove the basic pipeline works end-to-end.

**Implementation:**
```cpp
// Define a 5-pointed star in code
Vec2 starVertices[] = {
    {0, -50}, {15, -15}, {50, -10}, {20, 10}, {30, 45},
    {0, 25}, {-30, 45}, {-20, 10}, {-50, -10}, {-15, -15}
};

// Tessellate to triangles
TessellatedMesh mesh = Tessellate(starVertices, 10);

// Render via Primitive API
DrawTriangles(mesh.vertices, mesh.indices, mesh.indexCount);
```

**Deliverables:**
- `libs/renderer/vector/path.h` - VectorPath data structure
- `libs/renderer/vector/tessellator.{h,cpp}` - Tessellation wrapper (libtess2 or earcut)
- `apps/ui-sandbox/demos/vector_star_demo.cpp` - Demo application

**Metrics to Collect:**
- Tessellation time (microseconds)
- Triangle count output
- Render time (GPU query)
- Frame rate

**Success Criteria:**
- ✅ Star renders correctly on screen
- ✅ Tessellation completes in < 0.1ms for simple star
- ✅ No crashes, no visual artifacts
- ✅ Clear understanding of data flow

**Failure Modes:**
- Tessellation library doesn't integrate cleanly → Try different library
- Performance unacceptable → Try simpler algorithm or different approach
- Visual quality poor → Adjust tessellation quality settings

**Time Estimate:** 4-6 hours

---

### Phase 1: 10,000 Static Stars

**Goal:** Prove batching works at scale.

**Implementation:**
```cpp
// Tessellate star ONCE
TessellatedMesh starMesh = Tessellate(starVertices, 10);

// Render 10,000 instances at different positions
for (int i = 0; i < 10000; i++) {
    Vec2 position = CalculateGridPosition(i);
    Color color = CalculateColorVariation(i);

    // Draw using instancing or batched submission
    DrawMeshInstance(starMesh, position, color);
}
```

**Deliverables:**
- Batch renderer for vector meshes
- Instancing support (if applicable)
- Grid layout system for test rendering
- Performance profiling integration

**Metrics to Collect:**
- Frame rate (should be 60 FPS)
- Draw calls per frame (target: < 50)
- GPU rendering time (target: < 6ms)
- Memory usage (vertex buffers)
- CPU usage (should be minimal)

**Success Criteria:**
- ✅ 60 FPS with 10,000 stars on screen
- ✅ Draw calls < 100 (ideally < 50)
- ✅ GPU time < 6ms
- ✅ Memory usage acceptable

**Failure Modes:**
- Draw calls too high (>100) → Improve batching strategy
- GPU time too high → Reduce triangle count, optimize shaders
- Frame rate drops → Profile and identify bottleneck
- Memory usage excessive → Reduce precision, compress data

**Go/No-Go Decision:**
If this phase fails to meet performance targets, the entire vector graphics approach is questionable. Consider fallback strategies:
- Pre-rasterized sprites (Tier 1 only)
- Simpler shapes with fewer triangles
- Reduce entity count targets

**Time Estimate:** 1-2 days

---

### Phase 2: 10,000 Animated Stars

**Goal:** Prove real-time tessellation and animation works at scale.

**Implementation:**
```cpp
// Each frame, for all visible stars:
for (int i = 0; i < 10000; i++) {
    // Copy base star path
    Vec2 deformedPath[10];
    memcpy(deformedPath, starVertices, sizeof(starVertices));

    // Apply deformation (simulate swaying, bending)
    float time = GetTime();
    float phaseOffset = i * 0.1f;
    ApplyWaveDeformation(deformedPath, 10, time + phaseOffset);

    // Re-tessellate deformed path
    TessellatedMesh mesh = Tessellate(deformedPath, 10);

    // Render
    DrawMesh(mesh, positions[i], colors[i]);
}
```

**Deliverables:**
- Path deformation utilities (wave, bend, rotate)
- Streaming VBO system (efficiently upload dynamic geometry)
- Dirty tracking (only re-tessellate changed shapes)
- Memory arena integration (tessellation scratch space)

**Metrics to Collect:**
- Frame rate (target: 60 FPS)
- Animation update time (target: < 1ms)
- Tessellation time (target: < 2ms for all 10k)
- GPU upload time (target: < 1ms)
- GPU render time (target: < 6ms)
- Memory allocations per frame

**Success Criteria:**
- ✅ 60 FPS with 10,000 animated stars
- ✅ Smooth animation (no stuttering)
- ✅ Tessellation < 2ms total (0.2μs per shape average)
- ✅ Memory usage stable (no leaks, arenas working)

**Critical Optimizations:**
- **Dirty Tracking**: Only 30-50% of shapes animate per frame
- **LOD**: Distant stars use fewer vertices
- **Frustum Culling**: Only tessellate visible stars
- **Memory Arenas**: Zero allocations per frame (use scratch buffers)

**Failure Modes:**
- Tessellation too slow → Multi-thread tessellation, simplify shapes
- Frame rate drops → Reduce entity count, improve dirty tracking
- Memory allocations cause stutter → Fix arena usage, eliminate malloc

**Go/No-Go Decision:**
This is the **critical validation**. If we can hit 60 FPS with 10,000 animated shapes, the vector graphics approach is proven viable. If not:
- Consider GPU tessellation (Tier 4, compute shaders)
- Reduce scope (fewer entities, simpler shapes)
- Fallback to pre-animated sprite sheets

**Time Estimate:** 2-3 days

---

### Phase 3: SVG Loading

**Goal:** Replace hardcoded data with SVG file loading.

**Implementation:**
```cpp
// Load star from SVG file instead of hardcoded vertices
VectorPath* starPath = LoadSVG("assets/test/star.svg");

// Everything else stays the same
TessellatedMesh mesh = Tessellate(starPath->vertices, starPath->vertexCount);
DrawMesh(mesh, position, color);
```

**Deliverables:**
- NanoSVG integration wrapper
- SVG → VectorPath converter
- Simple test SVG assets (star, circle, grass blade)
- Asset loading via resource handles

**Metrics to Collect:**
- SVG parse time (should be < 10ms per file)
- Memory usage for parsed SVGs
- No performance regression vs hardcoded data

**Success Criteria:**
- ✅ SVG loads correctly
- ✅ Rendered output identical to hardcoded version
- ✅ No performance regression
- ✅ Clean integration with resource handle system

**Failure Modes:**
- NanoSVG doesn't support needed features → Use different library or custom parser
- Parse time too slow → Cache parsed data, load asynchronously
- Path data format mismatch → Write conversion layer

**Time Estimate:** 1 day

---

## Performance Budgets (Revisited)

Based on validation results, these budgets may be refined:

| System | Budget | Phase Validated |
|--------|--------|-----------------|
| Tessellation | < 2ms | Phase 2 |
| Animation Updates | < 1ms | Phase 2 |
| GPU Upload | < 1ms | Phase 2 |
| GPU Rendering | < 6ms | Phase 1, 2 |
| Draw Calls | < 100 | Phase 1 |
| Memory (Vector System) | ~350 MB | Phase 1, 2 |

## Data Structures

### VectorPath

```cpp
struct VectorPath {
    Vec2* vertices;
    int vertexCount;
    bool isClosed;
};
```

### TessellatedMesh

```cpp
struct TessellatedMesh {
    float* vertices;      // Interleaved: x, y (could add color, UV later)
    uint16_t* indices;    // Triangle indices
    int vertexCount;
    int indexCount;
};
```

### Deformation State

```cpp
struct DeformationState {
    float time;           // Animation time
    float amplitude;      // Deformation strength
    float frequency;      // Wave frequency
    Vec2 direction;       // Deformation direction
};
```

## Tessellation Library Decision

**Options:**
1. **libtess2** - Industry standard, robust, handles complex polygons
2. **earcut.hpp** - Header-only, fast for simple polygons, modern C++
3. **Custom** - Simple ear-clipping for convex/simple shapes only

**Recommendation (pending Phase 0 testing):**
- Start with **earcut.hpp** (header-only, easy integration)
- If robustness issues → Switch to **libtess2**
- If performance issues → Custom implementation for simple shapes

## Profiling Strategy

### Per-Frame Profiling

```cpp
void VectorStarDemo::Render() {
    SCOPED_TIMER("Vector Rendering Total");

    {
        SCOPED_TIMER("Animation Update");
        UpdateAnimations();
    }

    {
        SCOPED_TIMER("Tessellation");
        TessellateShapes();
    }

    {
        SCOPED_TIMER("GPU Upload");
        UploadDynamicGeometry();
    }

    {
        SCOPED_TIMER("GPU Render");
        RenderBatches();
    }
}
```

### Metrics Collection

```cpp
struct VectorRenderingMetrics {
    float tessellationTime;
    float animationTime;
    float uploadTime;
    float renderTime;
    int triangleCount;
    int drawCalls;
    int animatedShapeCount;
};
```

Stream to debug server for real-time visualization in developer client.

## Fallback Strategies

If validation fails at any phase:

### If Phase 0 Fails (Single Shape)
- **Problem:** Tessellation doesn't work or integrate cleanly
- **Fallback:** Try different library, or use pre-tessellated data from tools

### If Phase 1 Fails (10k Static)
- **Problem:** Batching insufficient, too many draw calls
- **Fallback:**
  - Pre-rasterize all static shapes (Tier 1 only)
  - Reduce entity count targets
  - Use sprite atlases instead of vectors

### If Phase 2 Fails (10k Animated)
- **Problem:** Real-time tessellation too slow
- **Fallback:**
  - GPU tessellation (Tier 4, compute shaders)
  - Pre-baked animation frames (sprite sheets)
  - Reduce animated entity count
  - Simplify shapes (fewer vertices)

### If All Phases Fail
- **Abandon vector graphics approach**
- **Pivot to traditional sprite-based rendering**
- **Redesign game art pipeline**

## Success Definition

**Vector graphics is PROVEN VIABLE if:**
- ✅ Phase 0 completes successfully
- ✅ Phase 1 achieves 60 FPS with 10,000 static shapes
- ✅ Phase 2 achieves 60 FPS with 10,000 animated shapes
- ✅ All performance budgets met

At that point, we can confidently invest in:
- Production asset pipeline
- SVG artist workflow
- Advanced features (LOD, culling, gradients, etc.)
- Full game integration

## Related Documentation

- [architecture.md](./architecture.md) - Four-tier rendering system
- [performance-targets.md](./performance-targets.md) - Detailed performance budgets
- [tessellation-options.md](./tessellation-options.md) - Library comparison
- [batching-strategies.md](./batching-strategies.md) - Draw call optimization

## Timeline

- **Phase 0**: 4-6 hours (current session)
- **Phase 1**: 1-2 days
- **Phase 2**: 2-3 days
- **Phase 3**: 1 day

**Total**: ~1 week to prove vector graphics viability
