# Primitive Rendering API Design

Created: 2025-10-26
Status: Design

## Overview

This document defines the unified primitive rendering API that serves as the foundation for all 2D drawing in world-sim. This API is used by:
- RmlUI backend (screen-space UI panels)
- Game world rendering (tiles, entities)
- World-space UI (health bars, tooltips)
- Custom UI components (if building `.Args{}` wrappers)

**Design Goal:** One consistent way to draw rectangles, text, and textures throughout the entire codebase.

## Architecture Principles

### 1. Immediate Mode API, Retained Mode Implementation

**API (Immediate Mode):**
```cpp
// Call every frame - no persistent objects
Renderer::Primitives::DrawRect({100, 100, 200, 50}, Color::Red);
```

**Implementation (Retained Mode):**
```cpp
// Internally batches geometry, minimizes GPU state changes
void Primitives::DrawRect(Rect bounds, Color color) {
    s_batcher.AddQuad(bounds, color);  // ← Accumulates in batch
}

void Primitives::EndFrame() {
    s_batcher.Flush();  // ← Single GPU upload + draw call
}
```

**Why:**
- Simple API (no memory management)
- Performance (batching, minimal draw calls)
- Flexibility (can optimize implementation without changing API)

### 2. Layered on Existing Renderer

**Does NOT replace your renderer, extends it:**

```
┌─────────────────────────────────────┐
│   High-Level APIs                   │
│   (RmlUI, Game Code, Custom UI)     │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   Primitives API (THIS)             │
│   DrawRect, DrawText, DrawTexture   │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   Your Renderer                     │
│   Shader, VBO, Batching, etc.       │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│   OpenGL                            │
└─────────────────────────────────────┘
```

**Primitives API is a convenience layer, not a replacement.**

### 3. Integration with Vector Graphics

**Primitives for simple shapes, Vector Graphics for complex:**

```cpp
// Simple colored rectangle
Primitives::DrawRect(bounds, color);  // ← Fast, uses primitives

// Complex SVG decoration
m_vectorRenderer->RenderSVG(treeAsset);  // ← Uses vector graphics system
```

**Not redundant - different use cases:**
- Primitives: UI elements, health bars, selection boxes (axis-aligned, simple)
- Vector Graphics: Tiles, entities, decorations (complex paths, animations)

## API Design

### Core Functions

```cpp
// libs/renderer/include/renderer/primitives.h

namespace Renderer {

// Initialize primitive rendering system
void InitPrimitives(Renderer* renderer);
void ShutdownPrimitives();

// Frame lifecycle
void BeginPrimitiveFrame();
void EndPrimitiveFrame();  // Flushes all batches

// --- Drawing Functions ---

// Rectangles
void DrawRect(const Rect& bounds, const Color& color);
void DrawRectBorder(const Rect& bounds, const Color& color,
                   float borderWidth, float cornerRadius = 0.0f);
void DrawRectGradient(const Rect& bounds, const Color& topLeft,
                     const Color& topRight, const Color& bottomLeft,
                     const Color& bottomRight);

// Text
struct TextStyle {
    FontHandle font;
    float fontSize = 16.0f;
    Color color = Color::White;
    TextAlign align = TextAlign::Left;
    bool bold = false;
    bool italic = false;
};

void DrawText(const std::string& text, const Vec2& position,
             const TextStyle& style);

Vec2 MeasureText(const std::string& text, const TextStyle& style);

// Textures
void DrawTexture(TextureHandle texture, const Rect& destBounds,
                const Rect& sourceUV = {0, 0, 1, 1},
                const Color& tint = Color::White);

void DrawTextureSliced(TextureHandle texture, const Rect& destBounds,
                      const RectSlicing& slicing);  // 9-slice scaling

// Lines
void DrawLine(const Vec2& start, const Vec2& end,
             const Color& color, float width = 1.0f);

void DrawPolyline(const std::vector<Vec2>& points,
                 const Color& color, float width = 1.0f,
                 bool closed = false);

// Circles (useful for minimaps, selection circles)
void DrawCircle(const Vec2& center, float radius,
               const Color& color, int segments = 32);

void DrawCircleBorder(const Vec2& center, float radius,
                     const Color& color, float borderWidth,
                     int segments = 32);

// Clipping
void PushScissor(const Rect& clipRect);
void PopScissor();
Rect GetCurrentScissor();

// Transform (for world-space rendering)
void PushTransform(const Mat4& transform);
void PopTransform();
Mat4 GetCurrentTransform();

// State
void SetBlendMode(BlendMode mode);
BlendMode GetBlendMode();

} // namespace Renderer
```

### Supporting Types

```cpp
// Basic types (already in foundation)
struct Vec2 { float x, y; };
struct Rect { float x, y, width, height; };
struct Color { float r, g, b, a; };
struct Mat4 { /* 4x4 matrix */ };

// Enums
enum class TextAlign {
    Left,
    Center,
    Right
};

enum class BlendMode {
    Alpha,       // Standard alpha blending
    Additive,    // Additive blending (lights, effects)
    Multiply,    // Multiplicative blending
    None         // No blending
};

// 9-slice scaling (for UI panels)
struct RectSlicing {
    float left, right, top, bottom;  // Border widths
};

// Font handle (from font system)
struct FontHandle {
    uint32_t id;
    // Internal handle to font texture atlas
};
```

## Implementation Architecture

### Batching Strategy

**Goal:** Minimize draw calls by batching geometry

```cpp
// Internal batch accumulator
class PrimitiveBatcher {
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    TextureHandle m_currentTexture;
    BlendMode m_currentBlendMode;

public:
    void AddQuad(Rect bounds, Color color);
    void AddTexturedQuad(Rect bounds, Rect uv, TextureHandle texture, Color tint);

    void Flush();  // Upload to GPU and draw
};
```

**Batching rules:**
1. Same texture → Same batch
2. Same blend mode → Same batch
3. Different texture/blend → Flush and start new batch
4. Scissor change → Flush and start new batch

**Performance target:**
- 10,000 rectangles: <100 draw calls
- Text rendering: Font atlas allows batching characters

### Text Rendering Integration

**Uses existing SDF font system (from vector graphics):**

```cpp
void DrawText(const std::string& text, const Vec2& position,
             const TextStyle& style) {
    // Get font atlas texture
    auto atlas = g_fontManager->GetAtlas(style.font);

    // For each character:
    for (char c : text) {
        // Get character UV coords from atlas
        Rect uv = atlas->GetCharUV(c);
        Rect bounds = {position.x, position.y, charWidth, charHeight};

        // Batch as textured quad
        s_batcher.AddTexturedQuad(bounds, uv, atlas->texture, style.color);

        position.x += charWidth;
    }
}
```

**Font system is shared with RmlUI:**
- RmlUI uses same font backend
- Both benefit from SDF font atlas
- Consistent text rendering across UI and game

### Scissor/Clipping Stack

**For scrollable containers:**

```cpp
class ClipStack {
    std::stack<Rect> m_scissors;
    Rect m_currentScissor;

public:
    void Push(const Rect& scissor) {
        // Intersect with current scissor (nested clipping)
        Rect newScissor = Intersect(m_currentScissor, scissor);

        m_scissors.push(m_currentScissor);
        m_currentScissor = newScissor;

        // Apply to OpenGL
        glScissor(newScissor.x, newScissor.y,
                 newScissor.width, newScissor.height);
    }

    void Pop() {
        m_currentScissor = m_scissors.top();
        m_scissors.pop();

        glScissor(m_currentScissor.x, m_currentScissor.y,
                 m_currentScissor.width, m_currentScissor.height);
    }
};
```

**Usage for scrollable panel:**
```cpp
void RenderScrollPanel() {
    Renderer::PushScissor(panelBounds);  // Clip to panel

    // Draw content (anything outside is clipped)
    for (auto& item : items) {
        Renderer::DrawRect(item.bounds, item.color);
    }

    Renderer::PopScissor();
}
```

### Transform Stack (World-Space Rendering)

**For rendering in world coordinates:**

```cpp
void RenderHealthBar(Entity entity) {
    // Set up world-to-screen transform
    Mat4 worldTransform = GetWorldToScreenTransform();
    Renderer::PushTransform(worldTransform);

    // Draw in world coordinates
    Vec2 worldPos = entity.position + Vec2(0, -1.0f);  // 1 meter above entity
    Renderer::DrawRect({worldPos.x - 0.5f, worldPos.y, 1.0f, 0.1f}, Color::Red);

    Renderer::PopTransform();
}
```

**Implementation:**
```cpp
class TransformStack {
    std::stack<Mat4> m_transforms;
    Mat4 m_current = Mat4::Identity();

public:
    void Push(const Mat4& transform) {
        m_transforms.push(m_current);
        m_current = m_current * transform;  // Concatenate
    }

    void Pop() {
        m_current = m_transforms.top();
        m_transforms.pop();
    }

    Vec2 TransformPoint(const Vec2& point) {
        Vec4 p = m_current * Vec4(point.x, point.y, 0, 1);
        return {p.x, p.y};
    }
};
```

## Usage Examples

### Example 1: Health Bar

```cpp
void RenderHealthBar(Entity entity, float healthPercent) {
    Vec2 screenPos = WorldToScreen(entity.position);
    Vec2 barPos = screenPos + Vec2(-25, -30);  // Above entity

    const float barWidth = 50.0f;
    const float barHeight = 5.0f;

    // Background
    Renderer::DrawRect(
        {barPos.x, barPos.y, barWidth, barHeight},
        Color(0.2f, 0.2f, 0.2f, 0.8f)
    );

    // Health fill (green to red based on health)
    Color healthColor = healthPercent > 0.3f ? Color::Green : Color::Red;
    Renderer::DrawRect(
        {barPos.x, barPos.y, barWidth * healthPercent, barHeight},
        healthColor
    );

    // Border
    Renderer::DrawRectBorder(
        {barPos.x, barPos.y, barWidth, barHeight},
        Color::Black,
        1.0f,  // 1px border
        1.0f   // 1px corner radius
    );
}
```

### Example 2: Resource Counter HUD

```cpp
void RenderResourceCounter(const std::string& resource, int amount) {
    Vec2 position = {20, 20};

    // Icon (texture from atlas)
    TextureHandle icon = GetResourceIcon(resource);
    Renderer::DrawTexture(icon, {position.x, position.y, 32, 32});

    // Text
    Renderer::DrawText(
        std::to_string(amount),
        position + Vec2(40, 8),
        {
            .font = g_uiFont,
            .fontSize = 18.0f,
            .color = Color::White,
            .align = TextAlign::Left
        }
    );
}
```

### Example 3: Minimap

```cpp
void RenderMinimap(const Rect& minimapBounds) {
    // Background
    Renderer::DrawRect(minimapBounds, Color(0.1f, 0.1f, 0.1f, 0.9f));

    // Border
    Renderer::DrawRectBorder(minimapBounds, Color::White, 2.0f);

    // Clip to minimap bounds
    Renderer::PushScissor(minimapBounds);

    // Draw world tiles (scaled down)
    for (auto& tile : visibleTiles) {
        Vec2 minimapPos = WorldToMinimap(tile.position, minimapBounds);
        Renderer::DrawRect(
            {minimapPos.x, minimapPos.y, 2, 2},
            tile.color
        );
    }

    // Draw entities
    for (auto& entity : entities) {
        Vec2 minimapPos = WorldToMinimap(entity.position, minimapBounds);
        Renderer::DrawCircle(minimapPos, 1.5f, Color::Yellow);
    }

    Renderer::PopScissor();
}
```

### Example 4: Tooltip

```cpp
void RenderTooltip(const std::string& text, const Vec2& mousePos) {
    // Measure text to size background
    Vec2 textSize = Renderer::MeasureText(text, {
        .font = g_uiFont,
        .fontSize = 14.0f
    });

    const Vec2 padding = {10, 6};
    Rect tooltipBounds = {
        mousePos.x + 10,
        mousePos.y + 10,
        textSize.x + padding.x * 2,
        textSize.y + padding.y * 2
    };

    // Background with rounded corners
    Renderer::DrawRect(tooltipBounds, Color(0.2f, 0.2f, 0.2f, 0.95f));
    Renderer::DrawRectBorder(tooltipBounds, Color(0.6f, 0.6f, 0.6f, 1.0f),
                            1.0f, 4.0f);

    // Text
    Renderer::DrawText(
        text,
        {tooltipBounds.x + padding.x, tooltipBounds.y + padding.y},
        {
            .font = g_uiFont,
            .fontSize = 14.0f,
            .color = Color::White
        }
    );
}
```

### Example 5: RmlUI Backend Integration

```cpp
class RmlUIBackend : public Rml::RenderInterface {
    void RenderGeometry(Rml::Vertex* vertices, int num_vertices,
                       int* indices, int num_indices,
                       Rml::TextureHandle texture,
                       const Rml::Vector2f& translation) override {

        // RmlUI gives us raw triangles, convert to textured quads where possible
        // For simplicity, just render as-is

        for (int i = 0; i < num_indices; i += 6) {
            // RmlUI uses 2 triangles per quad (6 indices)
            // Extract quad bounds from vertices
            Vec2 pos = {vertices[indices[i]].position.x,
                       vertices[indices[i]].position.y};
            Vec2 size = /* calculate from vertices */;
            Rect uv = /* extract UV coords */;

            // Draw via primitives API
            Renderer::DrawTexture(
                static_cast<TextureHandle>(texture),
                {pos.x + translation.x, pos.y + translation.y, size.x, size.y},
                uv
            );
        }
    }

    void EnableScissorRegion(bool enable) override {
        if (!enable) {
            Renderer::PopScissor();
        }
    }

    void SetScissorRegion(int x, int y, int width, int height) override {
        Renderer::PushScissor({(float)x, (float)y, (float)width, (float)height});
    }
};
```

## Performance Targets

### Batching Performance

**Goal:** Minimize GPU state changes and draw calls

| Scenario | Primitives | Draw Calls | Target |
|----------|------------|------------|--------|
| 100 health bars | 300 rects | 1-2 | <0.1ms |
| Resource HUD (10 counters) | 10 icons + 10 text | 2-3 | <0.05ms |
| Minimap (1000 tiles) | 1000 rects | 1-5 | <0.2ms |
| UI panel (RmlUI, 100 elements) | 200+ quads | 5-10 | <0.3ms |

**Overall budget:** <1ms per frame for all primitive rendering

### Memory Budget

**Batch buffers:**
- Vertex buffer: 64KB (reused each frame)
- Index buffer: 32KB (reused each frame)
- Total: <100KB

**Per-frame allocations:** Zero (reuse buffers)

## Integration Points

### With Existing Renderer

**Primitives API uses your renderer's internals:**

```cpp
void Primitives::Init(Renderer* renderer) {
    s_renderer = renderer;

    // Create shader for primitive rendering
    s_shader = renderer->CreateShader("primitive.vert", "primitive.frag");

    // Create VBOs for batching
    s_vertexBuffer = renderer->CreateDynamicVBO(64 * 1024);
    s_indexBuffer = renderer->CreateDynamicIBO(32 * 1024);

    // Create white texture for untextured quads
    s_whiteTexture = renderer->CreateTexture1x1(Color::White);
}

void Primitives::EndFrame() {
    s_batcher.Flush();  // Uploads to VBO and calls renderer->Draw()
}
```

### With Vector Graphics System

**Clear division of responsibility:**

```cpp
// Primitive rendering: Simple axis-aligned shapes
Renderer::DrawRect(bounds, color);  // Fast, batched

// Vector graphics: Complex SVG paths
m_vectorRenderer->RenderSVG(asset);  // Tessellated, animated

// Text: Shared SDF font system
Renderer::DrawText(text, pos, style);  // Uses font atlas
m_vectorRenderer->RenderText(text);    // Same font backend
```

**No conflict - complementary systems.**

### With RmlUI

**RmlUI backend is a thin wrapper:**

```cpp
// RmlUI calls this
backend->RenderGeometry(vertices, indices, texture);

// Backend calls primitives
Renderer::DrawTexture(texture, bounds, uv);

// Both end up in same batch!
```

## Shader Requirements

### Primitive Vertex Shader

```glsl
// primitive.vert
#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
layout(location = 2) in vec4 a_color;

uniform mat4 u_projection;
uniform mat4 u_transform;

out vec2 v_texCoord;
out vec4 v_color;

void main() {
    v_texCoord = a_texCoord;
    v_color = a_color;
    gl_Position = u_projection * u_transform * vec4(a_position, 0.0, 1.0);
}
```

### Primitive Fragment Shader

```glsl
// primitive.frag
#version 330 core

in vec2 v_texCoord;
in vec4 v_color;

uniform sampler2D u_texture;
uniform bool u_hasTexture;

out vec4 FragColor;

void main() {
    if (u_hasTexture) {
        FragColor = texture(u_texture, v_texCoord) * v_color;
    } else {
        FragColor = v_color;
    }
}
```

## File Structure

```
libs/renderer/
├── include/renderer/
│   ├── primitives.h          # Public API
│   └── primitive_types.h     # Color, Rect, TextStyle, etc.
│
└── src/
    ├── primitives.cpp         # API implementation
    ├── primitive_batcher.h    # Internal batching system
    ├── primitive_batcher.cpp
    ├── clip_stack.h           # Scissor management
    ├── clip_stack.cpp
    ├── transform_stack.h      # Transform management
    └── transform_stack.cpp
```

## Testing Strategy

**Unit tests:**
```cpp
TEST(Primitives, BatchingReducesDrawCalls) {
    Renderer::BeginPrimitiveFrame();

    // Draw 100 rects (same color, no texture)
    for (int i = 0; i < 100; i++) {
        Renderer::DrawRect({{i * 10, 0, 10, 10}}, Color::Red);
    }

    Renderer::EndPrimitiveFrame();

    // Verify: Should be 1 draw call, not 100
    ASSERT_EQ(GetDrawCallCount(), 1);
}
```

**Integration tests:**
```cpp
TEST(Primitives, RmlUIBackendUsesPrimitives) {
    // Create RmlUI document with button
    auto doc = LoadRML("<button>Test</button>");

    // Render
    doc->Render();

    // Verify primitives API was called
    ASSERT_TRUE(PrimitivesWereCalled());
}
```

## Migration Path

**Phase 1:** Implement primitives API
**Phase 2:** Use for simple HUD elements (resource counters)
**Phase 3:** Integrate RmlUI backend
**Phase 4:** Replace all direct OpenGL calls in game with primitives

**Backward compatibility:** Old rendering code continues to work, gradually migrate.

## Related Documentation

- [rendering-boundaries.md](./rendering-boundaries.md) - When to use primitives vs RmlUI vs vector graphics
- [Vector Graphics Architecture](/docs/technical/vector-graphics/architecture.md) - Complementary system for complex paths
- [Renderer Architecture](/docs/technical/renderer-architecture.md) - Lower-level renderer this builds on

## Open Questions

1. **Text rendering complexity:** SDF vs raster fonts?
   - Decision: Use SDF (crisp at any zoom, matches vector aesthetic)

2. **Line anti-aliasing:** Software or shader-based?
   - Recommendation: Shader-based (faster, GPU does the work)

3. **Circle rendering:** Real circles or tessellated polygons?
   - Recommendation: Tessellated for batching efficiency

4. **Gradient fills:** Linear/radial gradients?
   - Start simple (solid colors only), add later if needed

## Revision History

- 2025-10-26: Initial primitive rendering API design
