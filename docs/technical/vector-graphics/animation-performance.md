# Animated Vector Graphics Performance Optimization

**Status**: In Progress
**Created**: 2025-11-30
**Last Updated**: 2025-11-30

## Problem Statement

The animated grass demo (10,000 blades with per-frame Bezier retessellation) runs at **12 FPS** instead of the target 60 FPS. CPU tessellation takes ~65ms per frame, which is 4x over the 16.67ms budget.

**Critical Requirement**: This system must support **complex vector flora** (trees, bushes, procedurally generated assets) with true Bezier curve deformations - not just simple sine-wave vertex displacement. The grass demo is a validation benchmark, but the real use case is diverse animated vegetation.

## Benchmark Results (2025-11-30)

| Metric | Value | Target |
|--------|-------|--------|
| FPS | 12 | 60 |
| Frame time | ~83ms | 16.67ms |
| Tessellation time | ~65ms | <10ms |
| Blade count | 10,000 | 10,000 |
| Triangles | 24,832 | - |
| Vertices | 44,832 | - |

### Bottleneck Analysis

| Component | Time | % of Frame |
|-----------|------|------------|
| Bezier flattening (De Casteljau) | ~50ms | 77% |
| Ear clipping tessellation | ~10ms | 15% |
| Buffer building/rendering | ~5ms | 8% |

The Bezier curve flattening is the dominant bottleneck, taking 77% of frame time.

## GO/NO-GO Decision

**Result**: NO-GO for naive CPU tessellation at scale.

The current approach of regenerating all geometry every frame on the CPU is not viable for 10,000+ animated vector assets. However, with optimizations (detailed below), CPU tessellation remains viable for complex assets at lower counts.

## Recommended Architecture: Tiered Animation System

```
┌─────────────────────────────────────────────────────────────────┐
│                    Tiered Animation System                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Tier 1: GPU Instanced Rendering                            │ │
│  │ - Pre-tessellate ONE shape at load time                    │ │
│  │ - 100,000+ instances via glDrawElementsInstanced           │ │
│  │ - Vertex shader applies wind deformation (sine waves)      │ │
│  │ - Use for: grass, simple plants, repeated patterns         │ │
│  │ - Performance: 60+ FPS with virtually unlimited instances  │ │
│  └────────────────────────────────────────────────────────────┘ │
│                              │                                   │
│                              ▼                                   │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Tier 2: Optimized CPU Tessellation                         │ │
│  │ - Arena allocators (14x faster than malloc)                │ │
│  │ - Temporal coherence (skip unchanged objects)              │ │
│  │ - SIMD Bezier (4 curves in parallel)                       │ │
│  │ - Use for: trees, bushes, complex procedural shapes        │ │
│  │ - Performance: 1,000-2,000 complex assets at 60 FPS        │ │
│  └────────────────────────────────────────────────────────────┘ │
│                              │                                   │
│                              ▼                                   │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │ Tier 3: Hybrid Selection                                   │ │
│  │ - Distance-based: close → Tier 2, far → Tier 1            │ │
│  │ - Complexity-based: simple → Tier 1, complex → Tier 2     │ │
│  │ - Frame budget monitoring with graceful degradation        │ │
│  └────────────────────────────────────────────────────────────┘ │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Implementation Phases

### Phase 1: CPU Optimization Stack

**Goal**: Prove Bezier deformation can hit 60 FPS with optimization.

#### 1.1 Arena Allocator Integration
Add arena parameter to tessellation functions to eliminate malloc overhead.

**Files to modify**:
- `libs/renderer/vector/Bezier.h/cpp`
- `libs/renderer/vector/Tessellator.h/cpp`

**Expected improvement**: 5-10%

#### 1.2 Temporal Coherence System
Only retessellate blades when their deformation changes significantly.

```cpp
// Store previous frame's state
struct BladeCache {
    float lastBendAmount;
    renderer::TessellatedMesh mesh;
    bool dirty;
};

// In update loop:
float bendDelta = std::abs(newBend - blade.lastBendAmount);
if (bendDelta < kRetessellationThreshold) {
    // Reuse cached mesh
    continue;
}
```

**Expected improvement**: 2-4x (most blades barely change frame-to-frame)

#### 1.3 SIMD Bezier Flattening
Process 4 curves in parallel using ARM NEON intrinsics.

```cpp
// Process 4 midpoint calculations simultaneously
float32x4_t x0 = vld1q_f32(curve_x0);
float32x4_t x1 = vld1q_f32(curve_x1);
float32x4_t mid_x = vmulq_n_f32(vaddq_f32(x0, x1), 0.5f);
```

**Expected improvement**: 2-3x on Bezier specifically

### Phase 2: GPU Instancing Path

**Goal**: Enable massive instance counts for simple flora.

#### 2.1 Instanced Rendering Infrastructure
Add `glDrawElementsInstanced` support to BatchRenderer.

```cpp
// New vertex attributes (divisor = 1)
struct InstanceData {
    Vec2 position;      // World position
    float rotation;     // Radians
    float phase;        // Wind animation phase
    float scale;        // Size multiplier
    Vec3 colorMod;      // Color variation
};

// In flush():
glDrawElementsInstanced(GL_TRIANGLES, indexCount, GL_UNSIGNED_SHORT,
                        nullptr, instanceCount);
```

#### 2.2 Vertex Shader Animation
Wind animation computed entirely on GPU.

```glsl
// uber.vert additions
uniform float u_time;
in vec4 a_instanceData;   // position.xy, rotation, phase
in vec4 a_instanceData2;  // scale, color.rgb

void main() {
    // Apply wind deformation
    float windOffset = sin(u_time * 2.0 + a_instanceData.w) * 0.3;
    vec2 deformed = a_position.xy;
    deformed.x += windOffset * (1.0 - a_position.y / bladeHeight);

    // Apply instance transform
    // ...
}
```

### Phase 3: Tiered System Integration

**Goal**: Unified API that selects optimal path automatically.

#### 3.1 Asset Classification
```cpp
enum class VectorAssetComplexity {
    Simple,   // Can use GPU instancing (grass, simple plants)
    Complex   // Requires CPU tessellation (trees, procedural)
};

VectorAssetComplexity classifyAsset(const VectorAsset& asset) {
    if (asset.curveCount <= 2 && asset.deformationType == DeformType::Parametric) {
        return VectorAssetComplexity::Simple;
    }
    return VectorAssetComplexity::Complex;
}
```

#### 3.2 Runtime Selection
```cpp
void renderFlora(const std::vector<FloraInstance>& instances) {
    // Sort by tier
    std::vector<FloraInstance> tier1, tier2;
    for (const auto& instance : instances) {
        if (instance.asset->complexity == Simple &&
            instance.distanceToCamera > kTier2CutoffDistance) {
            tier1.push_back(instance);
        } else {
            tier2.push_back(instance);
        }
    }

    // Render Tier 1 (instanced)
    renderInstanced(tier1);

    // Render Tier 2 (CPU tessellated)
    renderCPUTessellated(tier2);
}
```

## Success Criteria

| Phase | Target |
|-------|--------|
| Phase 1 | 10,000 Bezier-deformed blades at 45+ FPS |
| Phase 2 | 100,000 instanced blades at 60+ FPS |
| Phase 3 | Mixed scene (50,000 simple + 500 complex) at 60 FPS |

## Key Technical Decisions

### Why Preserve CPU Tessellation?
- Complex procedural assets can't be reduced to parametric deformation
- Tree branches with multiple Bezier curves need true curve evaluation
- Future assets may have unpredictable deformation patterns
- Artistic freedom to create complex organic shapes

### Why Add GPU Instancing?
- Simple flora (grass, small plants) dominates instance count
- 90% of objects may be "simple" and benefit from instancing
- Industry standard approach (Zelda BotW, Genshin Impact)
- Enables 100,000+ instances vs 2,000 CPU tessellated

### Why Tiered System?
- Best of both worlds: performance AND flexibility
- Automatic optimization without artist intervention
- Graceful degradation maintains playable framerate
- Scales from mobile to desktop hardware

## Files to Modify/Create

### Phase 1 (CPU Optimization)
| File | Changes |
|------|---------|
| `libs/renderer/vector/Bezier.h/cpp` | Arena support, SIMD path |
| `libs/renderer/vector/Tessellator.h/cpp` | Arena support |
| `apps/ui-sandbox/scenes/GrassScene.cpp` | Temporal coherence |

### Phase 2 (GPU Instancing)
| File | Changes |
|------|---------|
| `libs/renderer/primitives/BatchRenderer.h/cpp` | Instanced rendering |
| `libs/renderer/shaders/uber.vert` | Instance attributes, wind animation |
| `apps/ui-sandbox/scenes/GrassInstancedScene.cpp` | New demo |

### Phase 3 (Tiered System)
| File | Changes |
|------|---------|
| `libs/renderer/vector/VectorAsset.h` | Complexity classification |
| `libs/renderer/Renderer.h` | Unified rendering API |
| `apps/ui-sandbox/scenes/FloraScene.cpp` | Mixed demo |

## Related Documents

- [Tessellation Options](tessellation-options.md) - Initial tessellation research
- [Validation Plan](validation-plan.md) - Grass blade validation phases
- [Animation System](animation-system.md) - Animation architecture design
