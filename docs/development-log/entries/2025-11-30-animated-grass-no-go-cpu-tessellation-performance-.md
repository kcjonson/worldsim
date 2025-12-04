# Animated Grass NO-GO: CPU Tessellation Performance Analysis

**Date:** 2025-11-30

**Summary:**
10,000 animated grass blades with per-frame Bezier retessellation achieved only **12 FPS** (target: 60 FPS). This is a critical NO-GO for naive CPU tessellation at scale. Created comprehensive optimization plan with tiered animation system.

**Benchmark Results:**
| Metric | Result | Target | Status |
|--------|--------|--------|--------|
| FPS | 12 | 60 | **4x under** |
| Frame time | ~83ms | 16.67ms | **5x over** |
| Tessellation | ~65ms | <10ms | **6x over** |
| Blades | 10,000 | 10,000 | OK |
| Triangles | 24,832 | - | OK |

**Bottleneck Analysis:**
- **Bezier flattening (De Casteljau)**: ~50ms (77% of frame)
- **Ear clipping tessellation**: ~10ms (15% of frame)
- **Buffer building/rendering**: ~5ms (8% of frame)

**GO/NO-GO Decision:** NO-GO for naive CPU tessellation.

**Research Conducted:**
Explored three optimization approaches:
1. **GPU Instancing + Vertex Shader Animation** - Pre-tessellate once, animate in vertex shader. Industry standard (Zelda BotW, Genshin Impact). Works for simple parametric deformation.
2. **CPU Optimization Stack** - Arena allocators (5-10%), temporal coherence (2-4x), SIMD Bezier (2-3x). Combined can achieve 45-60 FPS.
3. **Tiered System** - Combine both: GPU instancing for simple flora, optimized CPU for complex flora.

**Key Insight:** User clarified this isn't just about grass - the system must support complex vector flora (trees, bushes, procedural assets) with true Bezier curve deformations. This means CPU tessellation must remain viable for complex assets even while GPU instancing handles simple repeated objects.

**Chosen Architecture: Tiered Animation System**
- **Tier 1 (GPU Instancing)**: Simple flora with parametric deformation (grass, small plants). Target: 100,000+ instances @ 60 FPS.
- **Tier 2 (Optimized CPU)**: Complex flora with true Bezier curves (trees, bushes, procedural). Target: 1,000-2,000 assets @ 60 FPS.
- **Tier 3 (Hybrid)**: Runtime tier selection based on asset complexity and distance.

**Files Created:**
- `docs/technical/vector-graphics/animation-performance.md` - Technical spec for optimization epic

**Files Modified:**
- `docs/status.md` - Added Animation Performance Optimization epic, marked Grass Validation complete

**Next Steps:**
Phase 1 (CPU Optimization) first to validate Bezier deformation can hit 60 FPS:
1. Arena allocator integration for Bezier/Tessellator
2. Temporal coherence system (skip unchanged blades)
3. SIMD Bezier flattening (ARM NEON)



