# Tessellation Options - Comparative Analysis

Created: 2025-10-24
Last Updated: 2025-10-24
Status: Research

## Overview

Tessellation is the process of converting vector shapes (defined by paths, curves, and points) into triangles that can be rendered by the GPU. This is a **critical component** of the vector graphics system, as all vector shapes must be tessellated before rendering.

**Two Sub-Problems:**
1. **Polygon Tessellation**: Convert arbitrary polygons (with holes) → triangles
2. **Curve Conversion**: Convert Bezier curves → line segments (polylines)

This document analyzes libraries and approaches for both problems.

## Requirements

### Functional Requirements
- **Arbitrary Polygons**: Handle simple and complex polygons
- **Holes Support**: Polygons with holes (e.g., donut shapes)
- **Self-Intersections**: Robust handling of edge cases
- **Bezier Curves**: Support cubic and quadratic Bezier curves
- **Adaptive Quality**: Variable tessellation detail based on screen size/zoom
- **Correct Winding**: Respect fill rules (even-odd, non-zero)

### Performance Requirements
- **Tessellation Budget**: <2ms per frame for all dynamic entities
- **Target**: Tessellate 10,000 simple shapes per frame
- **Memory**: Minimal temporary allocations (use memory arenas)
- **Thread-Safe**: Must support parallel tessellation (multi-threading)

### Integration Requirements
- **C/C++ Compatible**: Must integrate with C++20 codebase
- **License**: Permissive (MIT, BSD, zlib, public domain)
- **Header-Only Preferred**: Easier integration
- **No External Dependencies**: Minimize dependency tree
- **Cross-Platform**: macOS, Windows, Linux support

## Polygon Tessellation Options

### Option A: libtess2

#### Overview
libtess2 is the industry-standard polygon tessellation library, extracted from the OpenGL reference implementation (SGI's GLU library). It's a battle-tested C library used in production by many projects.

**Repository**: https://github.com/memononen/libtess2
**License**: SGI Free Software License B 2.0 (permissive, similar to MIT)
**Language**: C (C89)
**Dependencies**: None

#### Pros
- **Battle-Tested**: Decades of production use, extremely robust
- **Arbitrary Polygons**: Handles complex polygons, self-intersections, holes
- **Winding Rules**: Supports even-odd and non-zero fill rules
- **Well-Documented**: Good API documentation and examples
- **Industry Standard**: Used by NanoVG, many game engines
- **No Dependencies**: Pure C, zero external dependencies

#### Cons
- **C API**: Requires C++ wrapper for RAII and type safety
- **Older Code Style**: C89 code, not modern C++
- **Performance**: Slower than specialized algorithms for simple polygons
- **Memory Management**: Manual memory management required
- **Not Header-Only**: Requires compilation and linking

#### Technical Details

**Performance Characteristics**:
- Asymptotic complexity: O(n log n) for n vertices
- Real-world: ~0.1-0.5ms for 100-vertex polygon on modern CPU
- Slower than Earcut for simple polygons, but handles complex cases

**Memory Footprint**:
- Allocates temporary memory for mesh data structures
- Memory usage: ~O(n) for n vertices
- Requires custom allocator integration for arena support

**Integration Complexity**:
- Low-Medium: C library, straightforward API
- Requires wrapper for C++ integration
- Need to handle memory management carefully

**Platform Compatibility**:
- Excellent: Pure C89, compiles everywhere
- No platform-specific code

**API Example**:
```cpp
// Basic usage (simplified)
TESStesselator* tess = tessNewTess(nullptr);
tessAddContour(tess, 2, vertices, stride, vertexCount);
tessTesselate(tess, TESS_WINDING_ODD, TESS_POLYGONS, 3, 2, nullptr);

const float* verts = tessGetVertices(tess);
const int* elems = tessGetElements(tess);
int nverts = tessGetVertexCount(tess);
int nelems = tessGetElementCount(tess);

// Use verts/elems for rendering...

tessDeleteTess(tess);
```

#### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Complex Polygons | ✅ Excellent | Handles all cases robustly |
| Performance | ⚠️ Good | Slower than Earcut, but acceptable |
| Thread-Safety | ⚠️ Manual | Need separate instance per thread |
| Memory Arenas | ⚠️ Possible | Requires custom allocator |
| Integration | ✅ Good | C API, well-documented |

### Option B: Earcut Algorithm (mapbox/earcut.hpp)

#### Overview
Earcut is a modern, fast polygon tessellation algorithm specifically designed for simple polygons. Originally written in JavaScript, it has been ported to C++ as a header-only library by Mapbox.

**Repository**: https://github.com/mapbox/earcut.hpp
**License**: ISC (permissive, similar to MIT)
**Language**: C++11 (header-only)
**Dependencies**: None

#### Pros
- **Header-Only**: Drop-in integration, no compilation needed
- **Fast**: 2-5x faster than libtess2 for simple polygons
- **Modern C++**: Clean C++11 API, STL-compatible
- **Minimal**: Small codebase (~500 lines), easy to audit
- **Zero Dependencies**: Only uses STL
- **Actively Maintained**: Mapbox actively maintains it
- **Well-Tested**: Extensive test suite, fuzzing

#### Cons
- **Simple Polygons Only**: Cannot handle self-intersections robustly
- **Limited Winding Rules**: No explicit winding rule support
- **Complex Holes**: May struggle with complex hole configurations
- **Less Battle-Tested**: Newer than libtess2 (but still production-proven)
- **No C API**: C++ only (not an issue for us)

#### Technical Details

**Performance Characteristics**:
- Asymptotic complexity: O(n log n) average, O(n²) worst case
- Real-world: ~0.02-0.1ms for 100-vertex simple polygon
- **2-5x faster than libtess2** for common cases
- Optimized for GIS/mapping use cases (many simple polygons)

**Memory Footprint**:
- Very small: allocates minimal temporary data
- Uses STL containers (can use custom allocators)
- Memory usage: ~O(n) for n vertices

**Integration Complexity**:
- Very Low: Header-only, modern C++ API
- Drop into project, #include and use
- Templated for flexibility

**Platform Compatibility**:
- Excellent: Header-only C++11, STL only
- No platform-specific code

**API Example**:
```cpp
#include <earcut.hpp>

// Vertices: array of {x, y} points
std::vector<std::array<double, 2>> polygon = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};

// Earcut expects nested arrays: outer ring, then holes
std::vector<std::vector<std::array<double, 2>>> polygons = {polygon};

// Tessellate
std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygons);

// indices contains triangle indices (every 3 = 1 triangle)
```

#### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Complex Polygons | ⚠️ Limited | Simple polygons only |
| Performance | ✅ Excellent | 2-5x faster than libtess2 |
| Thread-Safety | ✅ Excellent | Pure function, no global state |
| Memory Arenas | ✅ Good | Can use custom STL allocators |
| Integration | ✅ Excellent | Header-only, clean API |

### Option C: Poly2Tri

#### Overview
Poly2Tri is a 2D constrained Delaunay triangulation library. It's an alternative approach to polygon tessellation, using Delaunay triangulation instead of ear clipping or sweep-line algorithms.

**Repository**: https://github.com/jhasse/poly2tri (C++ version)
**License**: BSD 3-Clause
**Language**: C++
**Dependencies**: None

#### Pros
- **Constrained Delaunay**: Produces high-quality triangulations
- **C++ API**: Modern C++ interface
- **No Dependencies**: Self-contained
- **Good for Certain Shapes**: Excellent for convex polygons
- **Triangle Quality**: Better-shaped triangles than ear clipping

#### Cons
- **Performance**: Slower than Earcut, comparable to libtess2
- **Complexity**: More complex algorithm internally
- **Robustness Issues**: Can fail on degenerate cases
- **Limited Maintenance**: Less actively maintained
- **Not Header-Only**: Requires compilation
- **Simple Polygons Only**: Limited hole support

#### Technical Details

**Performance Characteristics**:
- Asymptotic complexity: O(n log n) average
- Real-world: Similar to libtess2 (~0.1-0.4ms for 100 vertices)
- Slower than Earcut
- Better triangle quality (useful for some applications, not critical for us)

**Memory Footprint**:
- Moderate: Delaunay algorithm requires more temporary data
- Memory usage: O(n) to O(n log n)

**Integration Complexity**:
- Medium: Requires compilation and linking
- C++ API is straightforward
- Some setup required

**Platform Compatibility**:
- Good: C++ with no platform-specific code
- May have build system quirks

**API Example**:
```cpp
#include <poly2tri/poly2tri.h>

std::vector<p2t::Point*> polyline;
for (auto& pt : points) {
    polyline.push_back(new p2t::Point(pt.x, pt.y));
}

p2t::CDT* cdt = new p2t::CDT(polyline);
cdt->Triangulate();

std::vector<p2t::Triangle*> triangles = cdt->GetTriangles();
// Use triangles...

// Cleanup (manual memory management)
delete cdt;
for (auto* pt : polyline) delete pt;
```

#### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Complex Polygons | ⚠️ Limited | Simple polygons, limited holes |
| Performance | ⚠️ Moderate | Slower than Earcut, similar to libtess2 |
| Thread-Safety | ⚠️ Manual | Need instance per thread |
| Memory Arenas | ❌ Difficult | Uses new/delete internally |
| Integration | ⚠️ Moderate | Requires compilation |

### Option D: No Library - Custom Ear Clipping

#### Overview
Implement a custom ear clipping algorithm from scratch. Ear clipping is a simple, well-understood algorithm suitable for simple polygons.

**References**:
- David Eberly, "Triangulation by Ear Clipping" (2002)
- Wikipedia: https://en.wikipedia.org/wiki/Polygon_triangulation#Ear_clipping_method

#### Pros
- **Full Control**: Complete control over implementation
- **Minimal Dependencies**: Zero external libraries
- **Optimized for Use Case**: Can optimize specifically for game assets
- **Learning Opportunity**: Understand tessellation deeply
- **No Licensing Concerns**: Own code, no external licenses
- **Tailored Memory Management**: Direct arena allocator integration

#### Cons
- **Development Time**: Significant implementation effort
- **Robustness**: Edge cases are tricky (self-intersections, degeneracies)
- **Performance**: Unlikely to beat optimized libraries initially
- **Maintenance Burden**: Must maintain and debug ourselves
- **Reinventing the Wheel**: Solving a solved problem

#### Technical Details

**Algorithm**:
```
Ear Clipping Algorithm (O(n²) naive, O(n log n) optimized):

1. Find all "ears" (triangles that can be cut off)
2. Cut off an ear (remove vertex, add triangle)
3. Update ear list for affected vertices
4. Repeat until 3 vertices remain

Ear definition:
  - Triangle formed by (prev, current, next) vertices
  - No other polygon vertices inside the triangle
  - Triangle has correct winding (CCW for outer, CW for holes)
```

**Performance Characteristics**:
- Naive: O(n²) - check all vertices for each ear
- Optimized: O(n log n) - use spatial index (grid, quadtree) for point-in-triangle tests
- Real-world: Likely 0.1-0.3ms for 100 vertices (after optimization)

**Memory Footprint**:
- Minimal: Linked list of vertices, ear list
- Can use memory arenas directly
- No external allocations needed

**Integration Complexity**:
- High: Must implement from scratch
- Testing: Extensive test cases needed for robustness
- Debugging: Edge cases can be subtle

**Implementation Estimate**:
- Core algorithm: ~200-300 lines of C++
- Robustness (edge cases): +200-400 lines
- Optimization (spatial index): +100-200 lines
- Testing: Extensive test suite needed
- **Total effort**: 2-4 days of focused work

**Example Pseudocode**:
```cpp
class EarClipper {
    struct Vertex {
        vec2 pos;
        Vertex* prev;
        Vertex* next;
        bool isEar;
    };

    std::vector<Triangle> Tessellate(const std::vector<vec2>& polygon) {
        // Build linked list of vertices
        Vertex* head = BuildVertexList(polygon);

        // Find all ears
        UpdateEars(head);

        std::vector<Triangle> triangles;
        while (VertexCount(head) > 3) {
            // Find an ear
            Vertex* ear = FindEar(head);

            // Cut off ear (add triangle)
            triangles.push_back({ear->prev->pos, ear->pos, ear->next->pos});

            // Remove vertex from list
            RemoveVertex(ear);

            // Update ears for neighbors
            UpdateEars(ear->prev);
            UpdateEars(ear->next);
        }

        // Add final triangle
        triangles.push_back({head->pos, head->next->pos, head->next->next->pos});

        return triangles;
    }

    bool IsEar(const Vertex* v) {
        Triangle tri = {v->prev->pos, v->pos, v->next->pos};

        // Check winding (must be CCW)
        if (!IsCounterClockwise(tri)) return false;

        // Check if any other vertices are inside
        for (Vertex* other = head; other != nullptr; other = other->next) {
            if (other == v || other == v->prev || other == v->next) continue;
            if (PointInTriangle(other->pos, tri)) return false;
        }

        return true;
    }
};
```

#### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Complex Polygons | ⚠️ Depends | Can handle holes with preprocessing |
| Performance | ⚠️ Depends | Likely good with optimization |
| Thread-Safety | ✅ Excellent | Can design for parallelism |
| Memory Arenas | ✅ Excellent | Direct integration |
| Integration | ⚠️ High Effort | Must implement from scratch |

## Curve Conversion (Bezier → Polyline)

All options above handle polygon tessellation, but SVG paths contain **Bezier curves** that must be converted to line segments first.

### Bezier Curve Flattening Options

#### Option A: De Casteljau's Algorithm (Custom Implementation)

**Overview**: Recursive subdivision of Bezier curves until flat enough.

**Pros**:
- Simple to implement (~50 lines)
- Numerically stable
- Adaptive (more points where curve is sharper)
- Full control over quality

**Cons**:
- Must implement ourselves
- Recursive (can use iterative variant)

**Implementation**:
```cpp
void FlattenBezier(vec2 p0, vec2 p1, vec2 p2, vec2 p3,
                   float tolerance, std::vector<vec2>& output) {
    // Check if curve is flat enough (distance from line)
    if (IsFlat(p0, p1, p2, p3, tolerance)) {
        output.push_back(p3); // Add endpoint
        return;
    }

    // Subdivide curve at t=0.5 using De Casteljau
    vec2 m01 = (p0 + p1) * 0.5f;
    vec2 m12 = (p1 + p2) * 0.5f;
    vec2 m23 = (p2 + p3) * 0.5f;
    vec2 m012 = (m01 + m12) * 0.5f;
    vec2 m123 = (m12 + m23) * 0.5f;
    vec2 m0123 = (m012 + m123) * 0.5f;

    // Recurse on both halves
    FlattenBezier(p0, m01, m012, m0123, tolerance, output);
    FlattenBezier(m0123, m123, m23, p3, tolerance, output);
}
```

**Recommendation**: **Implement this ourselves** - it's simple, well-understood, and gives full control.

#### Option B: Use SVG Parser's Built-In Flattening

**Overview**: Some SVG libraries (LunaSVG, NanoSVG) can flatten curves to polylines.

**Pros**:
- Zero implementation effort
- Already integrated with SVG parsing
- Tested

**Cons**:
- Less control over quality/tolerance
- Tied to specific SVG library
- May not be optimized for real-time

**Recommendation**: Possible if using a library that provides it, but custom implementation is better for control.

#### Option C: Mathematically Evaluate Bezier Curves

**Overview**: Evaluate Bezier curve at uniform `t` intervals: `B(t) = ...` formula.

**Pros**:
- Very simple (just plug in formula)

**Cons**:
- Non-adaptive (wastes points on flat sections)
- Poor quality (misses sharp corners)
- Not recommended

**Recommendation**: **Avoid** - adaptive subdivision is superior.

## Comparison Matrix

### Polygon Tessellation

| Criteria | libtess2 | Earcut (mapbox) | Poly2Tri | Custom Ear Clip |
|----------|----------|-----------------|----------|-----------------|
| **Performance** | Good (0.1-0.5ms) | Excellent (0.02-0.1ms) | Moderate (0.1-0.4ms) | Good (0.1-0.3ms*) |
| **Complex Polygons** | ✅ Excellent | ⚠️ Simple only | ⚠️ Limited | ⚠️ Depends |
| **Holes Support** | ✅ Yes | ✅ Yes | ⚠️ Limited | ✅ Yes* |
| **Self-Intersections** | ✅ Handles | ❌ No | ❌ No | ❌ No* |
| **Thread-Safety** | ⚠️ Manual | ✅ Yes | ⚠️ Manual | ✅ Yes* |
| **Integration** | ⚠️ C library | ✅ Header-only | ⚠️ Compile | ⚠️ Implement |
| **Memory Arenas** | ⚠️ Custom alloc | ✅ STL allocators | ❌ Difficult | ✅ Direct |
| **Dependencies** | None | None | None | None |
| **License** | SGI (permissive) | ISC (permissive) | BSD 3-Clause | N/A (own code) |
| **Maintenance** | Stable (infrequent) | Active | Low | Own |
| **Code Size** | Medium (~3k lines) | Small (~500 lines) | Medium (~2k lines) | Small (~500 lines*) |
| **Battle-Tested** | ✅✅✅ Decades | ✅✅ Production | ✅ Some | ❌ New |

\* = Estimated/depends on implementation quality

### Bezier Curve Flattening

| Criteria | Custom (De Casteljau) | SVG Library Built-In | Uniform Sampling |
|----------|----------------------|---------------------|------------------|
| **Performance** | Excellent (adaptive) | Good | Good |
| **Quality** | Excellent (adaptive) | Good | Poor |
| **Control** | Full | Limited | Full |
| **Implementation** | ~50 lines | Zero | ~10 lines |
| **Recommendation** | ✅ **Implement** | ⚠️ If available | ❌ Avoid |

## Open Questions

1. **Do our SVG assets contain self-intersecting polygons?**
   - If no: Earcut is likely sufficient
   - If yes: libtess2 required

2. **What is the complexity distribution of our polygons?**
   - Simple (3-20 vertices): Earcut is fastest
   - Complex (50-200 vertices): libtess2 may be better
   - Mixed: Hybrid approach? (use Earcut, fall back to libtess2)

3. **How many polygons with holes do we have?**
   - Rare: Both Earcut and libtess2 handle fine
   - Common: libtess2 is more robust

4. **Is triangle quality important?**
   - For rendering: No (any triangulation works)
   - For collision: Possibly (Delaunay might help)

5. **Can we afford header-only libraries in our build system?**
   - Compile time impact?
   - IDE parsing performance?

## Decision Criteria

**Choose libtess2 if:**
- Robustness is paramount (mission-critical)
- Assets contain complex/self-intersecting polygons
- Willing to write C++ wrapper
- Acceptable with slightly slower performance

**Choose Earcut (mapbox/earcut.hpp) if:**
- Assets are simple polygons (most SVG shapes)
- Performance is critical (2-5x faster)
- Want header-only, modern C++ API
- Willing to fall back to libtess2 for edge cases

**Choose Poly2Tri if:**
- Need Delaunay triangulation specifically
- Triangle quality matters (e.g., for physics simulation)
- Can tolerate moderate performance

**Choose Custom Implementation if:**
- Want to learn tessellation algorithms deeply
- Need perfect control over memory management
- Have time for 2-4 days of implementation + testing
- Want zero dependencies

## Recommendations Framework

### Primary Recommendation: Hybrid Approach

1. **Start with Earcut (mapbox/earcut.hpp)**
   - Use for 90%+ of assets (simple polygons)
   - Fast, header-only, modern C++
   - Prototype quickly

2. **Add libtess2 as Fallback**
   - Use for complex polygons (if needed)
   - Detect complexity: vertex count, holes, etc.
   - Route to appropriate tessellator

3. **Custom Bezier Flattening**
   - Implement De Casteljau's algorithm
   - ~50 lines, full control
   - Integrate with both tessellators

### Prototyping Plan

**Phase 1**: Implement Earcut integration
- Test with simple SVG shapes
- Measure performance
- Identify edge cases

**Phase 2**: Add Bezier flattening
- Implement De Casteljau
- Test curve quality
- Tune tolerance parameters

**Phase 3**: Evaluate Need for libtess2
- Based on asset complexity
- If Earcut handles everything: **done**
- If edge cases appear: add libtess2

**Phase 4**: Optimize
- Profile tessellation performance
- Implement spatial index for custom algorithm if needed
- Add multi-threading

## Related Documentation

- [architecture.md](./architecture.md) - How tessellation fits into overall system
- [svg-parsing-options.md](./svg-parsing-options.md) - SVG parsing (provides input to tessellation)
- [rendering-backend-options.md](./rendering-backend-options.md) - Consuming tessellated output
- [performance-targets.md](./performance-targets.md) - Tessellation performance budgets

## References

**Academic Papers**:
- David Eberly, "Triangulation by Ear Clipping" (2002)
- Held, "FIST: Fast Industrial-Strength Triangulation of Polygons" (1998)

**Libraries**:
- libtess2: https://github.com/memononen/libtess2
- Earcut.hpp: https://github.com/mapbox/earcut.hpp
- Poly2Tri: https://github.com/jhasse/poly2tri

**Tutorials**:
- LearnOpenGL Tessellation: https://learnopengl.com/Advanced-OpenGL/Advanced-Data
- De Casteljau's Algorithm: https://pomax.github.io/bezierinfo/

## Revision History

- 2025-10-24: Initial comparative analysis based on research
