# Batched Text Rendering

## Problem Statement

Currently, text rendering in the UI framework has a critical architectural issue:

- **Shapes (Rectangle, Circle, Line)**: Use batched rendering via the Primitives API
  - `DrawRect()`, `DrawCircle()`, `DrawLine()` batch geometry into VBOs
  - Actual rendering happens later when `EndFrame()` is called

- **Text**: Uses immediate rendering via FontRenderer
  - `Text::Render()` calls `FontRenderer::RenderText()` which renders immediately with direct OpenGL calls
  - No batching, no deferred rendering

**This creates z-ordering bugs**: When a Button component renders:
1. Button calls `DrawRect()` to batch the background rectangle
2. Button calls `m_labelText.Render()` which renders text immediately
3. Later, `EndFrame()` flushes the batched rectangle, drawing it OVER the already-rendered text
4. Result: Text is invisible, covered by the button background

This violates the principle that **all rendering should go through the batched Primitives API**.

## Current Architecture

### FontRenderer (libs/ui/font/)
- Uses FreeType to load font glyphs
- Stores glyphs in a texture atlas
- Renders text using immediate OpenGL calls:
  ```cpp
  // Immediate rendering - happens NOW
  glBindVertexArray(m_VAO);
  glBindTexture(GL_TEXTURE_2D, m_textureAtlas);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, numQuads * 6);
  ```

### Primitives API (libs/renderer/primitives/)
- Batches geometry into buffers
- Defers rendering until `EndFrame()`:
  ```cpp
  // Batched - queued for later
  void DrawRect(const RectArgs& args) {
      // Add vertices to batch...
  }

  void EndFrame() {
      // NOW we render everything
      glBufferData(...);
      glDrawArrays(...);
  }
  ```

### Text Component (libs/ui/shapes/shapes.cpp)
```cpp
void Text::Render() const {
    ui::FontRenderer* fontRenderer = Renderer::Primitives::GetFontRenderer();
    fontRenderer->RenderText(text, position, scale, color);  // ← Immediate rendering!
}
```

## Solution: Unified Command Queue with Z-Ordering

### Design Goals

1. **Text must batch like other primitives**: `Text::Render()` should queue commands, not render immediately
2. **Proper z-ordering**: All primitives (shapes + text) sorted by z-index and rendered back-to-front
3. **Support transparency**: Back-to-front rendering enables correct alpha blending
4. **Support shadows and effects**: Shadows render under objects, effects layer correctly
5. **Minimal API changes**: `Text::Render()` signature stays the same
6. **Performance**: Command queue sorting should be fast enough for UI (hundreds of elements, not thousands)

### Proposed Architecture

**Industry-Standard Approach**: Command queue with material-based batching, opaque/transparent split, and z-index sorting.

Based on research into Unity, Unreal, Chrome, and other modern engines, the correct solution is:

#### 1. Unified Command Queue (Not Type-Specific Batches)

**Replace type-specific batches with a command queue:**

```cpp
namespace Renderer::Primitives {
    // Material identifier for batching
    struct MaterialID {
        GLuint shader;
        GLuint texture;
        BlendMode blendMode;

        bool operator==(const MaterialID& other) const {
            return shader == other.shader &&
                   texture == other.texture &&
                   blendMode == other.blendMode;
        }
    };

    // Base draw command
    struct DrawCommand {
        MaterialID material;          // What GPU state does this need?
        float zIndex;                 // Render order
        bool isTransparent;           // Opaque vs transparent pass
        std::optional<Rect> scissor;  // Optional clipping region
        const char* id;               // Debug identifier

        // Vertex data (triangles, lines, etc.)
        std::vector<float> vertices;
        GLenum primitiveType;  // GL_TRIANGLES, GL_LINES, etc.
    };

    namespace {
        std::vector<DrawCommand> g_commandQueue;
        ui::FontRenderer* g_fontRenderer = nullptr;
    }
}
```

**Key insight from research**: What matters for batching is **GPU state** (shader, texture, blend mode), not semantic shape types (rect, circle, text). Rectangles, circles, polygons, stars - all become triangles with the same shader.

#### 2. API Surface Remains Simple

**Existing API stays the same, just queues commands:**

```cpp
void DrawRect(const RectArgs& args) {
    DrawCommand cmd;
    cmd.material = GetColorMaterial();  // Solid color shader
    cmd.zIndex = args.zIndex;
    cmd.isTransparent = args.style.fillColor.a < 1.0F;
    cmd.scissor = GetCurrentScissor();
    cmd.primitiveType = GL_TRIANGLES;

    // Tessellate rectangle into vertices
    GenerateRectVertices(args, cmd.vertices);

    g_commandQueue.push_back(std::move(cmd));
}

void DrawText(const TextArgs& args) {
    if (!g_fontRenderer) return;

    DrawCommand cmd;
    cmd.material = GetTextMaterial(g_fontRenderer->GetAtlasTexture());
    cmd.zIndex = args.zIndex;
    cmd.isTransparent = args.color.a < 1.0F;
    cmd.scissor = GetCurrentScissor();
    cmd.primitiveType = GL_TRIANGLES;

    // Generate text quads
    std::vector<GlyphQuad> quads;
    g_fontRenderer->GenerateGlyphQuads(args.text, args.position,
                                       args.scale, args.color, quads);

    // Convert to vertices
    for (const auto& quad : quads) {
        GenerateQuadVertices(quad, cmd.vertices);
    }

    g_commandQueue.push_back(std::move(cmd));
}

void DrawCircle(const CircleArgs& args) {
    // Tessellate circle, add to command queue
    // Uses same material as rectangles (both solid color)
}
```

#### 3. Two-Pass Rendering with Material Batching

**Critical for transparency**: Opaque and transparent objects must render separately with different sort orders.

```cpp
void EndFrame() {
    if (g_commandQueue.empty()) return;

    // ===== PASS 1: OPAQUE OBJECTS =====
    // Sort opaque objects: front-to-back by z-index (early-Z optimization)
    // Then by material (minimize state changes)

    auto transparentStart = std::partition(
        g_commandQueue.begin(), g_commandQueue.end(),
        [](const DrawCommand& cmd) { return !cmd.isTransparent; }
    );

    std::stable_sort(g_commandQueue.begin(), transparentStart,
        [](const DrawCommand& a, const DrawCommand& b) {
            // Sort key: material first (batching), then depth front-to-back
            if (a.material != b.material) return a.material < b.material;
            return a.zIndex > b.zIndex;  // Higher z = closer = render first
        });

    // Render opaque pass with depth writes enabled
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    RenderBatch(g_commandQueue.begin(), transparentStart);

    // ===== PASS 2: TRANSPARENT OBJECTS =====
    // Sort transparent objects: back-to-front by z-index (correct blending)
    // Then by material (minimize state changes within z-level)

    std::stable_sort(transparentStart, g_commandQueue.end(),
        [](const DrawCommand& a, const DrawCommand& b) {
            // Sort key: z-index first (visual correctness), then material
            if (std::abs(a.zIndex - b.zIndex) > 0.001F) {
                return a.zIndex < b.zIndex;  // Back-to-front
            }
            return a.material < b.material;  // Batch same z-level
        });

    // Render transparent pass with depth writes disabled
    glDepthMask(GL_FALSE);
    RenderBatch(transparentStart, g_commandQueue.end());

    g_commandQueue.clear();
}
```

#### 4. Batching Within Render Pass

**Minimize state changes by batching consecutive commands with same material:**

```cpp
void RenderBatch(CommandIterator begin, CommandIterator end) {
    if (begin == end) return;

    MaterialID currentMaterial = {};
    std::optional<Rect> currentScissor;
    std::vector<float> batchVertices;
    GLenum currentPrimitiveType = GL_TRIANGLES;

    auto flushBatch = [&]() {
        if (batchVertices.empty()) return;

        // Upload and draw
        glBufferData(GL_ARRAY_BUFFER,
                     batchVertices.size() * sizeof(float),
                     batchVertices.data(),
                     GL_DYNAMIC_DRAW);
        glDrawArrays(currentPrimitiveType, 0,
                     batchVertices.size() / GetVertexSize());

        batchVertices.clear();
    };

    for (auto it = begin; it != end; ++it) {
        const auto& cmd = *it;

        // State change detection
        bool needFlush = false;

        // Material change (most expensive)
        if (cmd.material != currentMaterial) {
            needFlush = true;
            BindMaterial(cmd.material);
            currentMaterial = cmd.material;
        }

        // Scissor change (forces flush)
        if (cmd.scissor != currentScissor) {
            needFlush = true;
            SetScissor(cmd.scissor);
            currentScissor = cmd.scissor;
        }

        // Primitive type change
        if (cmd.primitiveType != currentPrimitiveType) {
            needFlush = true;
            currentPrimitiveType = cmd.primitiveType;
        }

        if (needFlush) {
            flushBatch();
        }

        // Add to current batch
        batchVertices.insert(batchVertices.end(),
                           cmd.vertices.begin(),
                           cmd.vertices.end());
    }

    flushBatch();  // Final batch
}
```

#### 5. FontRenderer Modification

**Add batch mode support:**

```cpp
class FontRenderer {
public:
    // Immediate mode (keep for compatibility)
    void RenderText(const std::string& text, glm::vec2 position,
                    float scale, glm::vec3 color);

    // NEW: Batch mode - generate quads without rendering
    struct GlyphQuad {
        glm::vec2 position;
        glm::vec2 size;
        glm::vec2 uvMin;
        glm::vec2 uvMax;
        glm::vec4 color;  // RGBA for transparency
    };

    void GenerateGlyphQuads(const std::string& text, glm::vec2 position,
                           float scale, glm::vec4 color,
                           std::vector<GlyphQuad>& outQuads) const;

    GLuint GetAtlasTexture() const { return m_textureAtlas; }

    // Helper for vertex generation
    glm::vec2 MeasureText(const std::string& text, float scale) const;
    float GetAscent(float scale) const;
};
```

### Z-Ordering Examples

**Example 1: Button with text label**
```cpp
// Button::Render() queues commands with z-index:
DrawRect({.zIndex = 1.0F, ...});       // Background
DrawText({.zIndex = 1.1F, ...});       // Label on top

// EndFrame() sorts by z-index → text renders after rect ✓
```

**Example 2: Layered UI with transparency**
```cpp
// Background layer
DrawRect({.zIndex = 0.0F, .isTransparent = false, ...});

// Semi-transparent overlay
DrawRect({.zIndex = 1.0F, .isTransparent = true, .alpha = 0.5F, ...});

// Text on top
DrawText({.zIndex = 2.0F, ...});

// EndFrame() renders:
// 1. Opaque rect (z=0.0)
// 2. Transparent rect (z=1.0) - blends correctly
// 3. Text (z=2.0) - visible on top ✓
```

**Example 3: Shadow effect**
```cpp
// Shadow (rendered first, behind object)
DrawRect({.zIndex = 1.0F, .color = {0,0,0,0.3F}, .offset = {2,2}, ...});

// Object (rendered second, on top)
DrawRect({.zIndex = 1.1F, ...});
DrawText({.zIndex = 1.2F, ...});

// Shadow renders behind due to lower z-index ✓
```

## Implementation Plan

### Phase 1: Command Queue Infrastructure
- [ ] Define `DrawCommand` struct with material, z-index, transparency flag
- [ ] Define `MaterialID` for batching key
- [ ] Add `g_commandQueue` to Primitives API
- [ ] Implement `GetColorMaterial()` and `GetTextMaterial()` helpers
- [ ] Add `isTransparent` detection based on alpha channel

### Phase 2: Modify Existing Draw Functions
- [ ] Update `DrawRect()` to queue commands instead of immediate batching
- [ ] Update `DrawCircle()` to queue commands
- [ ] Update `DrawLine()` to queue commands
- [ ] Keep existing tessellation code, just queue instead of batch

### Phase 3: FontRenderer Batch Support
- [ ] Add `GenerateGlyphQuads()` to FontRenderer (no rendering)
- [ ] Add `GlyphQuad` struct with position, size, UV, color
- [ ] Implement `DrawText()` in Primitives API that queues text commands
- [ ] Update `Text::Render()` to use `DrawText()`

### Phase 4: Two-Pass Rendering
- [ ] Implement opaque/transparent partitioning in `EndFrame()`
- [ ] Add opaque pass: sort by material → z-index, enable depth writes
- [ ] Add transparent pass: sort by z-index → material, disable depth writes
- [ ] Implement `RenderBatch()` with state change minimization

### Phase 5: Testing & Profiling
- [ ] Test button scene - verify text renders correctly
- [ ] Test transparency - semi-transparent overlays blend correctly
- [ ] Test z-ordering - shadows render behind objects
- [ ] Performance test: Measure draw calls, state changes, frame time
- [ ] Verify 10,000+ primitives @ 60 FPS target

### Phase 6: Debug & Optimization
- [ ] Add debug mode to log draw calls, state changes, batches
- [ ] Profile state change costs vs draw call costs
- [ ] Consider vertex buffer pre-allocation for dynamic geometry
- [ ] Document performance characteristics

## Alternative Considered: Auto-Flush Before Text

We could make `Text::Render()` call `EndFrame()` before rendering:

```cpp
void Text::Render() const {
    Renderer::Primitives::EndFrame();  // Flush batches
    fontRenderer->RenderText(...);      // Immediate render
}
```

**Rejected because:**
- Terrible for performance (flushes batch for every text element)
- Breaks batching abstraction
- Doesn't solve the root problem (text not being batched)

## Performance Considerations

### The Real Bottleneck: State Changes

**Industry research findings** (Unity, Unreal, Chrome, Dear ImGui):
- **Shader change**: 1000× more expensive than draw call
- **Texture change**: 100× more expensive than draw call
- **Draw call itself**: Relatively cheap

**Implication**: Minimizing state changes is MORE important than minimizing draw calls.

### Expected Performance Profile

**Target**: 10,000+ primitives @ 60 FPS with <100 draw calls

**Estimated breakdown**:
- Vector entities (10-20 materials): 10-20 draw calls
- UI elements (5-10 materials): 5-10 draw calls
- Text (1 font atlas): 1 draw call per font
- Effects/shadows: 5-10 draw calls
- **Total**: 21-41 draw calls (well under target)

### Memory Costs

**Command Queue**:
- Per command: ~100-200 bytes (struct + vertex data)
- 1000 UI elements: ~100-200KB (acceptable)
- Cleared every frame (no accumulation)

**Vertex Buffers**:
- Dynamic geometry uploaded each frame
- Consider persistent mapped buffers (OpenGL 4.4+) for zero-copy
- Triple buffering to avoid GPU stalls

### Transparency Cost

**Opaque objects**: Front-to-back rendering enables early-Z rejection
- GPU can skip pixel shader for occluded fragments
- **Huge performance win** on complex UIs

**Transparent objects**: Back-to-front rendering required for correct blending
- No early-Z rejection (depth writes disabled)
- Every pixel runs fragment shader (**overdraw cost**)
- **Mobile impact**: Fill-rate bound, avoid large transparent areas

### Sorting Cost

**CPU overhead** for sorting command queue:
- 100 commands: ~1-2 µs (negligible)
- 1000 commands: ~10-20 µs (acceptable)
- 10,000 commands: ~100-200 µs (3% of 16ms frame budget)

**Mitigation**: Use stable_sort to preserve insertion order for equal z-index

### Batching Effectiveness

**Best case**: All primitives use same material
- Result: 1 draw call for entire frame
- Limited by texture atlas size

**Typical case**: 10-30 unique materials
- Result: 10-30 draw calls per frame
- Excellent performance on all platforms

**Worst case**: Every primitive different material
- Result: N draw calls for N primitives
- Still correct, just slower (falls back to current performance)

## Key Design Decisions

### 1. Material-Based Batching (Not Shape-Type)

**Why**: GPU doesn't care about semantics (rect vs circle). What matters:
- Shader program
- Texture binding
- Blend mode

**Benefit**: Rectangles, circles, polygons, stars all batch together if they use the same material.

### 2. Two-Pass Rendering (Opaque/Transparent Split)

**Why**: Different sort orders required:
- Opaque: Front-to-back (early-Z optimization)
- Transparent: Back-to-front (correct blending)

**Benefit**: Correct visuals + optimal performance

### 3. Z-Index Explicit (Not Call-Order)

**Why**: Call order is fragile and hard to reason about with complex UIs.

**Benefit**:
- Explicit control over render order
- Independent of code structure
- Handles nested layers correctly

### 4. Premultiplied Alpha (Standard)

**Why**: Industry standard for GPU rendering
- Correct filtering/interpolation
- Single blend mode for all transparency
- Better batching

**Implementation**: Multiply RGB by A in fragment shader output

## Related Documents

- [Primitives API](/libs/renderer/primitives/primitives.h) - Current batching implementation
- [FontRenderer](/libs/ui/font/font_renderer.h) - Current text rendering
- [Text Component](/libs/ui/shapes/shapes.h) - Text shape definition
- [UI Framework Architecture](./architecture.md) - Overall UI design
