# Vector Graphics System - Technical Documentation

Created: 2025-10-24
Status: Research & Documentation Phase

## Overview

This directory contains comprehensive technical documentation for the vector graphics rendering system in world-sim. The game uses vector-based assets (SVG) with real-time rendering, procedural variation, and dynamic animation capabilities.

**Key Design Principles:**
- **Desktop-first**: Targeting OpenGL 3.3+ (macOS, Windows, Linux)
- **Performance-driven**: 60 FPS with 10,000+ animated entities
- **Hybrid approach**: Multiple rendering tiers for different use cases
- **Animation-focused**: Real-time spline deformation for organic movement
- **No premature commitments**: Analyze multiple options before choosing libraries

## System Architecture

The vector graphics system uses a **four-tier rendering architecture**:

1. **Tier 1 - Static Backgrounds**: Pre-rasterized tile textures, cached in GPU atlases
2. **Tier 2 - Semi-Static Structures**: CPU-tessellated meshes, cached and reused
3. **Tier 3 - Dynamic Animated Entities**: Real-time tessellation with batched GPU rendering
4. **Tier 4 - GPU Compute (Future)**: Advanced effects using compute shaders

See [architecture.md](./architecture.md) for complete system design.

## Core Technical Documents

### System Design
- **[architecture.md](./architecture.md)** - Master architecture document: four-tier system, data flow, integration points
- **[animation-system.md](./animation-system.md)** - Spline-based animation, deformation strategies, interaction design
- **[collision-shapes.md](./collision-shapes.md)** - Dual representation: render geometry vs collision shapes
- **[lod-system.md](./lod-system.md)** - Level of detail strategies for zoom-based rendering
- **[memory-management.md](./memory-management.md)** - Memory architecture, cache policies, arena allocators
- **[performance-targets.md](./performance-targets.md)** - Performance requirements, budgets, profiling methodology
- **[asset-pipeline.md](./asset-pipeline.md)** - Asset workflow from SVG creation to runtime rendering

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

## Game Requirements Context

### Visual Requirements
- **Layered rendering**: Static tile backgrounds + dynamic vector entities on top
- **Real-time deformation**: Grass bending as entities walk through, trees swaying in wind
- **Procedural variation**: Each tile instance varies (color, rotation, scale)
- **Interactivity**: Grass harvestable by animals, visual feedback for trampling

### Performance Requirements
- **Frame rate**: 60 FPS target
- **Entity count**: 10,000+ concurrent animated objects
- **Tessellation budget**: <2ms CPU time per frame
- **Draw call budget**: <100 calls per frame
- **Memory budget**: ~350 MB total for vector rendering

### Technical Constraints
- **Platform**: Desktop only (macOS, Windows, Linux)
- **Graphics API**: OpenGL 3.3+ / OpenGL ES 3.0
- **Architecture**: Client/server with ECS on both sides
- **Asset format**: SVG with custom metadata

## Quick Reference

### Common Use Cases

**Adding a new static tile background:**
1. Create SVG asset
2. Pre-rasterize at multiple LOD levels (Tier 1)
3. Add to texture atlas
4. Reference in tile definition

**Adding an animated entity (grass, tree):**
1. Create SVG with animation metadata (spline keyframes)
2. Define collision shape (usually simplified)
3. Implement animation update logic (Tier 3)
4. Add to batch renderer

**Creating a complex structure:**
1. Create SVG asset
2. Tessellate once at load time (Tier 2)
3. Cache mesh in GPU memory
4. Render with transform-only updates

## Implementation Phases

### Phase 1: Foundation (ui-sandbox)
**Goal**: Prove basic tessellation and rendering pipeline
- Integrate chosen tessellation library
- Build simple vector path renderer
- Implement streaming VBO system
- Test: 10,000 static triangles @ 60 FPS

### Phase 2: Animation System (ui-sandbox)
**Goal**: Real-time spline deformation
- Implement Bezier curve evaluation
- Build spline deformation system
- Create animated grass blade demo
- Profile re-tessellation cost

### Phase 3: Batching + Atlasing (ui-sandbox)
**Goal**: Render thousands of dynamic shapes efficiently
- Build texture atlas system
- Implement batch renderer
- Test with 1,000+ animated grass blades
- Optimize with frustum culling

### Phase 4: Tiered System (game integration)
**Goal**: Multi-tier architecture in game
- Implement all four tiers
- Build scene graph with layers
- Test with real tile data

### Phase 5: Collision + Interaction (game integration)
**Goal**: Physics integration
- Generate collision shapes from vectors
- Implement trampling mechanics
- Add spatial partitioning

### Phase 6: Optimization + Polish
**Goal**: Hit performance targets
- Profile and optimize bottlenecks
- Implement LOD system
- Add multi-threaded tessellation if needed

## Related Documentation

### Design Documents
See [/docs/design/features/vector-graphics/](../../design/features/vector-graphics/) for player-facing design:
- Asset creation workflow
- Animated vegetation gameplay mechanics
- Environmental interaction design

### Engine Integration
- [/docs/technical/renderer-architecture.md](../renderer-architecture.md) - OpenGL renderer design
- [/docs/technical/cpp-coding-standards.md](../cpp-coding-standards.md) - ECS integration patterns
- [/docs/technical/memory-arenas.md](../memory-arenas.md) - Memory allocators for tessellation
- [/docs/technical/ground-textures.md](../ground-textures.md) - Tier 1 tile texture implementation (SVG patterns → GPU atlas)

### Research Sources
Key research findings documented in each comparative analysis document, including:
- Godot's 2D drawing API (CPU tessellation + batched GPU rendering)
- Unity Vector Graphics package (offline tessellation, mesh-based)
- Vello (modern GPU compute-centric renderer)
- NanoVG (stencil-based vector rendering)
- Bevy vector graphics crates
- Phaser/LibGDX batching strategies
- Raylib's rlgl abstraction layer

## Decision Status

**No libraries or approaches have been chosen yet.** This documentation phase analyzes options and establishes decision criteria. Library selection will occur after:
1. Completing all comparative analysis documents
2. Building small prototypes in ui-sandbox
3. Performance testing with real game requirements

## Contributing to These Docs

When adding or updating vector graphics documentation:
1. Keep comparative analyses objective (pros/cons/data, no opinions)
2. Include citations to research sources
3. Update this INDEX.md with links to new documents
4. Cross-reference related documents
5. Update `/docs/status.md` when completing major documentation milestones
