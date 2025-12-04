# Shader-Based Rect Clipping (Phase 1 Complete)

**Date:** 2025-11-29

**Summary:**
Implemented Phase 1 of the clipping system using shader-based rect clipping. This approach tests per-fragment whether it falls within the clip bounds and discards it if outside, avoiding costly GPU state changes from glScissor.

**What Was Accomplished:**
- Created clip_types.h with ClipShape (ClipRect, ClipRoundedRect, ClipCircle, ClipPath), ClipMode, ClipSettings
- Added clipBounds (vec4) to UberVertex struct in batch_renderer.h
- Updated VAO setup with new vertex attribute for clipBounds
- Added clip test to uber.frag shader (discard if outside bounds)
- Implemented PushClip/PopClip stack in primitives.cpp
- Fixed DPI scaling issue on Retina displays (logical→physical pixel conversion)
- Created clip_scene.cpp demo with text clipping demonstration
- Modified vector_perf_scene.cpp with 'C' key toggle for clipping performance testing

**Files Created:**
- `libs/renderer/graphics/clip_types.h` - Clipping type definitions
- `apps/ui-sandbox/scenes/clip_scene.cpp` - Clipping demo scene

**Files Modified:**
- `libs/renderer/primitives/batch_renderer.h` - Added clipBounds to UberVertex
- `libs/renderer/primitives/batch_renderer.cpp` - Updated VAO, SetClipBounds/ClearClipBounds
- `libs/renderer/primitives/primitives.cpp` - PushClip/PopClip implementation, DrawText stub
- `libs/renderer/primitives/primitives.h` - PushClip/PopClip declarations
- `libs/renderer/shaders/uber.vert` - Pass-through clipBounds attribute
- `libs/renderer/shaders/uber.frag` - Fragment discard based on clip test
- `apps/ui-sandbox/scenes/vector_perf_scene.cpp` - Added 'C' key toggle

**Key Technical Details:**

**1. Shader-Based Clipping (Zero GPU State Changes)**
```glsl
// uber.frag - Per-fragment clip test
if (clipBounds.z > 0.0 && clipBounds.w > 0.0) {
    if (gl_FragCoord.x < clipBounds.x || gl_FragCoord.x > clipBounds.x + clipBounds.z ||
        gl_FragCoord.y < clipBounds.y || gl_FragCoord.y > clipBounds.y + clipBounds.w) {
        discard;
    }
}
```

**2. DPI Scaling Fix (Retina Displays)**
The shader uses gl_FragCoord which operates in physical pixels, but the API uses logical pixels. Fixed by scaling clip bounds when setting:
```cpp
void BatchRenderer::SetClipBounds(float x, float y, float w, float h) {
    float dpiScale = static_cast<float>(m_framebufferHeight) / static_cast<float>(m_windowHeight);
    m_currentClipBounds = glm::vec4(x * dpiScale, y * dpiScale, w * dpiScale, h * dpiScale);
}
```

**3. Nested Clipping via Intersection**
PushClip intersects the new clip with the current clip to support nested clips:
```cpp
// Intersect with current clip for nested clipping
if (!m_clipStack.empty()) {
    // Calculate intersection of current and new clip
    float minX = std::max(currentClip.x, newClip.x);
    // ... intersection math
}
```

**4. Primitives::DrawText Dependency Issue**
Discovered that `Primitives::DrawText` cannot be implemented in the renderer library because `FontRenderer` lives in the ui library. This would create a circular dependency (renderer→ui→renderer).

**Options identified:**
1. Move FontRenderer to renderer library (cleanest, significant refactor)
2. Add PRIVATE include path (include-only, no link dependency)
3. Create abstract IGlyphGenerator interface in renderer, implement in ui

**Current solution:** Use `UI::Text` component for text rendering. DrawText remains a documented stub.

**Lessons Learned:**
- Shader-based clipping avoids GPU state machine overhead (glScissor requires flush)
- DPI handling is critical - gl_FragCoord always uses physical pixels
- Library dependency architecture matters: renderer→ui is the correct direction, not the reverse

**Next Steps:**
- Phase 2: Add clip and contentOffset properties to Container for scrolling
- Future: Implement ClipRoundedRect, ClipCircle in shader



