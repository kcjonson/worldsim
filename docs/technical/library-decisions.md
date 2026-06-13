# Library Decisions

**Status:** Active  
**Last Updated:** 2024-12-04

This document is the **single source of truth** for library selection decisions. All library choices should be documented here, not scattered across individual technical docs.

---

## Confirmed Decisions

### Networking

| Library | Purpose | Decision Date | Notes |
|---------|---------|---------------|-------|
| **cpp-httplib** | HTTP server, WebSocket | 2025-10-24 | Header-only, SSE support, used for both game server and debug server |

**Rationale:** Already in vcpkg.json, simple integration, good enough for Phase 1 & 2 (localhost, <50 players).

**Future consideration:** uWebSockets for Phase 3 if scaling to thousands of players.

### SVG Parsing

| Library | Purpose | Decision Date | Notes |
|---------|---------|---------------|-------|
| **NanoSVG** | SVG file parsing | 2025-10-24 | Header-only, lightweight, already in project |

**Rationale:** Simple, covers our minimal SVG subset. Upgrade path to LunaSVG if complex features needed.

**Custom metadata:** pugixml for parsing game-specific metadata in SVG files.

### Build System

| Tool | Purpose | Decision Date | Notes |
|------|---------|---------------|-------|
| **CMake** | Build system | Project start | Standard, good vcpkg integration |
| **vcpkg** | Package management | Project start | Manifest mode |

---

## Under Evaluation

### Tessellation

**Options being considered:**
- libtess2
- Earcut (mapbox/earcut.hpp)
- Poly2Tri
- Custom ear clipping

**Status:** Prototyping phase. See [tessellation-options.md](/docs/technical/vector-graphics/tessellation-options.md)

**Decision criteria:**
- Performance with complex paths
- Bezier curve handling
- Integration complexity

### Rendering Backend

**Options being considered:**
- Custom batched renderer (recommended)
- NanoVG
- Blend2D
- Vello (future, GPU compute)

**Status:** Prototyping phase. See [rendering-backend-options.md](/docs/technical/vector-graphics/rendering-backend-options.md)

**Recommendation:** Custom batched renderer for 10,000+ dynamic entities.

---

## Rejected Options

| Library | Purpose | Rejected Date | Reason |
|---------|---------|---------------|--------|
| Bazel | Build system | 2025-10-27 | Migration complexity not worth benefits for current project size |

### libs/geometry (integer-millimeter exact geometry for the construction system)

Context: [building-construction-architecture.md D2](./building-construction-architecture.md#d2-no-new-dependencies--geometry-built-in-house-concepts-borrowed). Decision to build in-house follows the same pattern as the Tessellator (concepts borrowed from Lyon, library not adopted).

| Library | License | Purpose considered | Rejected Date |
|---------|---------|-------------------|---------------|
| **Clipper2** | Boost | Polygon clipping and offsetting | 2026-06-12 |
| **earcut.hpp** | ISC (Mapbox) | Polygon triangulation | 2026-06-12 |
| **CGAL** | GPL / commercial | Exact arrangements and boolean ops | 2026-06-12 |
| **CavalierContours** | MIT | Polyline offsetting with arc support | 2026-06-12 |

**Clipper2:** we need a narrow slice — union/difference of two simple editor-validated rings, and straight-segment miter offsetting. The boolean ops ride the same arrangement + face-extraction core that room detection needs (D6), so that substrate had to be built regardless. Concepts borrowed (integer coordinates as the robustness model, miter limit with square fallback, post-boolean simplification pass); library not adopted.

**earcut.hpp:** the existing Tessellator already covers runtime polygon triangulation. Generated construction geometry is well-formed by construction (D4 invariant), so no second triangulator is needed.

**CGAL:** GPL/commercial licensing friction, and enormous dependency surface for a narrow, well-validated input domain. The editor invariants (no crossings, T-junctions pre-split, min angle/spacing enforced) remove the cases that make general-purpose arrangements hard.

**CavalierContours:** we offset only straight segments with miter joins. The 30° minimum-angle invariant bounds miter length and prevents band self-overlap, removing the hard cases (arcs, self-intersection trimming, collapsing loops) the library exists to solve.

---

## Decision Process

When evaluating a new library:

1. **Document options** in relevant technical doc (e.g., `tessellation-options.md`)
2. **Prototype** in ui-sandbox if significant
3. **Make decision** based on:
   - Performance requirements
   - Integration complexity
   - Maintenance burden
   - License compatibility (prefer MIT, BSD, zlib)
4. **Record decision** in this file
5. **Update vcpkg.json** if adding dependency

---

## vcpkg Dependencies

Current dependencies in `vcpkg.json`:

```json
{
  "dependencies": [
    "cpp-httplib",
    "nanosvg",
    "sol2",
    "lua",
    "imgui",
    "glfw3",
    "glad",
    "glm",
    "stb",
    "nlohmann-json"
  ]
}
```

---

## Related Documentation

- [Monorepo Structure](/docs/technical/monorepo-structure.md)
- [Build System](/docs/technical/build-system.md)
- [Vector Graphics Options](/docs/technical/vector-graphics/INDEX.md)
