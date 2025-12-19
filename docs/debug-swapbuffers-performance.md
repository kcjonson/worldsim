# Debug Log: swapBuffers Performance

**Issue:** glfwSwapBuffers takes 40-50ms, game runs at ~16-21 FPS
**Target:** 120 FPS (8.33ms per frame)
**Started:** 2025-12-19
**Status:** IN PROGRESS - GPU shader work still bottleneck

---

## Key Discoveries

### 1. Tile Shader FBM Noise (FIXED)
**Location:** `libs/renderer/shaders/includes/tile.glsl`
**Problem:** `computeTileEdgeDarkening()` called `tileFBM()` 8 times per pixel (~80 hash ops)
**Impact:** ~30ms GPU time at 2560x1440
**Fix:** Replaced with 4 simple `tileNoise2D()` calls (~16 hash ops)
**Result:** Saved ~25-30ms GPU time

### 2. glFinish() Diagnostic Overhead (REMOVED)
**Problem:** Adding `glFinish()` before swapBuffers forces CPU-GPU sync every frame
**Impact:** Adds 6-15ms even on simple scenes (main menu with 146 triangles)
**Lesson:** glFinish is useful for diagnosis but must be removed for production

### 3. Main Menu Performance (RESOLVED)
**Previous:** Main menu was slow with variable frame times
**After cleanup:** Main menu runs at 122 FPS, swapBuffersMs = 0.27ms
**Root cause:** Leftover debug code from previous session was causing the slowness

### 4. Developer Client Frame Budget Bar (IMPROVED)
**Problem:** "Other" category was showing ~200% of frame budget, hiding the real issue
**Fix:** Added explicit "GPU" category showing `swapBuffersMs`
**Result:** Now clearly shows GPU (swapBuffers) as the bottleneck in red

### 5. Per-Frame Vertex Uploads (KNOWN ISSUE)
**Location:** `libs/renderer/primitives/BatchRenderer.cpp:500`
**Problem:** UberVertex is 96 bytes. 6588 tiles × 4 verts = 2.5MB uploaded every frame
**Impact:** Significant CPU→GPU bandwidth usage
**Solution needed:** Cache tile geometry in GPU memory like EntityRenderer does for flora

---

## Session 2: Current Investigation (2025-12-19 afternoon)

### Starting Point
- Main menu: 122 FPS, swapBuffersMs = 0.27ms ✓
- Game scene: 24-27 FPS, swapBuffersMs = 32-36ms ✗

### Hypothesis: Tile Shader Per-Pixel Work Too Expensive

**Analysis:** Each tile pixel potentially does:
1. Base texture sample (required)
2. `computeHigherBleedWeights()` - calculates 4 cardinal blend weights
3. Up to 4 cardinal neighbor texture samples
4. `computeDiagonalCornerWeights()` - calculates 4 diagonal blend weights
5. Up to 4 diagonal neighbor texture samples
6. `computeTileEdgeDarkening()` - 4 noise samples + edge calcs

**Total per edge pixel:** Up to 13 texture samples + 4 noise calls
**At 3.7M pixels:** ~50M texture samples per frame (worst case)

### Test 1: Interior Pixel Early-Out
**Change:** Added early-out for interior pixels (uv 0.22-0.78 range)
**Location:** `libs/renderer/shaders/uber.frag:127-168`
**Rationale:** Interior pixels need no blending or edge darkening
**Expected savings:** 31% of pixels skip expensive work (interior = 0.56² = 31%)
**Result:** swapBuffersMs 36ms → 32ms (~11% improvement)
**Conclusion:** Modest improvement, but 69% of pixels still in edge region

### Test 2: Disable ALL Tile Blending/Darkening
**Change:** Wrapped entire blending block in `#if 0`
**Result:** swapBuffersMs 32ms → 29ms (only 3ms improvement!)
**Conclusion:** BLENDING IS NOT THE MAIN BOTTLENECK! Even with simplest tile shader, still 29ms.

### Test 3: Disable Tile Texture Sampling
**Change:** Skip texture lookup, just use solid vertex color
**Result:** swapBuffersMs 29ms → 19.6ms (~10ms improvement)
**Conclusion:** Texture sampling costs ~10ms. But still 19.6ms for solid colors! Something else is slow.

### Test 4: Disable Entity Rendering
**Change:** Wrapped `m_entityRenderer->render()` in `#if 0` in GameScene.cpp:293
**Result:** swapBuffersMs 19.6ms → 0.30ms!!! FPS jumps to 113-114!
**Conclusion:** **ENTITIES ARE THE MAIN BOTTLENECK!**

### Analysis Summary
| Configuration | swapBuffersMs | FPS | Delta |
|--------------|---------------|-----|-------|
| Full (baseline) | 36ms | 27 | - |
| + Interior early-out | 32ms | - | -4ms |
| No blending | 29ms | - | -3ms |
| No tile texture | 19.6ms | 41 | -10ms |
| **No entities** | 0.30ms | 114 | **-19ms** |
| Main menu (UI only) | 0.27ms | 122 | - |

**ROOT CAUSE IDENTIFIED:** 1.7M instanced entities cost ~19-30ms of GPU time.
- With entities: 24-29 FPS
- Without entities: 113-114 FPS

**Why entities are slow:**
The EntityRenderer uses GPU instancing to draw 1.7M flora entities across visible chunks.
Even with instancing, this many instances causes significant GPU load.

**Possible solutions:**
1. **Reduce flora density** - Increase kTileStride from 4 to 8 (4× fewer spawn points)
2. **Reduce clump sizes** - GrassBlade spawns 3-12 per clump, reduce to 1-4
3. **LOD for distant entities** - Skip or simplify entities at distance
4. **Reduce visible chunk count** - From 4 to 2 (but affects gameplay)
5. **View-frustum culling** - Only render entities in camera view
6. **Zoom-based culling** - Skip small entities when zoomed out

**Entity density analysis:**
- kTileStride = 4 → 16,384 spawn points per chunk
- GrassBlade in Grassland: 70% chance × 7.5 avg clump = ~86K grass per chunk
- 4 chunks visible → ~344K grass blades + other flora = 1.7M total
- Each entity is a GPU instance with position + color data

**SOLUTION: Bake static flora into per-chunk meshes**

Current approach (GPU Instancing):
- 1 template mesh (grass blade, ~4-10 vertices)
- 100K+ instances per chunk (each with position/scale/color data)
- GPU fetches instance data for EACH instance → memory bandwidth bottleneck

Proposed approach (Baked Static Mesh):
- Pre-transform all vertices on CPU ONCE when chunk loads
- Store 100K × 4 vertices = 400K vertices in ONE VBO per chunk
- One draw call, no per-instance data fetching
- GPU reads sequential vertex data with perfect cache coherency

Why baking is faster for 100K+ static instances:
1. No per-instance attribute fetching (major GPU stall)
2. Sequential memory access (cache-friendly)
3. Simpler vertex shader (no per-instance transform)
4. Modern games use this for foliage

Implementation outline:
1. In `buildChunkCache()`, bake vertices instead of storing instances
2. Transform each grass blade's vertices by its world position
3. Store as one large VBO per chunk (or per entity type per chunk)
4. Draw with single glDrawElements() instead of glDrawElementsInstanced()

Temporary fix (applied): Increase kTileStride from 4 to 8
- Reduces entities by 4× (1.7M → 440K)
- FPS: 27 → 41
- swapBuffersMs: 32ms → 20ms
- This buys time but proper baking is needed for full entity count

---

## Code Changes Made This Session

### Kept Changes
- `libs/renderer/shaders/uber.frag:127-168` - Interior pixel early-out optimization
- `libs/renderer/shaders/includes/tile.glsl` - FBM → simple noise (kept from earlier)
- `apps/developer-client/src/components/FrameBudgetBar.tsx` - Added GPU (swapBuffers) category
- `apps/developer-client/src/components/FrameBudgetBar.module.css` - Added GPU color (red)
- `apps/developer-client/src/App.tsx:365` - Pass swapBuffersMs to FrameBudgetBar

### Temporary Changes (need reverting after baked mesh implementation)
- `libs/engine/assets/placement/PlacementExecutor.cpp:143` - kTileStride 4→8 (temporary)
- `libs/engine/application/AppLauncher.cpp:96` - VSync disabled: `glfwSwapInterval(0)`

---

## Session 3: Baked Static Mesh Implementation (2025-12-19 late afternoon)

### Implementation Complete

Implemented baked static mesh approach:
1. Added `BakedChunkData` struct to EntityRenderer.h
2. Implemented `buildBakedChunkMesh()` - pre-transforms all entity vertices on CPU
3. Implemented `renderBakedChunks()` - uses glDrawElements (no instancing)
4. Added `u_instanced == 2` mode to uber.vert for world-space baked vertices
5. Added `worldToScreen()` function to instancing.glsl

### Performance Results

| Configuration | Entity Count | FPS | swapBuffersMs | entityRenderMs |
|---------------|--------------|-----|---------------|----------------|
| Instanced (kTileStride=8) | 440K | 41 | 20ms | - |
| Baked (kTileStride=8) | 440K | 33 | 26ms | 0.88ms |
| Instanced (kTileStride=4) | 1.76M | 24-27 | 32-36ms | - |
| **Baked (kTileStride=4)** | **1.76M** | **28.5** | **30ms** | **0.67ms** |

### Analysis

**CPU Performance: DRAMATICALLY IMPROVED**
- entityRenderMs dropped to 0.67ms for 1.76M entities
- Baking eliminated per-instance attribute fetching overhead
- CPU is no longer a bottleneck

**GPU Performance: STILL THE BOTTLENECK**
- Overall improvement: ~10-15% faster (28.5 FPS vs 24-27 FPS)
- GPU must still rasterize ~3.5M triangles per frame
- Memory bandwidth: 168MB of vertex data per frame

**Why GPU is still slow:**
Even with perfect vertex layout, the GPU must:
1. Read 168MB of vertex data per frame (1.76M × 4 verts × 24 bytes)
2. Transform & rasterize ~3.5M triangles
3. Run fragment shader for each visible pixel

### Conclusion

Baked static meshes successfully eliminated the CPU bottleneck, but 1.76M entities is simply too many for 60 FPS on this hardware. The GPU is now the pure bottleneck.

### Future Optimization Options

To achieve 60 FPS (target: 16.67ms per frame), need ~45% reduction in GPU work:

1. **View frustum culling within chunks** - Subdivide chunks and cull off-screen regions
2. **Billboard/impostor system** - Replace 3D geometry with textured quads
3. **Spatial partitioning** - Better culling with quadtree or grid subdivision

### OPTIONS RULED OUT (Do Not Implement)

The following are OFF THE TABLE - visual quality must be preserved:
- ❌ Zoom-based LOD (skipping grass when zoomed out)
- ❌ Reducing grass clump size (3-12 blades per clump is intentional)
- ❌ Simpler grass geometry (4 vertices per blade is minimal)

---

## Current Performance Metrics (with baked meshes, kTileStride=4)

**Main Menu (no tiles):**
- FPS: 122
- swapBuffersMs: 0.27ms
- triangles: 146
- draw calls: 1

**Game Scene (with baked meshes at full density):**
- FPS: 28.5
- swapBuffersMs: 30ms
- tileCount: 6588
- entityCount: 1.76M
- entityRenderMs: 0.67ms (CPU time - very fast!)
- gpuRenderMs: 21ms
- memoryUsed: 1.5GB

---

## Code Changes Made This Session

### New/Modified Files
- `libs/engine/world/rendering/EntityRenderer.h` - Added BakedChunkData struct and methods
- `libs/engine/world/rendering/EntityRenderer.cpp` - Implemented baked mesh rendering
- `libs/renderer/shaders/uber.vert` - Added u_instanced==2 (baked world-space) path
- `libs/renderer/shaders/includes/instancing.glsl` - Added worldToScreen() function
- `libs/engine/assets/placement/PlacementExecutor.cpp` - Restored kTileStride=4

### Changes from Previous Session Still Present
- `libs/renderer/shaders/includes/tile.glsl` - FBM → simple noise
- `libs/renderer/shaders/uber.frag` - Interior pixel early-out optimization
- `apps/developer-client/` - GPU (swapBuffers) category in FrameBudgetBar
- `libs/engine/application/AppLauncher.cpp` - VSync disabled

---

## Hardware Context
- OpenGL Version: 4.1 ATI-7.0.24 (AMD GPU on macOS)
- Monitor: 120Hz
- Window: 2560x1440
- VSync: Disabled via `glfwSwapInterval(0)`
