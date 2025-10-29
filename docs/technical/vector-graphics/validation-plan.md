# Vector Graphics Validation Plan

Created: 2025-10-27
Updated: 2025-10-29 (Added grass blade phases for Bezier curve validation)
Status: Active - Star phases complete, moving to grass blade phases

## Philosophy: Bottom-Up Validation

**Critical Insight:** The rendering pipeline is the risk, not data loading.

We must validate that we can **render and animate thousands of vector shapes at 60 FPS** before investing in asset pipelines, SVG parsing, or production features. SVG loading is trivial compared to proving the core rendering architecture works.

## Validation Progression

**Phase 0-3: Stars** ✅ - Validated basic polygon tessellation (straight edges)
**Phase 4-6: Grass Blades** 🔄 - Next step: Bezier curve tessellation (curved organic shapes)
**Phase 7: SVG Loading** - Final step: Real SVG file parsing

This progression moves from simple to complex:
1. **Stars**: Simple polygons with straight line segments (good first test)
2. **Grass Blades**: Organic shapes with Bezier curves (real game assets)
3. **SVG Files**: Production asset pipeline integration

### What We're Really Testing

**Stars (Phases 0-3):**
1. **Basic polygon tessellation** - Straight line segments only
2. **Batching effectiveness** - Can we minimize draw calls?
3. **Static rendering** - 10,000 shapes at 60 FPS

**Grass Blades (Phases 4-6):**
1. **Bezier curve tessellation** - Smooth organic curves
2. **Animation viability** - Real-time re-tessellation for swaying/bending
3. **Performance with curves** - Are curves significantly slower?
4. **Memory efficiency** - Curved shapes use more triangles

**SVG Loading (Phase 7):**
1. **SVG file parsing** - Real production asset pipeline
2. **Integration** - Loading, caching, resource handles

### What We're NOT Testing Yet

- Production features (LOD, culling, etc. - optimize after validation)
- Gradients, strokes, complex fills
- Multi-path SVG files

## Validation Phases

---

## COMPLETED: Star Phases (Basic Polygon Tessellation)

### Phase 0: Single Hardcoded Star ✅ COMPLETE

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

### Phase 1: 10,000 Static Stars ✅ COMPLETE

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

### Phase 2: 10,000 Animated Stars ✅ COMPLETE

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

### Phase 3: SVG Loading (Stars) ✅ COMPLETE

**Goal:** Replace hardcoded star data with SVG file loading.

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

## IN PROGRESS: Grass Blade Phases (Bezier Curve Tessellation)

### Phase 4: Single Grass Blade with Bezier Curves 🔄 CURRENT

**Goal:** Prove Bezier curve tessellation works with smooth organic shapes.

**Why Grass Blades?**
- Real game asset (grass tufts are ground decorations in design docs)
- Introduces curved shapes (not just straight line segments like stars)
- Perfect for testing animation (swaying, bending, trampling)
- Representative of organic vegetation shapes we'll use extensively

**Implementation:**
```cpp
// Define grass blade with Bezier curves
VectorPath grassBlade;
grassBlade.MoveTo(0, 0);                    // Base
grassBlade.CubicTo(1, 15, 2, 25, 3, 40);   // Left edge curve
grassBlade.LineTo(5, 40);                   // Tip
grassBlade.CubicTo(4, 25, 3, 15, 6, 0);    // Right edge curve
grassBlade.Close();

// Tessellate with curve flattening
TessellatedMesh mesh = Tessellate(grassBlade, tolerance=0.5f);

// Render via Primitive API
DrawTriangles(mesh.vertices, mesh.indices, mesh.indexCount);
```

**New Capability: Curve Flattening**
- Bezier curves must be converted to line segments before tessellation
- Adaptive subdivision based on curvature (fewer points on flat sections)
- Tolerance parameter controls quality vs triangle count trade-off

**Deliverables:**
- `libs/renderer/vector/curves.{h,cpp}` - Bezier curve flattening utilities
- Updated `Tessellator` to handle paths with curves
- `apps/ui-sandbox/scenes/vector_grass_scene.cpp` - Grass blade demo

**Metrics to Collect:**
- Curve flattening time (microseconds)
- Triangle count vs star (expect 2-3x more triangles)
- Tessellation time vs simple polygons
- Visual quality assessment

**Success Criteria:**
- ✅ Grass blade renders with smooth curves (no jagged edges)
- ✅ Curve flattening < 0.2ms for single blade
- ✅ Tessellation < 0.5ms for single blade
- ✅ Visual quality acceptable with tolerance=0.5

**Failure Modes:**
- Curve flattening too slow → Optimize algorithm, increase tolerance
- Visual quality poor → Decrease tolerance, improve curve subdivision
- Integration complexity → Simplify API, better documentation

**Time Estimate:** 6-8 hours

---

### Phase 5: 10,000 Static Grass Blades

**Goal:** Prove batching works with curved shapes at scale.

**Implementation:**
```cpp
// Tessellate grass blade ONCE (with curves)
TessellatedMesh grassBladeMesh = Tessellate(grassBladePath, tolerance=0.5f);

// Render 10,000 instances at different positions
for (int i = 0; i < 10000; i++) {
    Vec2 position = CalculateGridPosition(i);
    Color color = CalculateGreenVariation(i);  // Shades of green
    float rotation = CalculateRandomRotation(i);  // Varied orientation

    DrawMeshInstance(grassBladeMesh, position, rotation, color);
}
```

**Key Difference from Stars:**
- More triangles per blade (curves require more geometry)
- Performance test with realistic organic shapes
- Varied rotation (grass blades don't all face same direction)

**Deliverables:**
- Grid rendering with rotation support
- Color variation system (shades of green)
- Performance profiling with curved shapes

**Metrics to Collect:**
- Frame rate (target: 60 FPS)
- Triangle count (expect 2-3x more than stars)
- Draw calls (target: still < 100)
- GPU rendering time (budget: < 8ms with more triangles)
- Memory usage

**Success Criteria:**
- ✅ 60 FPS with 10,000 grass blades
- ✅ Draw calls < 100
- ✅ GPU time < 8ms (slightly higher budget due to more triangles)
- ✅ Memory usage acceptable

**Go/No-Go Decision:**
If performance significantly worse than stars (>2x slower), may need to:
- Reduce triangle count (increase tolerance)
- Simplify grass blade shape
- Reconsider curve complexity in production assets

**Time Estimate:** 1 day

---

### Phase 6: 10,000 Animated Grass Blades (Swaying)

**Goal:** Prove real-time curve-based animation works at scale.

**This is the CRITICAL validation for grass swaying mechanics.**

**Implementation:**
```cpp
// Each frame, for visible grass blades:
for (int i = 0; i < 10000; i++) {
    // Calculate wind influence
    float time = GetTime();
    float windPhase = i * 0.05f;  // Staggered swaying
    float bendAmount = sin(time * 2.0f + windPhase) * 0.3f;  // Gentle sway

    // Deform Bezier control points (bend grass blade)
    VectorPath deformedBlade = grassBladePath;
    ApplyWindBend(deformedBlade, bendAmount);

    // Re-tessellate deformed curved path
    TessellatedMesh mesh = Tessellate(deformedBlade, tolerance=0.5f);

    // Render
    DrawMesh(mesh, positions[i], colors[i]);
}
```

**Animation Types to Test:**
1. **Wind sway** - Gentle back-and-forth motion
2. **Bending** - Top of blade moves, base stays fixed
3. **Multiple frequencies** - Some blades sway faster than others

**Deliverables:**
- Wind deformation system for curves
- Bezier control point manipulation utilities
- Staggered animation system (not all blades sync)
- Memory arena integration for curve scratch space

**Metrics to Collect:**
- Frame rate (target: 60 FPS)
- Curve deformation time (target: < 0.5ms total)
- Re-tessellation time (target: < 3ms total for 10k blades)
- GPU upload time (target: < 1.5ms)
- GPU render time (target: < 8ms)

**Success Criteria:**
- ✅ 60 FPS with 10,000 animated grass blades
- ✅ Smooth swaying animation (looks natural)
- ✅ Tessellation < 3ms (0.3μs per blade average)
- ✅ No visual artifacts from curve deformation

**Critical Optimizations:**
- **Dirty tracking** - Only 20-30% of blades animate per frame (distant blades static)
- **LOD** - Reduce curve complexity for distant grass
- **Frustum culling** - Only tessellate visible blades
- **Curve tolerance** - Distant grass uses higher tolerance (fewer triangles)

**Failure Modes:**
- Tessellation too slow with curves → Multi-thread, simplify curves, GPU tessellation
- Memory allocations cause stutter → Fix arena usage
- Visual quality poor with animation → Improve curve deformation algorithm

**Go/No-Go Decision:**
**This is the FINAL validation for organic vector graphics.** If we can hit 60 FPS with 10,000 animated curved shapes, the entire vegetation system is proven viable. If not:
- Reduce grass blade complexity (simpler curves)
- Pre-bake animation frames (sprite sheets)
- GPU compute tessellation (more complex, but proven in other engines)

**Time Estimate:** 2-3 days

---

### Phase 7: SVG Loading (Grass Blades)

**Goal:** Load grass blades from SVG files with Bezier curves.

**Implementation:**
```cpp
// Load grass blade from SVG file (with curves)
VectorPath* grassPath = LoadSVG("assets/decorations/grass_blade_01.svg");

// Should contain cubic Bezier curves from artist
// SVG <path d="M 0,0 C 1,15 2,25 3,40 L 5,40 C 4,25 3,15 6,0 Z" />

// Everything else stays the same
TessellatedMesh mesh = Tessellate(grassPath, tolerance=0.5f);
DrawMesh(mesh, position, color);
```

**Deliverables:**
- SVG curve parsing (cubic Bezier support)
- Path command parser (M, L, C, Q, Z)
- Multiple grass blade SVG variants
- Asset loading integration

**Metrics to Collect:**
- SVG parse time with curves (target: < 20ms per file)
- Curve extraction accuracy
- No performance regression vs hardcoded curves

**Success Criteria:**
- ✅ SVG grass blades load correctly
- ✅ Curves match hardcoded version visually
- ✅ No performance regression
- ✅ Multiple grass variants work

**Time Estimate:** 1-2 days

---

## Performance Budgets (Revisited)

Based on validation results, these budgets may be refined:

| System | Budget (Stars) | Phase Validated | Budget (Grass) | Phase Validated |
|--------|----------------|-----------------|----------------|-----------------|
| Tessellation | < 2ms | Phase 2 ✅ | < 3ms | Phase 6 (target) |
| Curve Flattening | N/A | N/A | < 0.5ms | Phase 4-6 (target) |
| Animation Updates | < 1ms | Phase 2 ✅ | < 1ms | Phase 6 (target) |
| GPU Upload | < 1ms | Phase 2 ✅ | < 1.5ms | Phase 6 (target) |
| GPU Rendering | < 6ms | Phase 1, 2 ✅ | < 8ms | Phase 5-6 (target) |
| Draw Calls | < 100 | Phase 1 ✅ | < 100 | Phase 5-6 (target) |
| Memory (Vector System) | ~350 MB | Phase 1, 2 ✅ | ~350 MB | Phase 5-6 (target) |

**Notes:**
- Grass blades have higher budgets due to more triangles (curves)
- Curve flattening is new overhead not present in star phases
- GPU rendering budget increased 6ms→8ms to account for 2-3x more triangles

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

**Vector graphics with SIMPLE POLYGONS is PROVEN VIABLE:**
- ✅ Phase 0-3 complete (stars with straight edges)
- ✅ 60 FPS with 10,000 static and animated stars
- ✅ All performance budgets met

**Vector graphics with BEZIER CURVES is PROVEN VIABLE if:**
- ⏳ Phase 4 completes (single grass blade with curves)
- ⏳ Phase 5 achieves 60 FPS with 10,000 static grass blades
- ⏳ Phase 6 achieves 60 FPS with 10,000 animated grass blades
- ⏳ All performance budgets met (with curve-adjusted targets)

**Complete validation achieved when:**
- ✅ All star phases (0-3) complete
- ⏳ All grass phases (4-6) complete
- ⏳ SVG loading (Phase 7) complete

At that point, we can confidently invest in:
- Production asset pipeline
- SVG artist workflow (with Bezier curve support)
- Advanced features (LOD, culling, gradients, strokes, etc.)
- Full game integration with organic vegetation

## Related Documentation

- [architecture.md](./architecture.md) - Four-tier rendering system
- [performance-targets.md](./performance-targets.md) - Detailed performance budgets
- [tessellation-options.md](./tessellation-options.md) - Library comparison
- [batching-strategies.md](./batching-strategies.md) - Draw call optimization

## Timeline

**Completed (Stars):**
- **Phase 0**: 4-6 hours ✅
- **Phase 1**: 1-2 days ✅
- **Phase 2**: 2-3 days ✅
- **Phase 3**: 1 day ✅

**In Progress (Grass Blades):**
- **Phase 4**: 6-8 hours (Bezier curve implementation) 🔄
- **Phase 5**: 1 day (static curved shapes at scale)
- **Phase 6**: 2-3 days (animated curved shapes - critical validation)
- **Phase 7**: 1-2 days (SVG loading with curves)

**Total:**
- Stars: ~1 week ✅ COMPLETE
- Grass: ~1 week ⏳ IN PROGRESS
- **Overall**: ~2 weeks to prove full vector graphics viability (straight + curved)
