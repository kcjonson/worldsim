# Vector Graphics System - Technical Documentation

Created: 2025-10-24
Last Updated: 2025-12-23
Status: **Production** - Core system complete, GPU instancing achieving 34k+ entities @ 60 FPS

## Overview

This directory contains comprehensive technical documentation for the vector graphics rendering system in world-sim. The game uses vector-based assets (SVG) with real-time rendering, procedural variation, and dynamic animation capabilities.

**Key Design Principles:**
- **Desktop-first**: Targeting OpenGL 3.3+ (macOS, Windows, Linux)
- **Performance-driven**: 60 FPS with 10,000+ animated entities
- **Hybrid approach**: Multiple rendering tiers for different use cases
- **Animation-focused**: Real-time spline deformation for organic movement

## System Architecture

The vector graphics system uses a **four-tier rendering architecture**:

1. **Tier 1 - Static Backgrounds**: Pre-rasterized tile textures, cached in GPU atlases ✅ Complete
2. **Tier 2 - Semi-Static Structures**: CPU-tessellated meshes, cached and reused ✅ Complete
3. **Tier 3 - Dynamic Animated Entities**: Real-time tessellation with batched GPU rendering ✅ Complete
4. **Tier 4 - GPU Compute (Future)**: Advanced effects using compute shaders (deferred)

See [architecture.md](./architecture.md) for complete system design.

## Core Technical Documents

### System Design
- **[architecture.md](./architecture.md)** - Master architecture document: four-tier system, data flow, integration points
- **[animation-system.md](./animation-system.md)** - Spline-based animation, deformation strategies, interaction design
- **[animation-performance.md](./animation-performance.md)** - GPU instancing optimization (Phase 2 complete)
- **[collision-shapes.md](./collision-shapes.md)** - Dual representation: render geometry vs collision shapes
- **[lod-system.md](./lod-system.md)** - Level of detail strategies for zoom-based rendering
- **[memory-management.md](./memory-management.md)** - Memory architecture, cache policies, arena allocators
- **[performance-targets.md](./performance-targets.md)** - Performance requirements, budgets, profiling methodology
- **[asset-pipeline.md](./asset-pipeline.md)** - Asset workflow from SVG creation to runtime rendering
- **[validation-plan.md](./validation-plan.md)** - Historical: bottom-up validation phases (completed)

### Comparative Analysis Documents

**These documents analyze 3+ options for each component, including "no library" approaches:**

- **[tessellation-options.md](./tessellation-options.md)** - Polygon tessellation libraries and algorithms
  - libtess2, Earcut, custom implementation, mapbox/earcut.hpp
  - Bezier curve conversion strategies
  - Adaptive quality control

- **[svg-parsing-options.md](./svg-parsing-options.md)** - SVG parsing libraries
  - NanoSVG, LunaSVG, PlutoVG, custom subset parser
  - Feature coverage vs complexity trade-offs
  - Minimal SVG feature set for game assets

- **[rendering-backend-options.md](./rendering-backend-options.md)** - Overall rendering approach
  - NanoVG approach, Blend2D, custom batched renderer, Vello, hybrid
  - CPU vs GPU tessellation trade-offs
  - Batching and atlasing strategies

- **[batching-strategies.md](./batching-strategies.md)** - GPU batching and optimization
  - Streaming VBO patterns
  - Texture atlas generation
  - Draw call minimization
  - Instancing vs batching analysis

## Implementation Status

### Completed
- **Tessellation Pipeline**: Custom ear-clipping with convex polygon optimization
- **SVG Loading**: NanoSVG integration with custom metadata parsing
- **GPU Instancing**: Batched rendering achieving 34k+ entities @ 60 FPS
- **Tile Texture System**: Tier 1 complete - SVG patterns rasterized to GPU atlas
- **Asset System**: XML-driven definitions with SVG rendering

### Deferred (Post-MVP)
- **CPU Optimization Stack**: Arena allocators, temporal coherence, SIMD (Tier 3 optimization)
- **Vertex Shader Animation**: Wind displacement in shader (currently CPU-driven)
- **Tiered System Integration**: Full Phase 3 optimization pass

See `/docs/status.md` → "Deferred Epics" for details.

## Game Requirements Context

### Visual Requirements
- **Layered rendering**: Static tile backgrounds + dynamic vector entities on top
- **Real-time deformation**: Grass bending as entities walk through, trees swaying in wind
- **Procedural variation**: Each tile instance varies (color, rotation, scale)
- **Interactivity**: Grass harvestable by animals, visual feedback for trampling

### Performance Achieved
- **Frame rate**: 60 FPS maintained
- **Entity count**: 34,000+ concurrent animated objects demonstrated
- **Tessellation**: Custom implementation with convex polygon fast-path
- **Draw calls**: Batched via GPU instancing

### Technical Stack
- **Platform**: Desktop only (macOS, Windows, Linux)
- **Graphics API**: OpenGL 3.3+
- **SVG Parsing**: NanoSVG (header-only, already in vcpkg)
- **Tessellation**: Custom ear-clipping + fan tessellation for convex shapes
- **Asset format**: SVG + XML definition files

## Quick Reference

### Common Use Cases

**Adding a new static tile background:**
1. Create SVG pattern in `assets/patterns/`
2. Reference in TileAtlasBuilder (auto-rasterized to atlas)
3. Map surface type to pattern in ChunkRenderer

**Adding an animated entity (grass, tree):**
1. Create SVG asset in `assets/` folder
2. Add XML definition with placement/animation params
3. System auto-loads via AssetRegistry

**Creating a complex structure:**
1. Create folder in `assets/` with `Name/Name.xml` + `Name.svg`
2. Define capabilities, placement rules in XML
3. Tessellated mesh cached automatically

## Related Documentation

### Asset System
- [/docs/technical/asset-system/](../asset-system/) - Complete asset definition system
- [/docs/technical/asset-system/asset-definitions.md](../asset-system/asset-definitions.md) - XML schema reference

### Design Documents
See [/docs/design/features/vector-graphics/](../../design/features/vector-graphics/) for player-facing design:
- Asset creation workflow
- Animated vegetation gameplay mechanics
- Environmental interaction design

### Engine Integration
- [/docs/technical/ground-textures.md](../ground-textures.md) - Tier 1 tile texture implementation
- [/docs/technical/cpp-coding-standards.md](../cpp-coding-standards.md) - ECS integration patterns

### Research Sources
Key research findings documented in each comparative analysis document, including:
- Godot's 2D drawing API (CPU tessellation + batched GPU rendering)
- Unity Vector Graphics package (offline tessellation, mesh-based)
- Vello (modern GPU compute-centric renderer)
- NanoVG (stencil-based vector rendering)
