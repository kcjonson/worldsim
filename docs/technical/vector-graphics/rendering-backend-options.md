# Rendering Backend Options - Comparative Analysis

Created: 2025-10-24
Last Updated: 2025-10-24
Status: Research

## Overview

After parsing SVG and tessellating to triangles, we need a **rendering backend** to actually draw the geometry to the screen. This document analyzes different architectural approaches for the rendering pipeline.

**Key Question**: Should we use an existing vector graphics renderer, or build a custom batched renderer tailored to our game's needs?

## Requirements

### Functional Requirements
- **Multi-Tier Support**: Handle static (Tier 1), semi-static (Tier 2), and dynamic (Tier 3) geometry
- **Batched Rendering**: Minimize draw calls (target: <100 per frame)
- **Texture Atlasing**: Combine multiple textures to reduce state changes
- **Fill Styles**: Solid colors, gradients (optional)
- **Stroke Rendering**: Outlines with variable width (optional for initial version)
- **Transparency**: Alpha blending support
- **Transform Support**: Per-entity transforms (position, rotation, scale)

### Performance Requirements
- **60 FPS** with 10,000+ visible entities
- **GPU Draw Calls**: <100 per frame (ideally <50)
- **Upload Bandwidth**: Efficiently stream dynamic geometry to GPU
- **Rendering Time**: <6ms GPU time per frame

### Integration Requirements
- **OpenGL 3.3+**: Core profile, desktop platforms
- **Integrate with Existing Renderer**: Works with existing `libs/renderer` architecture
- **ECS Compatible**: Integrates with entity-component system
- **Cross-Platform**: macOS, Windows, Linux

## Option A: NanoVG Approach (Stencil-Based)

### Overview
NanoVG is a lightweight vector graphics rendering library that uses OpenGL for anti-aliased 2D rendering. It uses a **stencil buffer approach** for rendering filled shapes.

**Repository**: https://github.com/memononen/nanovg
**License**: zlib License
**Language**: C (can be used from C++)
**Dependencies**: OpenGL 2.0+ or OpenGL ES 2.0+

### How It Works

NanoVG uses a multi-pass stencil buffer technique:

1. **Stencil Pass**: Render path to stencil buffer (mark inside pixels)
2. **Cover Pass**: Render bounding quad, use stencil to mask fill
3. **Anti-Aliasing**: Edge AA using coverage calculation in fragment shader

**Rendering Pipeline**:
```
Path Data → Tessellate to Triangle Fan → Stencil Pass → Cover Pass → Final Pixels
```

### Pros
- **High Quality Anti-Aliasing**: Smooth edges using stencil+cover technique
- **Proven**: Widely used in imgui, games, UI toolkits
- **Handles Complex Shapes**: Overlapping paths, non-zero fill rules
- **Gradients**: Built-in gradient support
- **Mature**: Battle-tested codebase
- **Immediate Mode API**: Easy to use (BeginPath, Fill, Stroke, EndPath)

### Cons
- **Stencil Buffer Required**: Need stencil in framebuffer (8-bit stencil)
- **Multi-Pass**: Each shape requires multiple draw calls (stencil + cover)
- **Draw Call Heavy**: Not optimized for batching thousands of objects
- **Immediate Mode**: Must call drawing commands every frame (no caching)
- **Performance**: Slower than direct triangle rendering for many simple shapes
- **Overdraw**: Cover pass can waste fillrate

### Technical Details

**Performance Characteristics**:
- Draw calls: 2-4 per path (stencil, cover, potentially stroke)
- For 1,000 entities: **2,000-4,000 draw calls** → Way over budget
- GPU fillrate: High due to cover quads
- Best for: UI (hundreds of shapes), not ideal for thousands of game entities

**Memory Footprint**:
- Moderate: Command buffers for paths
- Streaming: Commands regenerated each frame (immediate mode)

**Integration Complexity**:
- Low-Medium: C API, straightforward to use
- Requires stencil buffer setup
- Immediate mode fits some use cases poorly (want persistent geometry)

**API Example**:
```cpp
#include <nanovg.h>

NVGcontext* vg = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);

// Every frame, for each entity:
nvgBeginFrame(vg, windowWidth, windowHeight, pixelRatio);

nvgBeginPath(vg);
nvgMoveTo(vg, x, y);
nvgLineTo(vg, x2, y2);
nvgLineTo(vg, x3, y3);
nvgClosePath(vg);

nvgFillColor(vg, nvgRGBA(255, 192, 0, 255));
nvgFill(vg);

nvgEndFrame(vg);
```

### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Multi-Tier Support | ⚠️ Possible | Immediate mode doesn't cache well |
| Batched Rendering | ❌ Poor | Multi-pass per shape = many draw calls |
| Performance (10k entities) | ❌ Insufficient | Would be 20k-40k draw calls |
| Texture Atlasing | ⚠️ Limited | Has pattern support, not optimized for atlases |
| Integration | ✅ Good | C API, well-documented |
| Quality | ✅ Excellent | Beautiful anti-aliasing |

### Recommendation
❌ **Not Suitable for Game Entities** - Great for UI rendering (hundreds of shapes), but **draw call count too high** for thousands of dynamic game entities. Stencil-based approach doesn't batch well.

**Could Use For**: In-game UI, menus, overlays (Tier 2-like usage, not Tier 3).

---

## Option B: Blend2D Integration (JIT-Compiled CPU Renderer)

### Overview
Blend2D is a high-performance 2D vector graphics engine with a JIT compiler for optimized rendering pipelines. It renders to bitmaps on the CPU, which can then be uploaded to GPU as textures.

**Repository**: https://github.com/blend2d/blend2d
**License**: Zlib License
**Language**: C++
**Dependencies**: AsmJit (JIT compiler)

### How It Works

Blend2D uses **CPU rendering with JIT-compiled pipelines**:

1. Parse vector paths
2. JIT-compile optimized rasterization code for the specific operations
3. Render to CPU bitmap
4. Upload bitmap to GPU as texture

**Rendering Pipeline**:
```
Path Data → JIT Compile Pipeline → CPU Rasterize → Bitmap → GPU Texture
```

### Pros
- **High Performance**: JIT compilation makes CPU rendering very fast
- **Excellent Quality**: High-quality anti-aliasing
- **Full 2D Features**: Gradients, patterns, compositing, filters
- **Multi-Threaded**: Can parallelize rendering across CPU cores
- **Flexible**: Full control over rasterization
- **Modern C++**: Clean API

### Cons
- **CPU-Based**: Renders on CPU, not GPU (may not leverage GPU well)
- **Large Dependency**: AsmJit JIT compiler adds significant complexity
- **Rasterization Required**: Defeats purpose of vector rendering for zoom
- **Upload Overhead**: Must upload bitmaps to GPU every frame (for dynamic)
- **Not Optimized for Real-Time**: Designed for high-quality offline rendering
- **Complexity**: Large codebase, steep learning curve

### Technical Details

**Performance Characteristics**:
- CPU rasterization: 10-50ms for complex scenes (estimate)
- Multi-threaded: Can use multiple cores
- Upload: 1-5ms per frame (texture upload bandwidth)
- **Not suitable for real-time dynamic rendering** at 60 FPS

**Use Cases**:
- **Tier 1 (Static Backgrounds)**: Pre-rasterize at asset import → GPU textures ✅
- **Tier 2 (Semi-Static)**: Pre-render on load → cache textures ✅
- **Tier 3 (Dynamic)**: Too slow for per-frame rasterization ❌

**Memory Footprint**:
- Large: JIT code cache, bitmap buffers
- Moderate GPU memory: Texture uploads

**Integration Complexity**:
- High: Large library, complex API
- CMake build, link dependencies

**API Example**:
```cpp
#include <blend2d.h>

// Create image (CPU buffer)
BLImage img(800, 600, BL_FORMAT_PRGB32);
BLContext ctx(img);

// Draw vector path
BLPath path;
path.moveTo(x, y);
path.lineTo(x2, y2);
path.close();

ctx.setFillStyle(BLRgba32(0xFFFF0000));
ctx.fillPath(path);

ctx.end();

// Upload img to GPU texture
GLuint texture;
glGenTextures(1, &texture);
glBindTexture(GL_TEXTURE_2D, texture);
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 800, 600, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.getData());
```

### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Multi-Tier Support | ⚠️ Tier 1-2 only | CPU rendering too slow for Tier 3 |
| Batched Rendering | ✅ Excellent | Renders to textures, batch textures |
| Performance (10k dynamic) | ❌ Insufficient | CPU rasterization too slow |
| Texture Atlasing | ✅ Good | Outputs bitmaps for atlasing |
| Integration | ⚠️ Complex | Large dependency |
| Quality | ✅ Excellent | High-quality rasterization |

### Recommendation
⚠️ **Use for Pre-Rasterization Only** - Excellent for **Tier 1 (static backgrounds)** at asset import time or on first load. **Not suitable for Tier 3 (dynamic entities)** due to CPU rendering overhead. Could replace NanoSVG's rasterizer.

**Use Case**: Offline or load-time rasterization of static assets to textures.

---

## Option C: Custom Batched Renderer (Streaming VBO + Atlasing)

### Overview
Build a custom OpenGL rendering backend optimized specifically for the game's needs: batch thousands of tessellated triangles with minimal draw calls using streaming VBOs and texture atlases.

**Approach**: Modern batched rendering techniques used in 2D game engines (Unity, Godot, etc.).

### How It Works

**Rendering Pipeline**:
```
Tessellated Triangles → Batch by Material → Upload to Streaming VBO → Single Draw Call per Batch
```

**Key Techniques**:
1. **Batching**: Group all entities with same material/texture
2. **Streaming VBO**: Use mapped buffers or glBufferSubData for dynamic geometry
3. **Texture Atlasing**: Pack multiple textures into large atlases
4. **Instancing**: Use for identical geometries (limited use case for us)

### Pros
- **Optimal Performance**: Tailored exactly to game requirements
- **Full Control**: Complete control over memory, batching strategy
- **Minimal Draw Calls**: Can achieve <100 draws for 10k+ entities
- **Leverage GPU**: Triangles rendered directly on GPU (no CPU rasterization)
- **Scalable**: Handles thousands of dynamic entities efficiently
- **No External Dependencies**: Just OpenGL calls
- **Learning Opportunity**: Deep understanding of rendering

### Cons
- **Implementation Effort**: 5-10 days to build robustly
- **Complexity**: Must handle batching, streaming, atlasing ourselves
- **No Built-In AA**: Must implement anti-aliasing (MSAA or edge AA in shader)
- **Gradient Support**: Must implement in shaders if needed
- **Maintenance**: Own all rendering code

### Technical Details

**Architecture**:
```cpp
class VectorBatchRenderer {
public:
    void BeginFrame();
    void Draw(const TessellatedMesh& mesh, const Transform& transform, MaterialID material);
    void EndFrame();

private:
    struct Batch {
        MaterialID material;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    std::unordered_map<MaterialID, Batch> m_batches;
    StreamingVBO m_vbo;
    TextureAtlasManager m_atlases;

    void FlushBatch(const Batch& batch);
};
```

**Streaming VBO Patterns**:

**Option 1: glBufferSubData** (OpenGL 3.3+)
```cpp
// Allocate large buffer
glGenBuffers(1, &vbo);
glBindBuffer(GL_ARRAY_BUFFER, vbo);
glBufferData(GL_ARRAY_BUFFER, 16 * 1024 * 1024, nullptr, GL_STREAM_DRAW);

// Each frame:
glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertexData);
glDrawElements(...);
```

**Option 2: Persistent Mapped Buffers** (OpenGL 4.4+, better performance)
```cpp
// One-time setup
GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
glBufferStorage(GL_ARRAY_BUFFER, bufferSize, nullptr, flags);
void* ptr = glMapBufferRange(GL_ARRAY_BUFFER, 0, bufferSize, flags);

// Each frame (zero-copy):
memcpy(ptr, vertexData, dataSize);
glDrawElements(...);
```

**Option 3: Buffer Orphaning** (OpenGL 3.3+, good compatibility)
```cpp
// Each frame:
glBufferData(GL_ARRAY_BUFFER, dataSize, nullptr, GL_STREAM_DRAW); // Orphan
glBufferSubData(GL_ARRAY_BUFFER, 0, dataSize, vertexData); // Upload
glDrawElements(...);
```

**Batching Strategy**:
```cpp
void VectorBatchRenderer::EndFrame() {
    // Sort batches by material to minimize state changes
    std::vector<MaterialID> sortedMaterials = SortByMaterial(m_batches);

    for (MaterialID mat : sortedMaterials) {
        Batch& batch = m_batches[mat];

        // Bind material (texture, shader, blend mode)
        BindMaterial(mat);

        // Upload geometry to streaming VBO
        m_vbo.Upload(batch.vertices, batch.indices);

        // Single draw call for entire batch
        glDrawElements(GL_TRIANGLES, batch.indices.size(), GL_UNSIGNED_INT, 0);

        // Clear batch for next frame
        batch.vertices.clear();
        batch.indices.clear();
    }
}
```

**Performance Characteristics**:
- Draw calls: ~10-50 per frame (one per material/texture)
- For 10,000 entities with 10 materials: **10 draw calls** ✅
- GPU time: ~2-4ms for rendering
- Upload time: ~0.5-2ms for streaming VBO

**Memory Footprint**:
- Streaming VBO: 16-50 MB (double/triple buffered)
- Texture atlases: 100-200 MB
- Batch accumulation: ~10-20 MB per frame

**Integration Complexity**:
- Medium-High: Must implement batching, streaming, atlasing
- Integrates directly with existing OpenGL renderer
- ~1000-2000 lines of code estimate

**Implementation Estimate**:

| Component | Lines of Code | Effort |
|-----------|---------------|--------|
| Streaming VBO Manager | 200-300 | 1-2 days |
| Batch Accumulator | 200-300 | 1-2 days |
| Texture Atlas Manager | 300-500 | 2-3 days |
| Material System | 150-250 | 1 day |
| Shaders (basic) | 100-200 | 1 day |
| Integration + Testing | - | 2-3 days |
| **Total** | **950-1550 lines** | **8-12 days** |

**Shaders**:

**Vertex Shader**:
```glsl
#version 330 core
layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

uniform mat4 uViewProjection;

out vec2 vTexCoord;
out vec4 vColor;

void main() {
    gl_Position = uViewProjection * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
    vColor = aColor;
}
```

**Fragment Shader** (basic):
```glsl
#version 330 core
in vec2 vTexCoord;
in vec4 vColor;

uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    vec4 texColor = texture(uTexture, vTexCoord);
    FragColor = texColor * vColor;
}
```

### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Multi-Tier Support | ✅ Excellent | Designed for all tiers |
| Batched Rendering | ✅ Excellent | Core feature, <100 draw calls |
| Performance (10k entities) | ✅ Excellent | Designed for this scale |
| Texture Atlasing | ✅ Excellent | Built-in atlas management |
| Integration | ✅ Good | Fits existing architecture |
| Quality | ⚠️ Depends | Must implement AA (MSAA or shader-based) |

### Recommendation
✅ **Primary Recommendation** - Best fit for game requirements. Provides optimal performance, full control, and is tailored to our specific needs. Implementation effort is justified by perfect alignment.

---

## Option D: Vello Integration (GPU Compute Shaders)

### Overview
Vello is a cutting-edge GPU compute-centric 2D renderer written in Rust. It uses compute shaders and prefix-sum algorithms to parallelize vector graphics rendering on the GPU.

**Repository**: https://github.com/linebender/vello
**License**: Apache 2.0 / MIT
**Language**: Rust (WGSL shaders)
**Dependencies**: wgpu (WebGPU API abstraction)

### How It Works

Vello uses **GPU compute shaders** for all rendering stages:

1. **Encoding**: Encode vector primitives to GPU buffers
2. **Flatten**: Compute shader flattens paths to segments
3. **Binning**: Spatial binning for rasterization
4. **Coarse Raster**: Tile-based rasterization
5. **Fine Raster**: Per-pixel rendering

**Rendering Pipeline**:
```
Vector Primitives → GPU Compute (Flatten/Bin/Raster) → Final Image
```

### Pros
- **Cutting Edge**: State-of-the-art GPU rendering approach
- **High Performance (Potential)**: Leverages GPU compute for vector rendering
- **Scalable**: Designed for thousands of shapes
- **No CPU Tessellation**: All work on GPU
- **Gradients/Filters**: Full 2D feature set
- **Active Development**: Improving rapidly

### Cons
- **Requires Compute Shaders**: OpenGL 4.3+ or Vulkan (not all platforms)
- **Complex Integration**: Rust library, requires FFI or rewrite in C++
- **WGPU Dependency**: WebGPU abstraction layer (large dependency tree)
- **Bleeding Edge**: API still evolving, less stable
- **Learning Curve**: Novel rendering approach, complex algorithms
- **Overkill**: Advanced for simple game shapes (designed for complex UIs)

### Technical Details

**Performance Characteristics**:
- **Potential**: Very high performance on modern GPUs
- Draw calls: Few (compute dispatches instead)
- GPU time: Depends on scene complexity
- **Unproven for game use case**: Mostly tested on UI rendering

**Platform Requirements**:
- OpenGL 4.3+ (compute shaders) or Vulkan
- Our target: OpenGL 3.3+ → **Incompatible** (could upgrade requirement)

**Integration Complexity**:
- Very High: Rust FFI or C++ port required
- Completely different rendering paradigm
- Significant learning and integration effort

**API** (Rust, simplified):
```rust
use vello::*;

let mut scene = Scene::new();
scene.fill(
    Fill::NonZero,
    Affine::IDENTITY,
    Color::rgb8(255, 0, 0),
    None,
    &circle,
);

// Render scene to texture
renderer.render_to_texture(&scene, &texture);
```

### Alignment with Game Requirements

| Requirement | Alignment | Notes |
|-------------|-----------|-------|
| Multi-Tier Support | ⚠️ Different paradigm | Not tier-based |
| Batched Rendering | ✅ Excellent | GPU compute handles batching |
| Performance (10k entities) | ✅ Likely excellent | Designed for scale |
| Texture Atlasing | ❌ Not needed | Different approach |
| Integration | ❌ Very difficult | Rust FFI or rewrite |
| Platform Requirements | ❌ Exceeds | Needs OpenGL 4.3+ |

### Recommendation
❌ **Defer to Future (Tier 4)** - **Too complex for initial implementation**. Cutting-edge technology that doesn't fit our current OpenGL 3.3+ target. Excellent for future optimization if CPU approach becomes bottleneck.

**Consider If**:
- Upgrade to OpenGL 4.3+ / Vulkan
- Willing to invest in Rust FFI or C++ port
- Need absolute maximum GPU performance
- After proving Tier 3 custom renderer works

---

## Option E: Hybrid Approach

### Overview
Combine multiple approaches for different tiers:
- **Tier 1 (Static)**: Pre-rasterize with Blend2D or NanoSVG rasterizer
- **Tier 2-3**: Custom batched renderer
- **UI Overlays**: NanoVG for menus/debug UI

### Pros
- **Best Tool for Each Job**: Leverage strengths of each approach
- **Pragmatic**: Use existing libraries where beneficial
- **Flexibility**: Can optimize each tier independently

### Cons
- **Complexity**: Multiple rendering systems to maintain
- **Integration Overhead**: Coordinate between systems
- **Code Duplication**: Some shared functionality

### Recommendation
⚠️ **Possible** - Pragmatic approach, but adds complexity. Consider if:
- Tier 1 pre-rasterization is difficult with custom code
- UI rendering needs NanoVG quality
- Willing to manage multiple systems

---

## Comparison Matrix

| Criteria | NanoVG | Blend2D | Custom Batched | Vello | Hybrid |
|----------|--------|---------|----------------|-------|--------|
| **Draw Calls (10k entities)** | ❌ 20k-40k | ✅ 10-50 | ✅ 10-50 | ✅ ~10 | ✅ 10-50 |
| **Performance (60 FPS)** | ❌ Insufficient | ⚠️ Tier 1-2 only | ✅ Excellent | ✅ Excellent | ✅ Excellent |
| **GPU Utilization** | ⚠️ Moderate | ❌ CPU-based | ✅ High | ✅ Very High | ✅ High |
| **Dynamic Entities (Tier 3)** | ❌ Too slow | ❌ Too slow | ✅ Designed for | ✅ Yes | ✅ Yes |
| **Static Backgrounds (Tier 1)** | ✅ Can rasterize | ✅ Excellent | ✅ Can batch | ⚠️ Overkill | ✅ Specialized |
| **Anti-Aliasing** | ✅ Excellent | ✅ Excellent | ⚠️ Must implement | ✅ Excellent | ✅ Mixed |
| **Gradient Support** | ✅ Built-in | ✅ Built-in | ⚠️ Must implement | ✅ Built-in | ✅ Mixed |
| **Implementation Effort** | ✅ Low (integrate lib) | ⚠️ Medium (integrate lib) | ⚠️ High (8-12 days) | ❌ Very High (weeks) | ⚠️ Medium-High |
| **Dependencies** | ✅ None (OpenGL 2.0+) | ⚠️ AsmJit | ✅ None (OpenGL 3.3+) | ❌ Many (wgpu, Rust) | ⚠️ Some |
| **Platform Requirements** | ✅ OpenGL 2.0+ | ✅ CPU only | ✅ OpenGL 3.3+ | ❌ OpenGL 4.3+ | ✅ OpenGL 3.3+ |
| **Maturity** | ✅✅✅ Very mature | ✅✅ Mature | ❌ New (our code) | ⚠️ Beta | ⚠️ Mixed |
| **Learning Curve** | ✅ Low | ⚠️ Medium | ⚠️ Medium-High | ❌ High | ⚠️ Medium-High |
| **Best For** | UI (hundreds) | Offline raster | Game entities | Cutting edge | Pragmatic mix |

## Decision Criteria

**Choose NanoVG if:**
- Only rendering UI (<1000 shapes)
- Want best anti-aliasing quality
- Don't need to batch thousands of entities
- Willing to accept 2-4 draw calls per shape

**Choose Blend2D if:**
- Only need Tier 1 (static pre-rasterization)
- Want CPU rendering for offline asset processing
- Quality is paramount over real-time performance

**Choose Custom Batched Renderer if:**
- Need optimal performance for 10,000+ dynamic entities ✅
- Want full control over rendering pipeline ✅
- Willing to implement batching, streaming, atlasing ✅
- Targeting OpenGL 3.3+ ✅
- **This is our primary use case** ✅

**Choose Vello if:**
- Willing to require OpenGL 4.3+ / Vulkan
- Want bleeding-edge GPU compute rendering
- Can integrate Rust library or port to C++
- Need maximum theoretical performance

**Choose Hybrid if:**
- Want best tool for each tier
- Willing to manage multiple systems
- Value pragmatism over simplicity

## Recommendations Framework

### Primary Recommendation: Custom Batched Renderer

**Rationale**:
1. ✅ **Perfect fit for game requirements** (10k dynamic entities)
2. ✅ **Optimal draw call count** (<100 per frame)
3. ✅ **Full control** over performance and memory
4. ✅ **Integrates cleanly** with existing OpenGL renderer
5. ✅ **Scalable** to future needs
6. ⚠️ **Implementation effort justified** (8-12 days for core system)

**Core Components to Build**:
1. Streaming VBO manager (persistent mapped buffers preferred)
2. Batch accumulator (group by material/texture)
3. Texture atlas manager (pack rasterized fills)
4. Basic shader pipeline (vertex color + texture)
5. Material system (blend modes, textures)

### Secondary Recommendation: Hybrid for Tier 1

**Consider using**:
- **NanoSVG Rasterizer** or **Blend2D** for Tier 1 static backgrounds
- Pre-rasterize at asset import or first load
- Feed textures into custom renderer's atlas system

**Benefits**:
- Leverage existing high-quality rasterizers
- Don't reinvent rasterization
- Focus custom renderer on triangle batching

### Future Consideration: Vello (Tier 4)

**Monitor**:
- If custom batched renderer hits performance limits
- If OpenGL 4.3+ becomes acceptable requirement
- If Vello matures and proves in game use cases

**Defer for now**: Too complex, exceeds platform requirements.

## Prototyping Plan

### Phase 1: Minimal Custom Renderer (3-4 days)
1. Streaming VBO with glBufferSubData (simple approach)
2. Single batch (no material sorting)
3. Single solid color shader
4. Test: Render 1,000 static triangles

### Phase 2: Batching System (2-3 days)
1. Batch accumulator (group by material)
2. Material sorting
3. Multiple material support
4. Test: Render 5,000 entities with 10 materials → verify <20 draw calls

### Phase 3: Texture Atlasing (2-3 days)
1. Simple rect-packing atlas generator
2. UV coordinate generation
3. Textured shader
4. Test: Render 10,000 textured entities

### Phase 4: Optimization (2-3 days)
1. Persistent mapped buffers (if OpenGL 4.4 available)
2. Frustum culling
3. Multi-threading tessellation
4. Profile and tune

### Phase 5: Polish (1-2 days)
1. MSAA or edge AA for anti-aliasing
2. Gradient shader (if needed)
3. Alpha blending modes
4. Integration with game ECS

**Total**: ~10-15 days for fully-featured custom renderer

## Related Documentation

- [architecture.md](./architecture.md) - How rendering backend fits into tiers
- [tessellation-options.md](./tessellation-options.md) - Feeding triangle data to renderer
- [batching-strategies.md](./batching-strategies.md) - Detailed batching implementation
- [performance-targets.md](./performance-targets.md) - Performance requirements

## References

**NanoVG**:
- Repository: https://github.com/memononen/nanovg
- Article: https://github.com/memononen/nanovg#how-it-works

**Blend2D**:
- Repository: https://github.com/blend2d/blend2d
- Benchmarks: https://blend2d.com/doc/group__blend2d__api__globals.html

**Vello**:
- Repository: https://github.com/linebender/vello
- Blog: https://linebender.org/blog/

**Batched Rendering**:
- LearnOpenGL Instancing: https://learnopengl.com/Advanced-OpenGL/Instancing
- Persistent Mapped Buffers: https://www.khronos.org/opengl/wiki/Buffer_Object#Persistent_mapping

## Revision History

- 2025-10-24: Initial comparative analysis based on research
