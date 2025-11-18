# GPU-Based SDF Rendering for UI Primitives

**Created**: 2025-11-18
**Last Updated**: 2025-11-18
**Status**: Design
**Priority**: High

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Background: Signed Distance Fields](#background-signed-distance-fields)
3. [Current Implementation Analysis](#current-implementation-analysis)
4. [Proposed Architecture](#proposed-architecture)
5. [Performance Analysis](#performance-analysis)
6. [Border Positioning Behavior](#border-positioning-behavior)
7. [Integration Points](#integration-points)
8. [Testing Strategy](#testing-strategy)
9. [Migration Plan](#migration-plan)
10. [Alternatives Considered](#alternatives-considered)
11. [Performance Targets](#performance-targets)
12. [Open Questions](#open-questions)
13. [Future Enhancements](#future-enhancements)
14. [Related Documentation](#related-documentation)
15. [Appendices](#appendices)

---

## Executive Summary

### Problem Statement

Current UI primitive rendering uses CPU tessellation for borders, requiring 4 separate line draw calls per rectangle with no corner radius support. This creates significant geometry overhead:

- **Rectangle with border**: 20 vertices, 30 indices (1 fill quad + 4 border line quads)
- **1000 rectangles**: 20,000 vertices requiring CPU tessellation and GPU upload
- **No corner radius**: Cannot render modern rounded UI elements
- **Corner overdraw**: Border corners drawn 4 times (waste)

### Proposed Solution

Implement GPU-based Signed Distance Field (SDF) rendering in the fragment shader to draw rounded rectangles with borders in a single draw call. This leverages the GPU for per-pixel distance calculations, enabling smooth anti-aliasing, corner radius, and efficient border rendering with no CPU tessellation.

### Benefits

- **5x geometry reduction**: Single quad per rectangle (4 vertices vs 20)
- **Hardware anti-aliasing**: Smooth edges via smoothstep on SDF (no MSAA needed)
- **Corner radius support**: Essential for modern UI, free with SDF
- **Configurable border positioning**: Inside/Outside/Center (CSS-like)
- **Better batching**: All rectangles batch together regardless of style
- **Reduced CPU workload**: No tessellation overhead
- **Higher quality**: Perfect pixel-level precision

### Scope

- **Applies to**: Axis-aligned rectangles in the Primitives API
- **Does not affect**: Vector graphics tessellation, circle rendering, text rendering
- **Target**: OpenGL 3.3+ (current project baseline)
- **Platform**: Desktop-first (per project principles)

### Expected Performance

- **Geometry**: 5x reduction (4 vertices vs 20 per bordered rect)
- **Frame time**: <1ms for 1000 bordered rectangles (vs ~4ms current)
- **GPU cost**: ~15-20 ALU ops per pixel (negligible on modern GPUs)
- **Upload bandwidth**: 2.5x reduction despite larger vertices (fewer total vertices)

---

## Background: Signed Distance Fields

### What is an SDF?

A Signed Distance Field function returns the shortest distance from a point to a shape's boundary:

- **Negative values**: Inside the shape (e.g., -10.0 = 10 pixels inside)
- **Zero**: Exactly on the shape's boundary
- **Positive values**: Outside the shape (e.g., +5.0 = 5 pixels outside)

### Why SDFs for UI Rendering?

SDFs provide several advantages for UI primitives:

1. **Anti-aliasing**: Distance gives us perfect gradient for smooth edges
2. **Border detection**: Distance tells us if we're in fill or border region
3. **Corner radius**: Rounded corners fall naturally out of the SDF math
4. **Resolution independence**: Works at any zoom level
5. **GPU-friendly**: Simple math, highly parallelizable

### SDF for Rounded Rectangle

Inigo Quilez's optimized rounded box SDF (industry standard):

```glsl
// https://iquilezles.org/articles/distfunctions2d/
float sdRoundedBox(vec2 p, vec2 size, float radius) {
    // Clamp radius to prevent invalid shapes
    radius = min(radius, min(size.x, size.y));

    // Exploit symmetry - work in positive quadrant only
    vec2 q = abs(p) - size + radius;

    // Distance calculation
    // - Outside corners: length(q) when both components > 0
    // - On edges: max(q.x, q.y) when one component < 0
    // - Inside: max(q.x, q.y) when both components < 0
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}
```

**How it works**:
1. `abs(p)` exploits 4-way symmetry of rectangles
2. `- size + radius` shifts coordinate system to corner circle centers
3. `length(max(q, 0.0))` handles outside-corner case (Pythagorean distance)
4. `min(max(q.x, q.y), 0.0)` handles edge and interior cases
5. `- radius` accounts for circle radius

### Anti-Aliasing with smoothstep

Convert SDF distance to alpha for smooth edges:

```glsl
float pixelSize = length(vec2(dFdx(dist), dFdy(dist)));
float alpha = 1.0 - smoothstep(-pixelSize, pixelSize, dist);
```

**Why this works**:
- `dFdx(dist), dFdy(dist)` give rate of change across pixels (screen-space derivatives)
- `pixelSize` is the distance change per pixel
- `smoothstep()` creates smooth transition exactly one pixel wide
- Result: Perfect anti-aliasing at any resolution or zoom level

---

## Current Implementation Analysis

### Existing Vertex Format

**File**: `libs/renderer/primitives/batch_renderer.h:20-24`

```cpp
struct PrimitiveVertex {
    Foundation::Vec2 position;  // 8 bytes - Screen-space position
    Foundation::Vec2 texCoord;  // 8 bytes - Currently unused for solid primitives
    Foundation::Vec4 color;     // 16 bytes - RGBA color
};
// Total: 32 bytes per vertex
```

**Assessment**: The `texCoord` field is currently unused for solid-color primitives (only used for text rendering). We can repurpose this for rect-local coordinates needed by the SDF shader, or add new fields.

### Existing Shaders

**Vertex Shader** (`batch_renderer.cpp:12-31`):

```glsl
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

**Fragment Shader** (`batch_renderer.cpp:32-44`):

```glsl
#version 330 core
in vec4 v_color;
out vec4 FragColor;

void main() {
    FragColor = v_color;  // Simple pass-through, no logic
}
```

**Assessment**: Current shaders are trivial pass-through. Fragment shader needs SDF calculation logic added.

### Current Border Rendering

**File**: `libs/renderer/primitives/primitives.cpp:149-169`

```cpp
void DrawRect(const RectArgs& args) {
    // Draw fill
    g_batchRenderer->AddQuad(args.bounds, args.style.fill);

    // Draw border as 4 separate lines (PROBLEM!)
    if (args.style.border.has_value()) {
        const auto& border = args.style.border.value();

        // Top line
        DrawLine({.start = args.bounds.TopLeft(),
                  .end = args.bounds.TopRight(),
                  .style = {border.color, border.width}});

        // Right line
        DrawLine({.start = args.bounds.TopRight(),
                  .end = args.bounds.BottomRight(),
                  .style = {border.color, border.width}});

        // Bottom line
        DrawLine({.start = args.bounds.BottomRight(),
                  .end = args.bounds.BottomLeft(),
                  .style = {border.color, border.width}});

        // Left line
        DrawLine({.start = args.bounds.BottomLeft(),
                  .end = args.bounds.TopLeft(),
                  .style = {border.color, border.width}});
    }
}
```

**Problems**:
1. **4 separate draw calls**: Each `DrawLine()` adds a quad to the batch
2. **Corner overdraw**: Corners drawn 4 times (same pixels, different quads)
3. **No corner radius**: `BorderStyle.cornerRadius` field exists but marked `TODO`
4. **8 extra vertices**: Each line is 2 triangles (4 vertices, 6 indices)
5. **CPU overhead**: Line generation logic runs per rectangle

**Current behavior**:
- Rectangle with border = 5 quads (20 vertices, 30 indices)
- 1000 bordered rects = 20,000 vertices to upload each frame

---

## Proposed Architecture

### Modified Vertex Format

```cpp
struct PrimitiveVertex {
    // Screen-space position (unchanged)
    Foundation::Vec2 position;      // 8 bytes

    // NEW: Rect-local coordinates for SDF calculation
    Foundation::Vec2 rectLocalPos;  // 8 bytes

    // Fill color (unchanged)
    Foundation::Vec4 color;         // 16 bytes

    // NEW: Border data (color + width)
    Foundation::Vec4 borderData;    // 16 bytes

    // NEW: Shape parameters (size + corner radius + border position)
    Foundation::Vec4 shapeParams;   // 16 bytes
};
// Total: 64 bytes per vertex (2x current, but 5x fewer vertices!)
```

**Field Details**:

- `position`: Screen-space position (unchanged from current)
- `rectLocalPos`: Position relative to rect center, in rect local space
  - Range: `[-halfWidth, halfWidth]` x `[-halfHeight, halfHeight]`
  - Used for SDF calculation in fragment shader
- `color`: Fill color RGBA (unchanged from current)
- `borderData.rgb`: Border color RGB
- `borderData.a`: Border width in pixels
- `shapeParams.x`: Rectangle half-width (width / 2)
- `shapeParams.y`: Rectangle half-height (height / 2)
- `shapeParams.z`: Corner radius in pixels
- `shapeParams.w`: Border position enum (0=Inside, 1=Center, 2=Outside)

**Memory Impact**:
- Current: 32 bytes/vertex × 20 vertices = 640 bytes per bordered rect
- Proposed: 64 bytes/vertex × 4 vertices = 256 bytes per bordered rect
- **Result**: 2.5x reduction in memory usage despite larger vertex size

### Updated Shaders

#### Vertex Shader

```glsl
#version 330 core

// Input attributes
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_rectLocalPos;
layout(location = 2) in vec4 a_color;
layout(location = 3) in vec4 a_borderData;
layout(location = 4) in vec4 a_shapeParams;

// Uniforms (unchanged)
uniform mat4 u_projection;
uniform mat4 u_transform;

// Outputs to fragment shader
out vec2 v_rectLocalPos;
out vec4 v_color;
out vec4 v_borderData;
out vec4 v_shapeParams;

void main() {
    // Pass data through to fragment shader
    v_rectLocalPos = a_rectLocalPos;
    v_color = a_color;
    v_borderData = a_borderData;
    v_shapeParams = a_shapeParams;

    // Transform position to clip space (unchanged)
    gl_Position = u_projection * u_transform * vec4(a_position, 0.0, 1.0);
}
```

#### Fragment Shader

```glsl
#version 330 core

// Inputs from vertex shader
in vec2 v_rectLocalPos;
in vec4 v_color;
in vec4 v_borderData;
in vec4 v_shapeParams;

// Output
out vec4 FragColor;

// SDF for rounded rectangle
// Based on Inigo Quilez's optimized implementation
// https://iquilezles.org/articles/distfunctions2d/
float sdRoundedBox(vec2 p, vec2 size, float radius) {
    // Clamp radius to half the smallest dimension to prevent invalid shapes
    radius = min(radius, min(size.x, size.y));

    // Exploit symmetry - work in positive quadrant
    vec2 q = abs(p) - size + radius;

    // Distance calculation
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main() {
    // Unpack shape parameters
    vec2 halfSize = v_shapeParams.xy;
    float cornerRadius = v_shapeParams.z;
    int borderPos = int(v_shapeParams.w + 0.5);  // Round to nearest int

    // Unpack border data
    vec3 borderColor = v_borderData.rgb;
    float borderWidth = v_borderData.a;

    // Calculate SDF distance to rect edge
    float dist = sdRoundedBox(v_rectLocalPos, halfSize, cornerRadius);

    // Anti-aliasing: smooth transition over ~1 pixel in screen space
    // Use screen-space derivatives to determine pixel size
    float pixelSize = length(vec2(dFdx(dist), dFdy(dist)));

    // Calculate alpha for shape boundary (1.0 inside, 0.0 outside)
    float shapeAlpha = 1.0 - smoothstep(-pixelSize, pixelSize, dist);

    // Early exit if completely transparent (optimization)
    if (shapeAlpha < 0.001) {
        discard;
    }

    // Determine border region boundaries based on position mode
    float borderInner, borderOuter;
    if (borderPos == 0) {
        // Inside: border is drawn inside the shape
        borderOuter = 0.0;
        borderInner = -borderWidth;
    } else if (borderPos == 1) {
        // Center: border straddles the shape edge
        borderOuter = borderWidth * 0.5;
        borderInner = -borderWidth * 0.5;
    } else {
        // Outside: border is drawn outside the shape
        borderOuter = borderWidth;
        borderInner = 0.0;
    }

    // Calculate border blend factor (1.0 in border region, 0.0 in fill)
    float borderBlend = 0.0;
    if (borderWidth > 0.001) {
        // Smooth transition at inner border edge
        float innerFade = smoothstep(borderInner - pixelSize, borderInner + pixelSize, dist);
        // Smooth transition at outer border edge
        float outerFade = 1.0 - smoothstep(borderOuter - pixelSize, borderOuter + pixelSize, dist);
        // Combine: 1.0 only in the region between inner and outer
        borderBlend = innerFade * outerFade;
    }

    // Mix fill color and border color based on blend factor
    vec3 finalColor = mix(v_color.rgb, borderColor, borderBlend);

    // Apply shape alpha and fill alpha
    FragColor = vec4(finalColor, shapeAlpha * v_color.a);
}
```

**Shader Complexity**:
- **Vertex shader**: Trivial pass-through (no performance concern)
- **Fragment shader**: ~20-25 ALU operations per pixel
  - 1× `sdRoundedBox()` call: ~10 ALU ops
  - 2× `smoothstep()` calls: ~8 ALU ops
  - 1× `mix()` call: ~3 ALU ops
  - Arithmetic: ~3-4 ALU ops
- **Performance**: Negligible on modern GPUs (TFLOPs of compute)

### BatchRenderer Changes

#### Modified AddQuad Signature

```cpp
// Before (current)
void AddQuad(
    const Foundation::Rect& bounds,
    const Foundation::Color& color
);

// After (proposed)
void AddQuad(
    const Foundation::Rect& bounds,
    const Foundation::Color& fillColor,
    const std::optional<Foundation::BorderStyle>& border = std::nullopt,
    float cornerRadius = 0.0f
);
```

#### Implementation

```cpp
void BatchRenderer::AddQuad(
    const Foundation::Rect& bounds,
    const Foundation::Color& fillColor,
    const std::optional<Foundation::BorderStyle>& border,
    float cornerRadius
) {
    uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());

    // Calculate rect center and half-dimensions for SDF
    Vec2 center = bounds.Center();
    float halfW = bounds.width * 0.5f;
    float halfH = bounds.height * 0.5f;

    // Pack border data (color + width)
    Vec4 borderData = border.has_value()
        ? Vec4(border->color.r, border->color.g, border->color.b, border->width)
        : Vec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Pack shape parameters (half-size + corner radius + border position)
    float borderPosEnum = 1.0f;  // Default to Center
    if (border.has_value()) {
        switch (border->position) {
            case BorderPosition::Inside: borderPosEnum = 0.0f; break;
            case BorderPosition::Center: borderPosEnum = 1.0f; break;
            case BorderPosition::Outside: borderPosEnum = 2.0f; break;
        }
    }
    Vec4 shapeParams(halfW, halfH, cornerRadius, borderPosEnum);

    // Four vertices with rect-local coordinates
    // Top-left corner
    m_vertices.push_back({
        .position = bounds.TopLeft(),
        .rectLocalPos = Vec2(-halfW, -halfH),
        .color = fillColor.ToVec4(),
        .borderData = borderData,
        .shapeParams = shapeParams
    });

    // Top-right corner
    m_vertices.push_back({
        .position = bounds.TopRight(),
        .rectLocalPos = Vec2(halfW, -halfH),
        .color = fillColor.ToVec4(),
        .borderData = borderData,
        .shapeParams = shapeParams
    });

    // Bottom-right corner
    m_vertices.push_back({
        .position = bounds.BottomRight(),
        .rectLocalPos = Vec2(halfW, halfH),
        .color = fillColor.ToVec4(),
        .borderData = borderData,
        .shapeParams = shapeParams
    });

    // Bottom-left corner
    m_vertices.push_back({
        .position = bounds.BottomLeft(),
        .rectLocalPos = Vec2(-halfW, halfH),
        .color = fillColor.ToVec4(),
        .borderData = borderData,
        .shapeParams = shapeParams
    });

    // Indices for two triangles (unchanged)
    m_indices.push_back(baseIndex + 0);
    m_indices.push_back(baseIndex + 1);
    m_indices.push_back(baseIndex + 2);
    m_indices.push_back(baseIndex + 0);
    m_indices.push_back(baseIndex + 2);
    m_indices.push_back(baseIndex + 3);
}
```

### Primitives API Changes

#### Updated DrawRect

**File**: `libs/renderer/primitives/primitives.cpp`

```cpp
void DrawRect(const RectArgs& args) {
    if (g_batchRenderer == nullptr) {
        return;
    }

    // Extract corner radius from border style
    float cornerRadius = 0.0f;
    if (args.style.border.has_value()) {
        cornerRadius = args.style.border->cornerRadius;
    }

    // Single call - no separate border rendering!
    g_batchRenderer->AddQuad(
        args.bounds,
        args.style.fill,
        args.style.border,
        cornerRadius
    );

    // No more DrawLine() calls!
}
```

**Key change**: Removed 4× `DrawLine()` calls. Border now rendered in fragment shader.

#### Updated BorderStyle

**File**: `libs/foundation/graphics/primitive_styles.h`

```cpp
// Border positioning mode (CSS-like)
enum class BorderPosition {
    Inside,   // Border drawn inside rect bounds (content area reduced)
    Center,   // Border centered on rect edge (default, matches CSS)
    Outside   // Border drawn outside rect bounds (total size increased)
};

struct BorderStyle {
    Color color = Color::White();
    float width = 1.0f;
    float cornerRadius = 0.0f;
    BorderPosition position = BorderPosition::Center;
};
```

**Key change**: Added `BorderPosition position` field with default of `Center`.

---

## Performance Analysis

### Before (Current Implementation)

**Single rectangle with border**:
- Fill: 1 quad (4 vertices, 6 indices)
- Border: 4 lines × 1 quad each (16 vertices, 24 indices)
- **Total**: 20 vertices, 30 indices
- **Memory**: 32 bytes/vertex × 20 = 640 bytes

**1000 rectangles with borders**:
- 20,000 vertices
- 30,000 indices
- **Upload**: ~640 KB vertex data
- **CPU overhead**: Line tessellation × 4000 calls
- **Draw calls**: 1 (good - batched)

### After (SDF Implementation)

**Single rectangle with border**:
- Single quad (4 vertices, 6 indices)
- Border rendered in fragment shader (no extra geometry)
- **Total**: 4 vertices, 6 indices
- **Memory**: 64 bytes/vertex × 4 = 256 bytes

**1000 rectangles with borders**:
- 4,000 vertices
- 6,000 indices
- **Upload**: ~256 KB vertex data
- **CPU overhead**: Minimal (no tessellation)
- **Draw calls**: 1 (same - batched)

### Comparison Table

| Metric | Current | Proposed | Improvement |
|--------|---------|----------|-------------|
| Vertices per rect | 20 | 4 | **5x reduction** |
| Bytes per rect | 640 | 256 | **2.5x reduction** |
| CPU tessellation | Yes (4 lines) | No | **Eliminated** |
| Corner radius | No | Yes | **New feature** |
| Anti-aliasing | MSAA only | Built-in | **Better quality** |
| Upload bandwidth | 640 KB | 256 KB | **2.5x reduction** |
| Fragment work | Minimal | ~20 ALU ops/pixel | Negligible |

### Fragment Shader Cost Analysis

**Per-pixel operations** (200×100 px rectangle = 20,000 pixels):
- 1× `sdRoundedBox()`: ~10 ALU ops
- 2× `smoothstep()`: ~8 ALU ops
- 1× `mix()`: ~3 ALU ops
- Arithmetic: ~4 ALU ops
- **Total**: ~25 ALU ops per pixel

**20,000 pixels × 25 ALU ops = 500,000 operations**

**Modern GPU throughput**: 1-10 TFLOPS (trillions of ops/sec)

**Time**: 500,000 ops ÷ 1,000,000,000,000 ops/sec = 0.0005 ms = **0.5 microseconds**

**Conclusion**: Fragment shader overhead is **negligible** compared to CPU tessellation and upload savings.

### Real-World Performance Estimate

**For 1000 bordered rectangles at 60 FPS**:

**Current implementation**:
- CPU: ~1.0 ms (tessellation + batching)
- Upload: ~2-3 ms (glBufferData 640 KB)
- GPU: ~0.1 ms (rasterization)
- **Total**: ~3-4 ms

**SDF implementation**:
- CPU: ~0.2 ms (batching only, no tessellation)
- Upload: ~0.8-1.0 ms (glBufferData 256 KB)
- GPU: ~0.2 ms (rasterization + fragment shader)
- **Total**: ~1.2-1.4 ms

**Result**: **~3x faster** overall, with better visual quality

---

## Border Positioning Behavior

### Inside (BorderPosition::Inside)

Border drawn inside the shape bounds, reducing content area.

```
Original bounds: ┌─────────────────┐
                 │                 │
With border:     ├─────────────────┤
                 │░░ Border Inside │ ← Border pixels
                 │█ Content area   │ ← Content reduced by border width
                 └─────────────────┘

SDF: borderOuter = 0.0
     borderInner = -borderWidth
```

**Use case**: Buttons, panels where outer size must stay fixed

### Center (BorderPosition::Center) - Default

Border straddles the shape edge (half inside, half outside).

```
Original bounds: ┌─────────────────┐
                 │                 │
With border:    ▓├─────────────────┤▓
                ▓│░ Border Center  │▓ ← Half border outside
                ▓│                 │▓
                 └─────────────────┘

SDF: borderOuter = borderWidth * 0.5
     borderInner = -borderWidth * 0.5
```

**Use case**: CSS-default, balanced appearance

### Outside (BorderPosition::Outside)

Border drawn outside the shape bounds, preserving content area.

```
Original bounds: ┌─────────────────┐
                 │                 │
With border:    ╔═╤═════════════════╤═╗
                ║ │  Border Outside │ ║ ← Border outside bounds
                ║ │ Content area OK │ ║ ← Content unchanged
                ╚═╧═════════════════╧═╝

SDF: borderOuter = borderWidth
     borderInner = 0.0
```

**Use case**: Focus indicators, selection outlines

### Visual Comparison

```
Inside:         Center:         Outside:
┌─────────┐    ▓┌─────────┐▓    ╔═┌─────────┐═╗
│░Content │    ▓│ Content │▓    ║ │ Content │ ║
│ Reduced │    ▓│ Same    │▓    ║ │ Same    ║ ║
└─────────┘    ▓└─────────┘▓    ╚═└─────────┘═╝
```

---

## Integration Points

### Files to Modify

#### 1. BatchRenderer Header

**File**: `libs/renderer/primitives/batch_renderer.h`

**Changes**:
- Update `PrimitiveVertex` struct (add 32 bytes of new fields)
- Update `AddQuad()` signature (add border and corner radius parameters)
- Add shader source string constants (new vertex and fragment shaders)

**Estimated lines**: ~50 lines changed

#### 2. BatchRenderer Implementation

**File**: `libs/renderer/primitives/batch_renderer.cpp`

**Changes**:
- Replace vertex shader source (~30 lines)
- Replace fragment shader source (~60 lines)
- Update vertex attribute setup in `Init()` (~15 lines)
- Update `AddQuad()` implementation (~40 lines)

**Estimated lines**: ~145 lines changed

#### 3. Primitives API

**File**: `libs/renderer/primitives/primitives.cpp`

**Changes**:
- Update `DrawRect()` to remove `DrawLine()` calls (~10 lines removed)
- Pass corner radius to `AddQuad()` (~3 lines added)

**Estimated lines**: ~13 lines changed (net: -7 lines)

#### 4. Primitive Styles

**File**: `libs/foundation/graphics/primitive_styles.h`

**Changes**:
- Add `BorderPosition` enum (~6 lines)
- Add `position` field to `BorderStyle` (~1 line)
- Update documentation (~5 lines)

**Estimated lines**: ~12 lines changed

**Total estimated changes**: ~220 lines across 4 files

### Backward Compatibility

#### Breaking Changes

**Vertex format change**:
- Old vertex format: 32 bytes (position, texCoord, color)
- New vertex format: 64 bytes (5 fields)
- **Impact**: Requires shader recompilation, VAO reconfiguration

**API signature change**:
- `BatchRenderer::AddQuad()` gains two parameters (both have defaults)
- **Impact**: Minimal - default parameters maintain compatibility

#### Migration Strategy

**Option A: Feature Flag** (Recommended)
```cpp
// batch_renderer.h
#define USE_SDF_RENDERING 1  // Toggle between implementations

#if USE_SDF_RENDERING
struct PrimitiveVertex {
    // New 64-byte format
};
#else
struct PrimitiveVertex {
    // Old 32-byte format
};
#endif
```

**Option B: Direct Migration**
- Implement new format
- Test thoroughly
- Merge as breaking change
- Update all dependent code simultaneously

**Recommendation**: Use Option B (direct migration). The project is early enough that clean breaks are acceptable, and maintaining two paths adds complexity.

### Testing Integration

New tests integrate with existing test infrastructure:

**Unit tests**: Add to existing `libs/renderer/primitives/batch_renderer.test.cpp`

**Integration tests**: Create new `libs/renderer/primitives/sdf_rendering_integration.test.cpp`

**Visual tests**: Add new scene `apps/ui-sandbox/scenes/sdf_test_scene.cpp`

---

## Testing Strategy

### Unit Tests

#### Vertex Data Correctness

**File**: `libs/renderer/primitives/batch_renderer.test.cpp`

```cpp
TEST(BatchRenderer, SDFVertexDataCorrect) {
    BatchRenderer renderer;
    renderer.Init();

    // Define test rectangle
    Rect bounds(100.0f, 100.0f, 200.0f, 100.0f);
    Color fill = Color::Red();
    BorderStyle border{
        .color = Color::Blue(),
        .width = 5.0f,
        .cornerRadius = 10.0f,
        .position = BorderPosition::Center
    };

    // Add quad with SDF parameters
    renderer.AddQuad(bounds, fill, border, border.cornerRadius);

    // Verify vertex data
    const auto& vertices = renderer.GetVertices();
    ASSERT_EQ(vertices.size(), 4);

    // Check rect-local coordinates (relative to center)
    EXPECT_FLOAT_EQ(vertices[0].rectLocalPos.x, -100.0f);  // Half-width
    EXPECT_FLOAT_EQ(vertices[0].rectLocalPos.y, -50.0f);   // Half-height
    EXPECT_FLOAT_EQ(vertices[1].rectLocalPos.x, 100.0f);
    EXPECT_FLOAT_EQ(vertices[1].rectLocalPos.y, -50.0f);

    // Check border data
    EXPECT_COLOR_EQ(vertices[0].borderData.rgb(), Color::Blue());
    EXPECT_FLOAT_EQ(vertices[0].borderData.a, 5.0f);  // Border width

    // Check shape params
    EXPECT_FLOAT_EQ(vertices[0].shapeParams.x, 100.0f);  // Half-width
    EXPECT_FLOAT_EQ(vertices[0].shapeParams.y, 50.0f);   // Half-height
    EXPECT_FLOAT_EQ(vertices[0].shapeParams.z, 10.0f);   // Corner radius
    EXPECT_FLOAT_EQ(vertices[0].shapeParams.w, 1.0f);    // Center = 1.0
}
```

#### SDF Function Correctness

```cpp
// Helper: CPU implementation of sdRoundedBox for testing
float SDFRoundedBox(Vec2 p, Vec2 size, float radius) {
    radius = std::min(radius, std::min(size.x, size.y));
    Vec2 q = Vec2(std::abs(p.x), std::abs(p.y)) - size + Vec2(radius, radius);
    float maxQ = std::max(q.x, q.y);
    float len = std::sqrt(std::max(q.x, 0.0f) * std::max(q.x, 0.0f) +
                          std::max(q.y, 0.0f) * std::max(q.y, 0.0f));
    return len + std::min(maxQ, 0.0f) - radius;
}

TEST(SDFMath, RoundedBoxDistanceCorrect) {
    Vec2 size(100.0f, 50.0f);
    float radius = 10.0f;

    // Center point should be negative (inside)
    float distCenter = SDFRoundedBox(Vec2(0, 0), size, radius);
    EXPECT_LT(distCenter, 0.0f);
    EXPECT_NEAR(distCenter, -50.0f, 1.0f);  // Roughly -halfHeight

    // Point on right edge (no corner) should be ~0
    float distEdge = SDFRoundedBox(Vec2(100, 0), size, radius);
    EXPECT_NEAR(distEdge, 0.0f, 0.1f);

    // Point far outside should be positive
    float distOutside = SDFRoundedBox(Vec2(150, 0), size, radius);
    EXPECT_NEAR(distOutside, 50.0f, 1.0f);

    // Corner point should account for radius
    float distCorner = SDFRoundedBox(Vec2(100, 50), size, radius);
    EXPECT_NEAR(distCorner, 0.0f, 1.0f);  // On rounded corner edge
}
```

#### Border Positioning Logic

```cpp
TEST(SDFRendering, BorderPositionInsideCorrect) {
    // Test Inside mode: border doesn't extend past bounds
    // borderOuter = 0.0, borderInner = -borderWidth

    float borderWidth = 5.0f;
    float borderOuter = 0.0f;
    float borderInner = -borderWidth;

    // Point at dist = -2.5 (inside fill, outside border)
    float dist = -2.5f;
    EXPECT_LT(dist, borderOuter);  // Inside shape
    EXPECT_GT(dist, borderInner);  // Outside border
    // Expected: fill color
}

TEST(SDFRendering, BorderPositionCenterCorrect) {
    // Test Center mode: border straddles edge
    // borderOuter = borderWidth * 0.5, borderInner = -borderWidth * 0.5

    float borderWidth = 5.0f;
    float borderOuter = borderWidth * 0.5f;  // 2.5
    float borderInner = -borderWidth * 0.5f; // -2.5

    // Point at dist = 0.0 (exactly on edge)
    float dist = 0.0f;
    EXPECT_LT(dist, borderOuter);  // Inside outer edge
    EXPECT_GT(dist, borderInner);  // Outside inner edge
    // Expected: border color
}

TEST(SDFRendering, BorderPositionOutsideCorrect) {
    // Test Outside mode: border extends beyond bounds
    // borderOuter = borderWidth, borderInner = 0.0

    float borderWidth = 5.0f;
    float borderOuter = borderWidth;  // 5.0
    float borderInner = 0.0f;

    // Point at dist = 2.5 (outside shape, inside border region)
    float dist = 2.5f;
    EXPECT_LT(dist, borderOuter);  // Inside border region
    EXPECT_GT(dist, borderInner);  // Outside fill region
    // Expected: border color
}
```

### Integration Tests

#### Rendering Correctness

**File**: `libs/renderer/primitives/sdf_rendering_integration.test.cpp`

```cpp
class SDFRenderingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize OpenGL context (using GLFW offscreen)
        glfwInit();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        m_window = glfwCreateWindow(800, 600, "Test", nullptr, nullptr);
        glfwMakeContextCurrent(m_window);

        // Initialize renderer
        Renderer::Primitives::Init(/* renderer */);
    }

    void TearDown() override {
        Renderer::Primitives::Shutdown();
        glfwDestroyWindow(m_window);
        glfwTerminate();
    }

    GLFWwindow* m_window;
};

TEST_F(SDFRenderingTest, RectWithBorderRendersCorrectly) {
    Renderer::Primitives::BeginFrame();

    // Draw test rectangle with border
    Renderer::Primitives::DrawRect({
        .bounds = {100, 100, 200, 100},
        .style = {
            .fill = Color(1.0f, 0.0f, 0.0f, 1.0f),  // Red fill
            .border = BorderStyle{
                .color = Color(0.0f, 0.0f, 1.0f, 1.0f),  // Blue border
                .width = 5.0f,
                .cornerRadius = 10.0f,
                .position = BorderPosition::Center
            }
        }
    });

    Renderer::Primitives::EndFrame();

    // Read framebuffer pixels
    std::vector<Color> pixels = ReadFramebuffer(800, 600);

    // Check fill color inside (center of rect)
    Color centerPixel = GetPixel(pixels, 200, 150);
    EXPECT_COLOR_NEAR(centerPixel, Color::Red(), 0.1f);

    // Check border color at edge
    Color edgePixel = GetPixel(pixels, 100, 150);
    EXPECT_COLOR_NEAR(edgePixel, Color::Blue(), 0.1f);

    // Check transparency outside rect
    Color outsidePixel = GetPixel(pixels, 50, 150);
    EXPECT_FLOAT_EQ(outsidePixel.a, 0.0f);
}

TEST_F(SDFRenderingTest, CornerRadiusAntialiased) {
    // Draw rect with large corner radius
    Renderer::Primitives::BeginFrame();
    Renderer::Primitives::DrawRect({
        .bounds = {100, 100, 200, 200},
        .style = {
            .fill = Color::Red(),
            .border = BorderStyle{
                .color = Color::Blue(),
                .width = 3.0f,
                .cornerRadius = 50.0f  // Large radius
            }
        }
    });
    Renderer::Primitives::EndFrame();

    // Read corner pixel region
    std::vector<Color> pixels = ReadFramebuffer(800, 600);

    // Check that corner has smooth gradient (anti-aliasing)
    // Sample pixels along the corner curve
    float alphaAtCorner = GetPixel(pixels, 100, 100).a;
    float alphaInside = GetPixel(pixels, 120, 120).a;
    float alphaOutside = GetPixel(pixels, 90, 90).a;

    // Expect gradient: outside < corner < inside
    EXPECT_LT(alphaOutside, alphaAtCorner);
    EXPECT_LT(alphaAtCorner, alphaInside);

    // Check that it's actually a smooth transition (not hard edge)
    EXPECT_GT(alphaAtCorner, 0.0f);
    EXPECT_LT(alphaAtCorner, 1.0f);
}
```

### Visual Tests

#### Test Scene: SDF Demo

**File**: `apps/ui-sandbox/scenes/sdf_test_scene.cpp`

```cpp
class SDFTestScene : public IScene {
public:
    void Init() override {
        // Create test rectangles demonstrating all features

        // Row 1: Corner radius variations
        for (int i = 0; i < 5; ++i) {
            float radius = i * 10.0f;  // 0, 10, 20, 30, 40
            m_rects.push_back({
                .bounds = {50.0f + i * 150.0f, 50.0f, 100.0f, 100.0f},
                .style = {
                    .fill = Color(0.2f, 0.6f, 0.9f),
                    .border = BorderStyle{
                        .color = Color::Black(),
                        .width = 3.0f,
                        .cornerRadius = radius
                    }
                }
            });
        }

        // Row 2: Border positions (Inside/Center/Outside)
        BorderPosition positions[] = {
            BorderPosition::Inside,
            BorderPosition::Center,
            BorderPosition::Outside
        };
        const char* labels[] = {"Inside", "Center", "Outside"};

        for (int i = 0; i < 3; ++i) {
            m_rects.push_back({
                .bounds = {100.0f + i * 200.0f, 200.0f, 150.0f, 100.0f},
                .style = {
                    .fill = Color(0.9f, 0.5f, 0.2f),
                    .border = BorderStyle{
                        .color = Color(0.1f, 0.1f, 0.1f),
                        .width = 10.0f,
                        .cornerRadius = 15.0f,
                        .position = positions[i]
                    }
                }
            });
            m_labels.push_back({labels[i], Vec2(100.0f + i * 200.0f, 180.0f)});
        }

        // Row 3: Border width variations
        for (int i = 0; i < 4; ++i) {
            float width = (i + 1) * 2.0f;  // 2, 4, 6, 8
            m_rects.push_back({
                .bounds = {80.0f + i * 170.0f, 350.0f, 120.0f, 80.0f},
                .style = {
                    .fill = Color(0.5f, 0.9f, 0.5f),
                    .border = BorderStyle{
                        .color = Color(0.2f, 0.4f, 0.2f),
                        .width = width,
                        .cornerRadius = 10.0f
                    }
                }
            });
        }

        // Row 4: Stress test (many nested rects)
        for (int i = 0; i < 10; ++i) {
            float offset = i * 10.0f;
            m_rects.push_back({
                .bounds = {50.0f + offset, 500.0f + offset,
                           200.0f - offset * 2, 100.0f - offset * 2},
                .style = {
                    .fill = Color::Transparent(),
                    .border = BorderStyle{
                        .color = Color::HSV(i * 36.0f, 0.8f, 0.9f),
                        .width = 2.0f,
                        .cornerRadius = 20.0f - i * 2.0f
                    }
                }
            });
        }
    }

    void Update(float deltaTime) override {
        // Optional: animate corner radius for visual testing
    }

    void Render() override {
        Renderer::Primitives::BeginFrame();

        // Draw all test rectangles
        for (const auto& rect : m_rects) {
            Renderer::Primitives::DrawRect(rect);
        }

        // Draw labels
        for (const auto& label : m_labels) {
            Renderer::Primitives::DrawText({
                .text = label.first,
                .position = label.second,
                .style = {.color = Color::Black(), .fontSize = 14.0f}
            });
        }

        Renderer::Primitives::EndFrame();
    }

private:
    std::vector<RectArgs> m_rects;
    std::vector<std::pair<std::string, Vec2>> m_labels;
};
```

#### Visual Regression Testing

**Workflow**:
1. Run `ui-sandbox --scene=sdf_test`
2. Capture screenshot via HTTP endpoint: `curl http://127.0.0.1:8081/api/ui/screenshot > reference.png`
3. Store reference images in `tests/visual/references/`
4. On CI, compare new screenshots against references
5. Fail build if pixel difference exceeds threshold (e.g., 1% of pixels differ by >5%)

**Tools**: ImageMagick `compare` or custom pixel diff tool

### Performance Tests

#### Benchmark Suite

**File**: `libs/renderer/primitives/sdf_rendering.bench.cpp`

```cpp
static void BM_DrawManyRects_NoB order(benchmark::State& state) {
    // Setup
    SetupOffscreenContext();
    Renderer::Primitives::Init(/* renderer */);

    int rectCount = state.range(0);

    for (auto _ : state) {
        Renderer::Primitives::BeginFrame();

        for (int i = 0; i < rectCount; ++i) {
            Renderer::Primitives::DrawRect({
                .bounds = {(float)(i % 10) * 80.0f, (float)(i / 10) * 60.0f, 70.0f, 50.0f},
                .style = {
                    .fill = Color::Random()
                }
            });
        }

        Renderer::Primitives::EndFrame();
        glFinish();  // Wait for GPU
    }

    state.SetItemsProcessed(state.iterations() * rectCount);
}

static void BM_DrawManyRects_WithBorder(benchmark::State& state) {
    SetupOffscreenContext();
    Renderer::Primitives::Init(/* renderer */);

    int rectCount = state.range(0);

    for (auto _ : state) {
        Renderer::Primitives::BeginFrame();

        for (int i = 0; i < rectCount; ++i) {
            Renderer::Primitives::DrawRect({
                .bounds = {(float)(i % 10) * 80.0f, (float)(i / 10) * 60.0f, 70.0f, 50.0f},
                .style = {
                    .fill = Color::Random(),
                    .border = BorderStyle{
                        .color = Color::Random(),
                        .width = 3.0f,
                        .cornerRadius = 5.0f
                    }
                }
            });
        }

        Renderer::Primitives::EndFrame();
        glFinish();
    }

    state.SetItemsProcessed(state.iterations() * rectCount);
}

BENCHMARK(BM_DrawManyRects_NoBorder)->Range(100, 10000);
BENCHMARK(BM_DrawManyRects_WithBorder)->Range(100, 10000);

// Expected results:
// BM_DrawManyRects_NoBorder/1000    ~0.5 ms
// BM_DrawManyRects_WithBorder/1000  ~1.0 ms (vs ~3-4 ms with old implementation)
```

#### Profiling Points

**Measurements**:
1. **Frame time**: Total time from BeginFrame() to EndFrame()
2. **Upload time**: glBufferData() duration
3. **Draw call time**: glDrawElements() duration
4. **Vertex count**: Verify 4 per rect (not 20)
5. **Draw call count**: Verify = 1 (batched)

**Tools**:
- CPU profiling: Instruments (macOS), perf (Linux)
- GPU profiling: NVIDIA Nsight, AMD Radeon GPU Profiler, RenderDoc

---

## Migration Plan

### Phase 1: Core Implementation (5 days)

#### Day 1-2: Shader Development
- [ ] Update `PrimitiveVertex` struct with new fields
- [ ] Write new vertex shader (pass-through with new attributes)
- [ ] Write new fragment shader with `sdRoundedBox()` SDF
- [ ] Implement border positioning logic in shader
- [ ] Add anti-aliasing with smoothstep
- [ ] Test shader with ShaderToy or standalone app

#### Day 3: BatchRenderer Integration
- [ ] Update `CompileShader()` with new shader sources
- [ ] Modify `Init()` to set up new vertex attributes (5× glVertexAttribPointer)
- [ ] Update `AddQuad()` signature and implementation
- [ ] Calculate rect-local coordinates
- [ ] Pack border and shape parameters into vertices
- [ ] Test with simple rectangles in ui-sandbox

#### Day 4: Primitives API Update
- [ ] Remove `DrawLine()` calls from `DrawRect()`
- [ ] Pass corner radius to `AddQuad()`
- [ ] Add `BorderPosition` enum to primitive_styles.h
- [ ] Add `position` field to `BorderStyle`
- [ ] Update API documentation

#### Day 5: Initial Testing
- [ ] Create SDF test scene (sdf_test_scene.cpp)
- [ ] Visual verification of borders and corner radius
- [ ] Test all three border positions
- [ ] Fix any rendering artifacts

### Phase 2: Testing & Validation (5 days)

#### Day 6-7: Unit Tests
- [ ] Write vertex data correctness tests
- [ ] Write SDF function accuracy tests (CPU reference implementation)
- [ ] Write border positioning logic tests
- [ ] All tests passing (target: 100% coverage of SDF code)

#### Day 8: Integration Tests
- [ ] Create rendering correctness tests (framebuffer pixel checking)
- [ ] Test corner anti-aliasing quality
- [ ] Test border positions render correctly
- [ ] Test edge cases (zero radius, zero border, tiny rects)

#### Day 9: Performance Benchmarking
- [ ] Write benchmark suite (1000+ rects with/without borders)
- [ ] Measure frame time vs old implementation
- [ ] Verify geometry reduction (4 vertices vs 20)
- [ ] Profile GPU with RenderDoc or Nsight
- [ ] Verify <1ms target met

#### Day 10: Visual Quality Verification
- [ ] Test on various displays (Retina, standard DPI)
- [ ] Verify anti-aliasing quality at different zoom levels
- [ ] Check corner radius accuracy (compare to design mockups)
- [ ] Test border width consistency
- [ ] Capture reference screenshots for regression testing

### Phase 3: Polish & Documentation (5 days)

#### Day 11-12: Edge Case Handling
- [ ] Test extreme corner radii (0, max, > rect size)
- [ ] Test extreme border widths (0, 1px, > rect size)
- [ ] Test tiny rectangles (< 10px)
- [ ] Test huge rectangles (> 1000px)
- [ ] Test negative sizes (should clamp or error)
- [ ] Fix any discovered issues

#### Day 13: Documentation
- [ ] Finalize this technical spec with findings
- [ ] Update primitive-rendering-api.md with SDF section
- [ ] Add code examples to README
- [ ] Document shader parameters and their effects
- [ ] Create visual guide for border positioning

#### Day 14-15: Code Review & Merge
- [ ] Internal code review
- [ ] Address review feedback
- [ ] Final testing pass (all tests, benchmarks, visual)
- [ ] Update CHANGELOG.md
- [ ] Merge to main branch
- [ ] Update status.md (mark epic complete)

**Total**: 15 days (3 weeks)

---

## Alternatives Considered

### Alternative 1: CPU Tessellation for Rounded Corners

**Approach**: Tessellate rounded corners using CPU bezier curve approximation, keep border as 4 lines.

**Implementation**:
- For each corner, generate 8-12 vertices along circular arc
- Connect with triangle strip
- Still use 4 lines for borders

**Pros**:
- No shader changes needed
- Familiar technique (used in many 2D libraries)

**Cons**:
- **More vertices**: 12-20 vertices per corner × 4 corners = 48-80 vertices per rect
- **CPU overhead**: Tessellation logic runs per rectangle per frame
- **Still have border overdraw** at corners (4× pixel writes)
- **Anti-aliasing requires more vertices** (or MSAA)
- **Not resolution-independent**: Tessellation quality fixed at generation time

**Rejected**: GPU SDF approach is more efficient and produces superior visual quality.

### Alternative 2: Texture-Based Borders (9-Slice)

**Approach**: Pre-render rounded corners to textures, use 9-slice rendering technique.

**Implementation**:
- Create texture atlas with corner variations
- Render rectangle as 9 quads (4 corners + 4 edges + 1 center)
- Sample from texture for corners

**Pros**:
- No shader math needed (just texture lookups)
- Proven technique (used in many UI frameworks)
- Simple fragment shader

**Cons**:
- **Limited corner radii**: Pre-rendered sizes only, or visible scaling artifacts
- **Texture memory overhead**: Atlas must store many radius variations
- **Still 9 quads per rect**: Not as efficient as single quad
- **Border positioning complex**: Requires different texture sets
- **Not resolution-independent**: Textures have fixed DPI

**Rejected**: Not flexible enough for dynamic UI sizing and styling.

### Alternative 3: Geometry Shader Approach

**Approach**: Use geometry shader to generate border geometry on GPU.

**Implementation**:
- Pass single point per rectangle to GPU
- Geometry shader generates quad + border geometry
- Fragment shader renders solid color

**Pros**:
- CPU passes minimal data (position + style params)
- Geometry generated on GPU

**Cons**:
- **Requires OpenGL 3.2+** (geometry shaders), not 3.3 baseline
- **Geometry shaders are slower** than fragment shaders on most GPUs
- **Still need to generate border geometry** (even on GPU)
- **More complex pipeline** (three shader stages)
- **Doesn't eliminate vertex count** (GPU generates same 20 vertices)

**Rejected**: Fragment shader approach is simpler, faster, and more compatible.

### Alternative 4: Instanced Rendering with Uniforms

**Approach**: Use instanced rendering, pass shape data as per-instance uniforms.

**Implementation**:
- Single base quad mesh (4 vertices, static)
- Per-instance uniforms: position, size, colors, border params
- Instance 1000× for 1000 rectangles

**Pros**:
- Minimal vertex data upload (just 4 vertices total)
- Per-instance data in uniform buffer

**Cons**:
- **Uniform buffer size limits**: Typically 64 KB, limits instance count
- **Breaks batching flexibility**: All instances must share shader/blend mode
- **More complex state management**: Uniform buffer updates per instance
- **Doesn't reduce total data**: Uniform data size ≈ vertex attribute size

**Rejected**: Attribute-based approach offers better flexibility for batching.

---

## Performance Targets

### Geometry Reduction

**Target**: 5x reduction in vertex count for bordered rectangles

**Measurement**:
- Current: 20 vertices per bordered rect
- Goal: 4 vertices per bordered rect
- **Success**: Verify in unit tests (`vertices.size() == 4` after AddQuad)

### Frame Time

**Target**: <1ms for 1000 bordered UI rectangles (debug build, integrated GPU)

**Measurement**:
- Benchmark suite with 1000 rectangles
- Measure from BeginFrame() to EndFrame() + glFinish()
- Average over 100 iterations

**Baseline** (old implementation): ~3-4 ms
**Goal** (new implementation): <1 ms
**Success metric**: New implementation ≤ baseline (ideally 3x faster)

### GPU Utilization

**Target**: <5% GPU time for UI rendering at 60 FPS (typical game UI ~1000 elements)

**Measurement**:
- Use GPU profiler (NVIDIA Nsight, AMD RGP, or RenderDoc)
- Measure fragment shader time as % of total frame
- Typical frame budget: 16.67 ms @ 60 FPS

**Expected**:
- Vertex shader: <0.1 ms (trivial pass-through)
- Fragment shader: <0.5 ms (SDF calculation)
- Rasterization: <0.5 ms
- **Total**: ~1 ms = 6% of frame budget
- **Success**: <10% of frame budget

### Memory Bandwidth

**Target**: <10 MB/frame for typical UI (1000 elements)

**Calculation**:
- 4 vertices × 64 bytes × 1000 rects = 256 KB vertices
- 6 indices × 4 bytes × 1000 rects = 24 KB indices
- **Total**: 280 KB per frame
- At 60 FPS: 280 KB × 60 = 16.8 MB/sec

**Success**: Well below target (10 MB/frame = 600 MB/sec, far from PCIe bandwidth limits)

### Visual Quality

**Target**: Pixel-perfect anti-aliasing, no visible aliasing at 1× zoom

**Measurement**:
- Capture screenshots of test scene
- Zoom to 400% and inspect edges
- Check for smooth gradients (no stair-stepping)

**Success**:
- Corner edges smooth at all zoom levels
- Border edges smooth at all widths
- No visible artifacts or overdraw

---

## Open Questions

### 1. Texture Coordinate Field Reuse

**Question**: Should we reuse the existing `texCoord` field for `rectLocalPos`, or add a new field?

**Option A**: Rename `texCoord` → `rectLocalPos`
- Pros: No vertex size increase (stays 32 bytes with old data)
- Cons: Breaks text rendering (uses texCoord for glyph atlas)

**Option B**: Add new `rectLocalPos` field, keep `texCoord`
- Pros: Text rendering unaffected, clearer semantics
- Cons: Vertex size increases (but we're adding other fields anyway)

**Recommendation**: **Option B**. We're already increasing vertex size to 64 bytes for border/shape data, so keeping both fields maintains clarity and compatibility.

### 2. Anti-Aliasing Tuning

**Question**: How many pixels should the smoothstep transition span?

**Current**: `fwidth(dist)` gives automatic screen-space sizing (typically 1-2 pixels)

**Potential adjustment**:
```glsl
float pixelSize = length(vec2(dFdx(dist), dFdy(dist)));
float aaScale = 1.0;  // Tuning parameter (0.5-2.0)
float alpha = 1.0 - smoothstep(-pixelSize * aaScale, pixelSize * aaScale, dist);
```

**Recommendation**: Start with `aaScale = 1.0`. If edges appear too soft/hard on testing, add tuning parameter. May vary by display DPI.

### 3. Batching with Varying Styles

**Question**: Should we break batches when corner radius or border position changes?

**Answer**: **No**. Corner radius and border position are per-vertex data (shapeParams), so different values can batch together. This is a key advantage of the attribute-based approach.

**Batching still works** because:
- Same shader
- Same blend mode
- Same texture (none)
- Only vertex attribute values differ (GPU handles automatically)

### 4. OpenGL Version Compatibility

**Question**: Do we need to support older OpenGL versions (3.0, 2.1)?

**Answer**: **No**. Project targets OpenGL 3.3+ as documented. `dFdx()`/`dFdy()` are core since OpenGL 2.0, so we're safe.

### 5. Mobile/WebGL Support

**Question**: Does this need to work on mobile (OpenGL ES) or WebGL?

**Answer**: **Not initially**. Project is desktop-first. However, the shader code is compatible:
- GLSL ES 3.0 supports all required features
- Fragment derivatives (`dFdx`) available
- May need `#version 300 es` header and precision qualifiers

**Recommendation**: Defer mobile support until desktop implementation proven. Add platform-specific shader variants if needed.

---

## Future Enhancements

### Short Term (Next 3-6 Months)

#### Gradient Fills
Add linear and radial gradient support via shader parameters.

**Implementation**:
- Add `Vec4 gradientParams` to vertex (start color, end color as attributes)
- In fragment shader, interpolate based on `rectLocalPos`
- Minimal shader complexity increase (~5-10 ALU ops)

#### Drop Shadow
Add drop shadow via dual SDF sampling.

**Implementation**:
- Sample SDF at offset position for shadow
- Blur shadow with multiple samples (3-5)
- Composite shadow behind fill
- Performance: ~2-3x fragment shader cost (still fast)

#### Inner Shadow / Glow
Add inner shadow and outer glow effects.

**Implementation**:
- Inner shadow: darken fill near border (negative offset SDF)
- Outer glow: add halo outside shape (positive offset SDF with fade)
- Compositing in shader

### Medium Term (6-12 Months)

#### SDF-Based Circle Rendering
Replace CPU tessellation with fragment shader SDF for circles.

**Benefits**:
- Eliminate circle tessellation (currently 32+ vertices)
- Perfect circles at any size
- Consistent with rectangle approach

#### SDF-Based Line Rendering
Use fragment shader for lines with rounded caps.

**Benefits**:
- Eliminate line quad generation
- Add caps: round, square, arrow
- Consistent stroke quality

#### Custom SDF Shapes
Support arbitrary shapes (hexagons, stars, arrows) via SDF.

**Implementation**:
- Add shape type enum to vertex data
- Fragment shader dispatches to appropriate SDF function
- Reuse border/corner/anti-aliasing logic

### Long Term (1+ Years)

#### SDF-Based Text Rendering
Replace bitmap fonts with multi-channel SDF fonts (MSDF).

**Benefits**:
- Crisp text at any size
- No glyph atlas management
- Consistent rendering pipeline

**Reference**: [Viktor Chlumský's MSDF](https://github.com/Chlumsky/msdfgen)

#### Advanced Effects Library
Blur, bloom, and other post-processing effects using SDF.

**GPU Compute for SDF Generation**
Generate SDF fields on GPU for complex shapes.

**Reference**: [JFA+: Jump Flooding Algorithm](http://www.comp.nus.edu.sg/~tants/jfa.html)

---

## Related Documentation

### Implementation References

**Current implementation files**:
- `/libs/renderer/primitives/batch_renderer.h` - Current vertex format and batching
- `/libs/renderer/primitives/batch_renderer.cpp` - Shader implementation
- `/libs/renderer/primitives/primitives.h` - Primitives API
- `/libs/renderer/primitives/primitives.cpp` - Current border rendering (4 lines)
- `/libs/foundation/graphics/primitive_styles.h` - Style definitions

### Architecture Documents

**Project documentation**:
- `/docs/technical/ui-framework/primitive-rendering-api.md` - Primitives API design
- `/docs/technical/ui-framework/colonysim-integration-architecture.md` - Four-layer architecture
- `/docs/technical/cpp-coding-standards.md` - Code style and patterns
- `/docs/technical/logging-system.md` - Logging conventions
- `/docs/workflows.md` - Common development tasks

### External References

**SDF theory and implementation**:
- [Inigo Quilez - 2D Distance Functions](https://iquilezles.org/articles/distfunctions2d/) - Canonical SDF reference
- [Valve - Improved Alpha-Tested Magnification](https://steamcdn-a.akamaihd.net/apps/valve/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf) - SDF text rendering (2007)
- [Evan Wallace - GLSLSandbox SDF Demos](http://glslsandbox.com/search?tags=sdf) - Interactive examples

**Modern UI rendering**:
- [Warp Terminal - High-Performance UI](https://www.warp.dev/blog/how-warp-works) - SDF-based terminal UI
- [Flutter Impeller Engine](https://github.com/flutter/engine/tree/main/impeller) - GPU-accelerated UI renderer (2024+)
- [WebGPU UI Rendering](https://tchayen.com/posts/webgpu-2-rectangle) - 120 FPS with 100k rectangles

**ShaderToy examples**:
- [Rounded Box SDF](https://www.shadertoy.com/view/4llXD7) - Interactive visualization
- [SDF Border Demo](https://www.shadertoy.com/view/XlsfDj) - Border rendering with SDF
- [UI Components SDF](https://www.shadertoy.com/view/3lBBRw) - Complex UI with SDFs

---

## Appendices

### Appendix A: Complete Shader Source Code

The complete, production-ready GLSL shaders are included in the [Proposed Architecture](#proposed-architecture) section above. No changes from those listings.

### Appendix B: Implementation Checklist

#### Files to Create
- [ ] `/docs/technical/ui-framework/sdf-rendering.md` - This document
- [ ] `/tests/renderer/primitives/sdf_rendering.test.cpp` - Unit tests
- [ ] `/tests/renderer/primitives/sdf_rendering_integration.test.cpp` - Integration tests
- [ ] `/tests/renderer/primitives/sdf_rendering.bench.cpp` - Performance benchmarks
- [ ] `/apps/ui-sandbox/scenes/sdf_test_scene.{h,cpp}` - Visual test scene

#### Files to Modify
- [ ] `/libs/renderer/primitives/batch_renderer.h`
  - [ ] Update `PrimitiveVertex` struct (add 32 bytes)
  - [ ] Update `AddQuad()` signature (add 2 parameters with defaults)

- [ ] `/libs/renderer/primitives/batch_renderer.cpp`
  - [ ] Replace vertex shader source (~30 lines)
  - [ ] Replace fragment shader source (~60 lines)
  - [ ] Update `Init()` vertex attribute setup (~15 lines)
  - [ ] Update `AddQuad()` implementation (~40 lines)

- [ ] `/libs/renderer/primitives/primitives.cpp`
  - [ ] Remove `DrawLine()` calls from `DrawRect()` (~10 lines removed)
  - [ ] Pass corner radius to `AddQuad()` (~3 lines added)

- [ ] `/libs/foundation/graphics/primitive_styles.h`
  - [ ] Add `BorderPosition` enum (~6 lines)
  - [ ] Add `position` field to `BorderStyle` (~1 line)

#### Documentation to Update
- [ ] `/docs/technical/ui-framework/primitive-rendering-api.md` - Add SDF rendering section
- [ ] `/docs/technical/INDEX.md` - Add link to sdf-rendering.md
- [ ] `/docs/status.md` - Track implementation progress, mark complete
- [ ] `/README.md` - Update rendering capabilities section (if applicable)

### Appendix C: Performance Measurement Template

**Benchmark results template** (to be filled after implementation):

```
Platform: [MacBook Pro M3 / Dell XPS 15 / etc.]
GPU: [Integrated / Discrete: model]
Resolution: [1920x1080 / 3840x2160]
Build: [Debug / Release]

Frame Time (ms) - 1000 bordered rectangles:
  Old Implementation: [X.XX] ms
  New Implementation: [X.XX] ms
  Improvement: [X.X]x faster

Vertex Count:
  Old: [20,000] vertices
  New: [4,000] vertices
  Reduction: [5]x

Upload Bandwidth:
  Old: [640] KB
  New: [256] KB
  Reduction: [2.5]x

GPU Time:
  Vertex Shader: [0.XX] ms
  Fragment Shader: [0.XX] ms
  Total GPU: [X.XX] ms

Visual Quality:
  Anti-aliasing: [Excellent / Good / Needs tuning]
  Corner accuracy: [Perfect / Off by X pixels]
  Border consistency: [Perfect / Has artifacts]

Tests:
  Unit tests: [XX/XX] passing
  Integration tests: [XX/XX] passing
  Benchmarks: [XX/XX] meeting targets
```

---

## Revision History

| Date | Version | Changes | Author |
|------|---------|---------|--------|
| 2025-11-18 | 1.0 | Initial specification | Claude Code |

---

**Document Status**: ✅ Design Complete - Ready for Implementation

**Next Steps**: Begin Phase 1 implementation (shader development)