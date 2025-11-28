# Clipping and Scrolling

Created: 2025-11-27
Status: Design Complete (Not Yet Implemented)

## Overview

This document describes the clipping and scrolling system for the UI framework. The design follows the **Flutter/Unity decoupled primitives pattern** where clipping and content offset (scrolling) are independent concepts.

## Requirements

- Clip layer contents (and all children) to arbitrary shapes
- Support both **ClipMode::Inside** (standard overflow) and **ClipMode::Outside** (punch holes)
- Support nested clipping
- Maximum performance (CPU/GPU/framerate)
- API integrates with layer/shapes system

## API Design

### ClipShape Types

```cpp
using ClipShape = std::variant<
    ClipRect,           // Axis-aligned rectangle (FAST PATH)
    ClipRoundedRect,    // Rectangle with corner radius
    ClipCircle,         // Circle
    ClipPath            // Arbitrary polygon
>;

struct ClipRect {
    std::optional<Foundation::Rect> bounds = std::nullopt;  // nullopt = layer bounds
};

struct ClipRoundedRect {
    std::optional<Foundation::Rect> bounds = std::nullopt;
    float cornerRadius = 8.0F;
};

struct ClipCircle {
    Foundation::Vec2 center;
    float radius;
};

struct ClipPath {
    std::vector<Foundation::Vec2> vertices;  // Closed polygon
};

enum class ClipMode { Inside, Outside };

struct ClipSettings {
    ClipShape shape;
    ClipMode mode = ClipMode::Inside;
};
```

### Layer Properties

```cpp
struct Container {
    // ... existing fields ...

    // Clipping - visual masking (independent of scrolling)
    std::optional<ClipSettings> clip = std::nullopt;

    // Content offset - translates all children (independent of clipping)
    Foundation::Vec2 contentOffset{0, 0};
};
```

**Design rationale** (Flutter/Unity pattern - decoupled primitives):
- Clipping and scrolling are **independent concepts**
- Any container can have either, both, or neither
- Matches "everything is a layer" architecture

### Usage Examples

```cpp
// Static clipped area (no scrolling)
auto clipped = Container{
    .size = {400, 300},
    .clip = ClipSettings{.shape = ClipRect{}}
};

// Scrollable container (clips + offset)
auto scrollList = Container{
    .size = {400, 300},
    .clip = ClipSettings{.shape = ClipRect{}},
    .contentOffset = {0, -scrollY}  // Negative = scrolled down
};

// Circular avatar mask
auto avatar = Container{
    .clip = ClipSettings{
        .shape = ClipCircle{.center = {50, 50}, .radius = 50}
    }
};

// Spotlight effect (punch hole)
auto spotlight = Container{
    .clip = ClipSettings{
        .shape = ClipCircle{.center = {400, 300}, .radius = 100},
        .mode = ClipMode::Outside
    }
};

// Parallax layer (offset without clipping - unusual but valid)
auto parallax = Container{
    .contentOffset = {parallaxX, 0}  // No clip, content overflows
};
```

---

## Implementation: Hybrid Approach

### Strategy

| Clip Type | Implementation | Draw Calls Added |
|-----------|---------------|------------------|
| Axis-aligned rect | Shader clipping (per-vertex bounds) | **0** |
| Rounded rect, circle, path | Stencil buffer | **1** (mask draw) |

### Fast Path: Shader Clipping for Rectangles

**Why**: Zero GL state changes, zero additional draw calls, preserves full batching.

**Vertex extension:**
```cpp
struct PrimitiveVertex {
    // ... existing fields ...
    Foundation::Vec4 clipBounds;  // (minX, minY, maxX, maxY) or (0,0,0,0)
};
```

**Fragment shader:**
```glsl
in vec4 v_clipBounds;

void main() {
    // Fast-path rect clip (before any other work)
    if (v_clipBounds.z > v_clipBounds.x) {
        if (gl_FragCoord.x < v_clipBounds.x || gl_FragCoord.x > v_clipBounds.z ||
            gl_FragCoord.y < v_clipBounds.y || gl_FragCoord.y > v_clipBounds.w) {
            discard;
        }
    }
    // ... existing SDF code ...
}
```

**Detection:**
```cpp
void PushClip(const ClipSettings& settings) {
    if (auto* rect = std::get_if<ClipRect>(&settings.shape)) {
        if (settings.mode == ClipMode::Inside) {
            // Fast path - just update clip bounds
            m_clipBounds = rect->bounds.value_or(currentLayerBounds);
            m_useShaderClip = true;
            return;  // NO stencil needed
        }
    }
    // Fall through to stencil path for complex shapes
    UseStencilClip(settings);
}
```

### Slow Path: Stencil Buffer for Complex Shapes

**GL sequence for PushClip:**
```cpp
void UseStencilClip(const ClipSettings& settings) {
    Flush();  // Flush pending geometry

    glEnable(GL_STENCIL_TEST);
    m_clipDepth++;

    // Write mask to stencil (no color output)
    glStencilMask(0xFF);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    if (settings.mode == ClipMode::Inside) {
        glStencilFunc(GL_ALWAYS, m_clipDepth, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    } else {  // Outside - punch hole
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
    }

    DrawClipMask(settings.shape);
    Flush();

    // Re-enable color, set up stencil test for content
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x00);
    glStencilFunc(GL_EQUAL, m_clipDepth, 0xFF);

    m_clipStack.push(settings);
}
```

**PopClip:**
```cpp
void PopClip() {
    Flush();
    m_clipStack.pop();
    m_clipDepth--;

    if (m_clipDepth > 0) {
        glStencilFunc(GL_EQUAL, m_clipDepth, 0xFF);
    } else {
        glDisable(GL_STENCIL_TEST);
    }
}
```

### Nested Clipping

Uses stencil reference counting:
- Push increments depth, writes depth value to stencil
- Content renders only where stencil == current depth
- Pop decrements depth, restores parent test

```
PushClip A:  stencil = 1 inside A
PushClip B:  stencil = 2 inside B (only where already 1)
PopClip B:   test stencil == 1
PopClip A:   disable stencil
```

### Content Offset Implementation

**Zero GPU cost** - just a transform applied before rendering children:

```cpp
void RenderNode(LayerNode& node) {
    // Apply content offset as translation (before clipping)
    bool hasOffset = (node.contentOffset.x != 0 || node.contentOffset.y != 0);
    if (hasOffset) {
        PushTransform(glm::translate(Mat4(1), Vec3(-node.contentOffset, 0)));
    }

    // Apply clipping (after offset, so clip bounds stay fixed)
    if (node.clip) {
        PushClip(*node.clip);
    }

    // Render this node
    RenderShape(node);

    // Render children (offset + clipped)
    for (auto& child : node.children) {
        RenderNode(child);
    }

    // Pop in reverse order
    if (node.clip) PopClip();
    if (hasOffset) PopTransform();
}
```

**Order matters**: Offset is applied BEFORE clip so that:
- Clip bounds stay fixed (viewport doesn't move)
- Content moves within the fixed clip region

---

## Performance Analysis

### Draw Call Comparison

| Scenario | This Approach | Per-Command Scissor |
|----------|--------------|---------------------|
| 3 nested rect clips, 35 shapes | **1 draw** | 35+ draws |
| 3 nested circle clips, 35 shapes | **6 draws** (3 mask + 3 content) | 35+ draws |
| No clips, 100 shapes | **1 draw** | 1 draw |

### Why This Is Optimal

1. **Rectangular clips (most common)**: Zero overhead - shader handles it, full batching preserved
2. **Complex clips**: Stencil testing is per-pixel, so all children still batch together
3. **Per-command scissor problem**: Breaks batching entirely (ColonySim experience)

---

## Implementation Phases

### Phase 1: Infrastructure
- Ensure stencil buffer requested in window creation (`GLFW_STENCIL_BITS, 8`)
- Add stencil clear to `BeginFrame()`
- Add clip state tracking (depth, stack)

### Phase 2: Shader Fast Path
- Extend `PrimitiveVertex` with `clipBounds`
- Update VAO setup for new attribute
- Modify fragment shader with clip test
- Update `DrawRect`, `DrawTriangles` to pass clip bounds
- Test with TextInput component

### Phase 3: Stencil Slow Path
- Implement `PushClip`/`PopClip` with stencil ops
- Support `ClipCircle` and `ClipRoundedRect`
- Draw mask shapes to stencil buffer

### Phase 4: Nested Clipping
- Test stencil reference counting
- Verify pop restores correct state
- Test 3+ levels of nesting

### Phase 5: ClipMode::Outside
- Implement hole-punching with stencil decrement
- Test spotlight effect

### Phase 6: Layer Integration
- Add `clip` and `contentOffset` properties to `Container`
- Modify `RenderNode` to call `PushClip`/`PopClip` and handle transforms
- Handle text clipping (share stencil state with TextBatchRenderer)

---

## Files to Modify

| File | Changes |
|------|---------|
| `libs/renderer/primitives/batch_renderer.h` | Add `clipBounds` to `PrimitiveVertex` |
| `libs/renderer/primitives/batch_renderer.cpp` | VAO setup, pass clip bounds |
| `libs/renderer/primitives/primitives.cpp` | `PushClip`/`PopClip`, stencil state management |
| `apps/ui-sandbox/shaders/primitive.frag` | Fast-path rect clip test |
| `libs/foundation/graphics/primitive_styles.h` | `ClipShape`, `ClipMode`, `ClipSettings` types |
| `libs/ui/shapes/shapes.h` | Add `clip` and `contentOffset` properties to `Container` |
| `libs/ui/layer/layer_manager.cpp` | Integrate clip into `RenderNode` traversal |
| `libs/ui/font/text_batch_renderer.cpp` | Share stencil state for text clipping |

## Notes

- Stencil buffer provides 8-bit depth (max 255 nested clips)
- Text clipping: TextBatchRenderer must respect same stencil state
- For MSAA: stencil buffer must match sample count of color buffer
