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
