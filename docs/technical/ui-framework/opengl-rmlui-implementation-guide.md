# OpenGL + RmlUI Implementation Guide

Created: 2025-10-26
Status: Production Implementation Guide
Based on: RmlUI 6.0 official documentation and GL3 reference backend

## Overview

This document provides production-ready guidance for implementing the OpenGL rendering layer with RmlUI integration. It synthesizes RmlUI's battle-tested patterns from their OpenGL 3 reference backend with our unified Primitive Rendering API architecture.

**Key Sources:**
- RmlUI official documentation (mikke89.github.io/RmlUiDoc)
- Reference implementation: `RmlUi_Renderer_GL3.cpp`
- RenderInterface specification: `<RmlUi/Core/RenderInterface.h>`

**This guide is NOT speculative.** All patterns are derived from RmlUI's production-tested implementations and official best practices.

---

## Table of Contents

1. [OpenGL Rendering Architecture](#opengl-rendering-architecture)
2. [Coordinate System Conventions](#coordinate-system-conventions)
3. [RmlUI Backend Implementation](#rmlui-backend-implementation)
4. [Primitive API OpenGL Implementation](#primitive-api-opengl-implementation)
5. [Integration Patterns](#integration-patterns)
6. [State Management](#state-management)
7. [Production Best Practices](#production-best-practices)
8. [Common Pitfalls](#common-pitfalls)
9. [Performance Considerations](#performance-considerations)
10. [Testing and Validation](#testing-and-validation)

---

## OpenGL Rendering Architecture

### Complete Render Pipeline

The rendering happens in three distinct phases, each with different characteristics:

```cpp
void Game::RenderFrame() {
    m_renderer->Clear();

    // PHASE 1: Game World Rendering
    // - Tiles (procedural or vector-based)
    // - Entities (SVG decorations, creatures)
    // - Effects (particles, water)
    // Uses: Custom renderer or vector graphics system
    RenderGameWorld();

    // PHASE 2: World-Space UI
    // - Health bars above entities
    // - Floating damage numbers
    // - Selection rectangles
    // - Name tags
    // Uses: Primitive Rendering API directly
    RenderWorldSpaceUI();

    // PHASE 3: Screen-Space UI (RmlUI)
    // - Menus, inventory, dialogs
    // - HUD elements (if complex)
    // Uses: RmlUI → Backend → Primitive API
    SetupUIRenderingState();
    m_uiContext->Render();  // RmlUI renders here
    RestoreGameRenderingState();

    m_renderer->Present();
}
```

### Rendering Layer Architecture

```
┌─────────────────────────────────────────────┐
│          RmlUI Library                      │
│  (Generates vertices, indices, draw calls)  │
└────────────────┬────────────────────────────┘
                 │ RenderInterface calls
                 ↓
┌─────────────────────────────────────────────┐
│     RmlUI Backend Adapter (our code)        │
│  - Implements Rml::RenderInterface          │
│  - Compiles geometry (VAO/VBO/IBO)          │
│  - Converts to Primitive API calls          │
│  - Manages state backup/restore             │
└────────────────┬────────────────────────────┘
                 │ Primitive API calls
                 ↓
┌─────────────────────────────────────────────┐
│     Primitive Rendering API (batched)       │
│  - DrawRect, DrawText, DrawTexture          │
│  - Accumulates draw calls                   │
│  - Batches by texture/state                 │
│  - Flushes on state change or frame end     │
└────────────────┬────────────────────────────┘
                 │ OpenGL calls (batched)
                 ↓
┌─────────────────────────────────────────────┐
│              OpenGL                         │
└─────────────────────────────────────────────┘
```

### Key Architectural Decision: Where to Batch?

**RmlUI's Reference Implementation (GL3 backend):**
- Each `RenderGeometry()` call = one OpenGL draw call
- No batching (prioritizes correctness and transform flexibility)
- Simple, battle-tested approach

**Our Architecture:**
- RmlUI backend uploads geometry to GPU (same as reference)
- But calls Primitive API instead of raw OpenGL
- Primitive API handles batching internally
- **Result:** Battle-tested geometry compilation + performance batching

**Why This Works:**
- RmlUI guarantees geometry data is immutable until `ReleaseGeometry()`
- We can safely store GPU buffer handles
- Primitive API batches draw calls by texture/state
- Rendering order preserved (flush on state changes)

---

## Coordinate System Conventions

### The Y-Axis Mismatch

**RmlUI Convention:**
- Origin: **Top-left** corner of window (like HTML/CSS)
- Y-axis: Points **downward**
- Vertex positions: Pixel units from top-left

**OpenGL Convention:**
- Origin: **Bottom-left** corner
- Y-axis: Points **upward**
- Normalized device coordinates: [-1, 1]

**Generated Textures:**
- Use **OpenGL convention** (origin at bottom-left)
- RmlUI documents this explicitly

### Projection Matrix Setup

You **must** flip the Y-axis when constructing the projection matrix:

```cpp
// Orthographic projection for UI rendering
// Note: Y-axis is FLIPPED to convert top-left to bottom-left origin
glm::mat4 CreateUIProjectionMatrix(int viewportWidth, int viewportHeight) {
    return glm::ortho(
        0.0f,                           // left
        static_cast<float>(viewportWidth),  // right
        static_cast<float>(viewportHeight), // bottom (FLIPPED)
        0.0f,                           // top (FLIPPED)
        -1.0f,                          // near
        1.0f                            // far
    );
}
```

**Critical:** The bottom/top parameters are swapped compared to normal orthographic projection.

### Scissor Region Transformation

Scissor regions **do NOT** have transforms applied (per RmlUI spec), so they also need Y-flipping:

```cpp
void SetScissorRegion(Rml::Rectanglei rect) {
    // RmlUI gives us: top-left origin
    // OpenGL expects: bottom-left origin

    int viewportHeight = m_viewportHeight;

    // Flip Y-coordinates
    int flippedY = viewportHeight - rect.p1.y;  // p1 is bottom-right in RmlUI coords
    int height = rect.p1.y - rect.p0.y;

    // Clamp to viewport (prevents WebGL validation errors)
    int x = std::max(0, rect.p0.x);
    int y = std::max(0, flippedY);
    int width = std::min(rect.p1.x - rect.p0.x, viewportWidth - x);
    height = std::min(height, viewportHeight - y);

    glScissor(x, y, width, height);
    glEnable(GL_SCISSOR_TEST);
}
```

**From GL3 reference backend:** The clamping prevents out-of-bounds scissor regions that cause validation errors in WebGL and strict OpenGL contexts.

### Vertex Transformation

Vertices flow through this transformation pipeline:

```
RmlUI Space (top-left origin, pixels)
    ↓ (translation vector applied)
Translated Vertices
    ↓ (custom transform matrix applied)
Transformed Vertices
    ↓ (projection matrix with Y-flip)
Normalized Device Coordinates
    ↓ (OpenGL viewport transform)
Window Coordinates (bottom-left origin, pixels)
```

**In vertex shader:**
```glsl
#version 330 core

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uProjection;  // Includes Y-flip
uniform mat4 uTransform;   // Optional custom transform

out vec4 vColor;
out vec2 vTexCoord;

void main() {
    // Apply translation first (already in pixel coordinates)
    // Then apply custom transform (if enabled)
    // Finally apply projection (which flips Y)
    vec4 pos = vec4(aPosition, 0.0, 1.0);
    gl_Position = uProjection * uTransform * pos;

    vColor = aColor;
    vTexCoord = aTexCoord;
}
```

### Color Space and Blending

**RmlUI Specification:**
- Colors: **sRGB color space** with **premultiplied alpha**
- Textures: Multiplied by vertex colors during sampling
- Generated textures: RGBA8 format, tightly-packed rows

**Required Blend Function:**
```cpp
glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
```

**Why premultiplied alpha?**
- Prevents color bleeding at transparent edges
- Mathematically correct for layered compositing
- Industry standard for UI rendering

**Fragment shader:**
```glsl
#version 330 core

in vec4 vColor;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform int uUseTexture;  // 0 = color only, 1 = texture

out vec4 FragColor;

void main() {
    if (uUseTexture == 1) {
        // Sample texture and multiply by vertex color
        // Texture is already premultiplied alpha
        // Vertex color is already premultiplied alpha
        FragColor = texture(uTexture, vTexCoord) * vColor;
    } else {
        // Color-only rendering (rectangles, etc.)
        FragColor = vColor;
    }
}
```

### Face Culling

**RmlUI Geometry:**
- All triangles: **Counter-clockwise winding**
- Matches OpenGL default

**OpenGL Default:**
- Front faces: Counter-clockwise (CCW)
- Back face culling: Enabled by default

**Recommendation:**
```cpp
// Either disable face culling (simplest):
glDisable(GL_CULL_FACE);

// Or ensure CCW front faces:
glEnable(GL_CULL_FACE);
glFrontFace(GL_CCW);
glCullFace(GL_BACK);
```

**Warning:** DirectX uses clockwise (CW) front faces. If you port to DirectX, you must either disable culling or flip winding direction to avoid blank screen.

---

## RmlUI Backend Implementation

### RenderInterface Implementation

Create a class that implements `Rml::RenderInterface`:

```cpp
// libs/ui/src/rmlui/RmlUIBackend.h
#pragma once
#include <RmlUi/Core/RenderInterface.h>
#include <renderer/Primitives.h>
#include <glm/glm.hpp>
#include <GL/gl3w.h>  // Or your OpenGL loader
#include <unordered_map>
#include <vector>

namespace UI::Internal {

class RmlUIBackend : public Rml::RenderInterface {
public:
    explicit RmlUIBackend(Renderer::Primitives* primitives);
    ~RmlUIBackend() override;

    // Geometry management
    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices) override;

    void RenderGeometry(
        Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation,
        Rml::TextureHandle texture) override;

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    // Texture management
    Rml::TextureHandle LoadTexture(
        Rml::Vector2i& texture_dimensions,
        const Rml::String& source) override;

    Rml::TextureHandle GenerateTexture(
        Rml::Span<const Rml::byte> source,
        Rml::Vector2i source_dimensions) override;

    void ReleaseTexture(Rml::TextureHandle texture) override;

    // Scissor regions
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    // Transform support (optional but recommended)
    void SetTransform(const Rml::Matrix4f* transform) override;

    // Frame lifecycle
    void BeginFrame();
    void EndFrame();

    // Viewport management
    void SetViewport(int width, int height);

private:
    struct CompiledGeometry {
        GLuint vao;
        GLuint vbo;
        GLuint ibo;
        int numIndices;
    };

    struct GLTexture {
        GLuint handle;
        int width;
        int height;
    };

    Renderer::Primitives* m_primitives;

    std::unordered_map<Rml::CompiledGeometryHandle, CompiledGeometry> m_geometries;
    std::unordered_map<Rml::TextureHandle, GLTexture> m_textures;

    Rml::CompiledGeometryHandle m_nextGeometryHandle = 1;
    Rml::TextureHandle m_nextTextureHandle = 1;

    int m_viewportWidth = 0;
    int m_viewportHeight = 0;

    bool m_scissorEnabled = false;

    glm::mat4 m_projectionMatrix;
    glm::mat4 m_transformMatrix;
    bool m_hasTransform = false;

    // OpenGL state backup (for state management)
    struct GLState {
        GLboolean blendEnabled;
        GLboolean cullFaceEnabled;
        GLboolean depthTestEnabled;
        GLboolean scissorTestEnabled;
        GLint blendSrc, blendDst;
        GLint viewport[4];
        GLint scissorBox[4];
        // Add more as needed
    };
    GLState m_savedState;

    void BackupGLState();
    void RestoreGLState();
    void SetupGLState();
};

} // namespace UI::Internal
```

### Geometry Compilation Strategy

**Follow the GL3 reference backend approach:**

```cpp
Rml::CompiledGeometryHandle RmlUIBackend::CompileGeometry(
    Rml::Span<const Rml::Vertex> vertices,
    Rml::Span<const int> indices) {

    // Allocate handle
    auto handle = m_nextGeometryHandle++;

    // Create OpenGL objects
    CompiledGeometry compiled;
    glGenVertexArrays(1, &compiled.vao);
    glGenBuffers(1, &compiled.vbo);
    glGenBuffers(1, &compiled.ibo);
    compiled.numIndices = static_cast<int>(indices.size());

    // Bind VAO
    glBindVertexArray(compiled.vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, compiled.vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(Rml::Vertex),
                 vertices.data(),
                 GL_STATIC_DRAW);  // Static because RmlUI guarantees immutability

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, compiled.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 indices.size() * sizeof(int),
                 indices.data(),
                 GL_STATIC_DRAW);

    // Setup vertex attributes
    // RmlUI Vertex structure:
    // struct Vertex {
    //     Rml::Vector2f position;
    //     Rml::Colourb colour;  // 4 bytes (RGBA)
    //     Rml::Vector2f tex_coord;
    // };

    // Position (vec2)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Rml::Vertex),
                          (void*)offsetof(Rml::Vertex, position));

    // Color (vec4, normalized bytes)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                          sizeof(Rml::Vertex),
                          (void*)offsetof(Rml::Vertex, colour));

    // TexCoord (vec2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Rml::Vertex),
                          (void*)offsetof(Rml::Vertex, tex_coord));

    // Unbind
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Store
    m_geometries[handle] = compiled;

    return handle;
}
```

**Why GL_STATIC_DRAW?**
- RmlUI guarantees vertex/index data is immutable until `ReleaseGeometry()`
- Geometry typically lives for entire UI element lifetime
- Static allocation optimizes GPU memory placement

### Rendering Geometry

**This is where we integrate with Primitive API:**

```cpp
void RmlUIBackend::RenderGeometry(
    Rml::CompiledGeometryHandle geometry,
    Rml::Vector2f translation,
    Rml::TextureHandle texture) {

    auto it = m_geometries.find(geometry);
    if (it == m_geometries.end()) {
        return;  // Invalid handle
    }

    const CompiledGeometry& compiled = it->second;

    // Get texture (if any)
    GLuint textureHandle = 0;
    if (texture) {
        auto texIt = m_textures.find(texture);
        if (texIt != m_textures.end()) {
            textureHandle = texIt->second.handle;
        }
    }

    // Option A: Direct OpenGL rendering (like reference backend)
    // This is simpler but doesn't batch
    RenderGeometryDirect(compiled, translation, textureHandle);

    // Option B: Use Primitive API (batched)
    // This is more complex but gets batching benefits
    // RenderGeometryViaPrimitiveAPI(compiled, translation, textureHandle);
}

void RmlUIBackend::RenderGeometryDirect(
    const CompiledGeometry& geometry,
    Rml::Vector2f translation,
    GLuint texture) {

    // Setup shader (use appropriate program)
    // Set uniforms (projection, transform, translation, texture)
    // Bind VAO
    // Draw

    glBindVertexArray(geometry.vao);

    if (texture) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
    }

    // Apply translation by modifying transform matrix
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f),
                                           glm::vec3(translation.x, translation.y, 0.0f));

    glm::mat4 finalTransform = m_projectionMatrix;
    if (m_hasTransform) {
        finalTransform = finalTransform * m_transformMatrix;
    }
    finalTransform = finalTransform * modelMatrix;

    // Set uniform (shader-dependent)
    // glUniformMatrix4fv(transformUniformLocation, 1, GL_FALSE, glm::value_ptr(finalTransform));

    // Draw
    glDrawElements(GL_TRIANGLES, geometry.numIndices, GL_UNSIGNED_INT, nullptr);

    glBindVertexArray(0);
}
```

**Decision Point: Direct vs Primitive API**

**Option A: Direct OpenGL (Recommended for initial implementation)**
- Simpler implementation
- Matches reference backend
- Easier to debug
- No batching (many draw calls)

**Option B: Primitive API Integration (Future optimization)**
- Requires converting geometry to primitive calls
- Enables batching
- More complex (need to reconstruct quads/text from triangles)
- Better performance for complex UIs

**Recommendation:** Start with direct OpenGL (Option A), optimize to Primitive API (Option B) if profiling shows RmlUI is bottleneck.

### Texture Management

```cpp
Rml::TextureHandle RmlUIBackend::LoadTexture(
    Rml::Vector2i& texture_dimensions,
    const Rml::String& source) {

    // Load image from file
    // This is application-specific (use your asset loading system)

    // For reference backend, this loads TGA files
    // You should integrate with your asset loader

    // Example stub:
    int width, height, channels;
    unsigned char* data = LoadImageFile(source, &width, &height, &channels);

    if (!data) {
        return 0;  // Failed to load
    }

    // Convert to RGBA if needed
    std::vector<unsigned char> rgba;
    if (channels == 3) {
        rgba = ConvertRGBToRGBA(data, width, height);
        data = rgba.data();
    }

    // Create texture
    GLuint handle;
    glGenTextures(1, &handle);
    glBindTexture(GL_TEXTURE_2D, handle);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    // Set filtering (important for crisp UI)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Free image data
    FreeImageData(data);

    // Store and return
    auto textureHandle = m_nextTextureHandle++;
    texture_dimensions.x = width;
    texture_dimensions.y = height;

    m_textures[textureHandle] = GLTexture{handle, width, height};

    return textureHandle;
}

Rml::TextureHandle RmlUIBackend::GenerateTexture(
    Rml::Span<const Rml::byte> source,
    Rml::Vector2i source_dimensions) {

    // RmlUI passes raw RGBA pixel data
    // Format: Tightly-packed rows, RGBA8, premultiplied alpha
    // Origin: Bottom-left (OpenGL convention)

    GLuint handle;
    glGenTextures(1, &handle);
    glBindTexture(GL_TEXTURE_2D, handle);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 source_dimensions.x, source_dimensions.y, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, source.data());

    // Set filtering
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Store and return
    auto textureHandle = m_nextTextureHandle++;
    m_textures[textureHandle] = GLTexture{
        handle,
        source_dimensions.x,
        source_dimensions.y
    };

    return textureHandle;
}

void RmlUIBackend::ReleaseTexture(Rml::TextureHandle texture) {
    auto it = m_textures.find(texture);
    if (it != m_textures.end()) {
        glDeleteTextures(1, &it->second.handle);
        m_textures.erase(it);
    }
}
```

### Scissor Implementation

```cpp
void RmlUIBackend::EnableScissorRegion(bool enable) {
    m_scissorEnabled = enable;

    if (m_primitives) {
        // If using Primitive API
        if (enable) {
            // Scissor will be set in SetScissorRegion
        } else {
            m_primitives->PopScissor();
        }
    } else {
        // Direct OpenGL
        if (enable) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
    }
}

void RmlUIBackend::SetScissorRegion(Rml::Rectanglei region) {
    if (!m_scissorEnabled) {
        return;
    }

    // Convert from RmlUI top-left origin to OpenGL bottom-left origin
    int x = region.p0.x;
    int y = m_viewportHeight - region.p1.y;  // Flip Y
    int width = region.p1.x - region.p0.x;
    int height = region.p1.y - region.p0.y;

    // Clamp to viewport (prevents WebGL validation errors)
    x = std::max(0, x);
    y = std::max(0, y);
    width = std::min(width, m_viewportWidth - x);
    height = std::min(height, m_viewportHeight - y);

    if (m_primitives) {
        // If using Primitive API
        Renderer::Rect scissorRect{
            static_cast<float>(x),
            static_cast<float>(y),
            static_cast<float>(width),
            static_cast<float>(height)
        };
        m_primitives->PushScissor(scissorRect);
    } else {
        // Direct OpenGL
        glScissor(x, y, width, height);
    }
}
```

### Transform Support

```cpp
void RmlUIBackend::SetTransform(const Rml::Matrix4f* transform) {
    if (transform) {
        m_hasTransform = true;

        // RmlUI matrices are COLUMN-MAJOR (same as OpenGL and glm)
        // Can copy directly
        m_transformMatrix = glm::make_mat4(&(*transform)[0][0]);
    } else {
        m_hasTransform = false;
        m_transformMatrix = glm::mat4(1.0f);
    }
}
```

**Important:** RmlUI uses column-major ordering, which matches OpenGL and glm. If you use a row-major graphics API (e.g., DirectX), you must transpose the matrix.

### Resource Cleanup

```cpp
void RmlUIBackend::ReleaseGeometry(Rml::CompiledGeometryHandle geometry) {
    auto it = m_geometries.find(geometry);
    if (it != m_geometries.end()) {
        glDeleteVertexArrays(1, &it->second.vao);
        glDeleteBuffers(1, &it->second.vbo);
        glDeleteBuffers(1, &it->second.ibo);
        m_geometries.erase(it);
    }
}
```

---

## Primitive API OpenGL Implementation

### Primitive API Design

From our documented architecture (`primitive-rendering-api.md`):

```cpp
// libs/renderer/include/renderer/Primitives.h
namespace Renderer {

struct Rect {
    float x, y, width, height;
};

struct Color {
    float r, g, b, a;
};

struct TextStyle {
    std::string fontFamily;
    float fontSize;
    // ...
};

class Primitives {
public:
    // Initialization
    static void Init(OpenGLRenderer* renderer);
    static void Shutdown();

    // Frame lifecycle
    static void BeginFrame();
    static void EndFrame();  // Flushes all batched geometry

    // Drawing primitives
    static void DrawRect(const Rect& bounds, const Color& color);
    static void DrawRectBorder(const Rect& bounds, const Color& color,
                               float borderWidth, float cornerRadius = 0.0f);
    static void DrawTexture(GLuint texture, const Rect& destBounds,
                           const Rect& sourceUV = {0, 0, 1, 1});
    static void DrawText(const std::string& text, const glm::vec2& position,
                        const TextStyle& style, const Color& color);

    // State management
    static void PushScissor(const Rect& clipRect);
    static void PopScissor();
    static void PushTransform(const glm::mat4& transform);
    static void PopTransform();

    // Immediate flush
    static void Flush();

private:
    static OpenGLRenderer* s_renderer;
    static BatchAccumulator s_batch;
    static std::vector<Rect> s_scissorStack;
    static std::vector<glm::mat4> s_transformStack;
};

} // namespace Renderer
```

### Batching Accumulator

```cpp
// libs/renderer/src/BatchAccumulator.h
namespace Renderer {

struct Vertex {
    glm::vec2 position;
    glm::vec4 color;
    glm::vec2 texCoord;
};

struct DrawBatch {
    GLuint texture;  // 0 for color-only
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    glm::mat4 transform;
    std::optional<Rect> scissor;
};

class BatchAccumulator {
public:
    BatchAccumulator();
    ~BatchAccumulator();

    void BeginFrame();
    void EndFrame();

    // Add primitives to batch
    void AddRect(const Rect& bounds, const Color& color);
    void AddTexturedQuad(GLuint texture, const Rect& bounds,
                        const Rect& uvs, const Color& tint = {1,1,1,1});

    // State changes trigger flush
    void SetScissor(const Rect* scissor);
    void SetTransform(const glm::mat4& transform);

    // Explicit flush
    void Flush();

private:
    std::vector<DrawBatch> m_batches;
    DrawBatch* m_currentBatch = nullptr;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ibo = 0;
    size_t m_vboCapacity = 0;
    size_t m_iboCapacity = 0;

    GLuint m_shaderProgram = 0;
    GLint m_uniformProjection = -1;
    GLint m_uniformTransform = -1;
    GLint m_uniformTexture = -1;
    GLint m_uniformUseTexture = -1;

    glm::mat4 m_currentTransform;
    std::optional<Rect> m_currentScissor;

    void StartNewBatch(GLuint texture);
    void FlushCurrentBatch();
    void EnsureCapacity(size_t vertices, size_t indices);
};

} // namespace Renderer
```

### Batch Flush Triggers

```cpp
void BatchAccumulator::AddTexturedQuad(
    GLuint texture,
    const Rect& bounds,
    const Rect& uvs,
    const Color& tint) {

    // Flush if state changed
    bool needsFlush = false;

    if (m_currentBatch) {
        // Different texture?
        if (m_currentBatch->texture != texture) {
            needsFlush = true;
        }

        // Different transform?
        if (m_currentBatch->transform != m_currentTransform) {
            needsFlush = true;
        }

        // Different scissor?
        if (m_currentBatch->scissor != m_currentScissor) {
            needsFlush = true;
        }

        // Batch too large?
        const size_t MAX_QUADS_PER_BATCH = 10000;
        if (m_currentBatch->vertices.size() / 4 >= MAX_QUADS_PER_BATCH) {
            needsFlush = true;
        }
    }

    if (needsFlush) {
        FlushCurrentBatch();
    }

    // Start new batch if needed
    if (!m_currentBatch) {
        StartNewBatch(texture);
    }

    // Add quad vertices
    size_t baseIndex = m_currentBatch->vertices.size();

    m_currentBatch->vertices.push_back({
        {bounds.x, bounds.y},
        {tint.r, tint.g, tint.b, tint.a},
        {uvs.x, uvs.y}
    });

    m_currentBatch->vertices.push_back({
        {bounds.x + bounds.width, bounds.y},
        {tint.r, tint.g, tint.b, tint.a},
        {uvs.x + uvs.width, uvs.y}
    });

    m_currentBatch->vertices.push_back({
        {bounds.x + bounds.width, bounds.y + bounds.height},
        {tint.r, tint.g, tint.b, tint.a},
        {uvs.x + uvs.width, uvs.y + uvs.height}
    });

    m_currentBatch->vertices.push_back({
        {bounds.x, bounds.y + bounds.height},
        {tint.r, tint.g, tint.b, tint.a},
        {uvs.x, uvs.y + uvs.height}
    });

    // Add quad indices (two triangles)
    m_currentBatch->indices.push_back(baseIndex + 0);
    m_currentBatch->indices.push_back(baseIndex + 1);
    m_currentBatch->indices.push_back(baseIndex + 2);

    m_currentBatch->indices.push_back(baseIndex + 0);
    m_currentBatch->indices.push_back(baseIndex + 2);
    m_currentBatch->indices.push_back(baseIndex + 3);
}
```

### Rendering Batches

```cpp
void BatchAccumulator::FlushCurrentBatch() {
    if (!m_currentBatch || m_currentBatch->vertices.empty()) {
        return;
    }

    // Ensure GPU buffers are large enough
    EnsureCapacity(m_currentBatch->vertices.size(),
                   m_currentBatch->indices.size());

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    m_currentBatch->vertices.size() * sizeof(Vertex),
                    m_currentBatch->vertices.data());

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    m_currentBatch->indices.size() * sizeof(uint32_t),
                    m_currentBatch->indices.data());

    // Setup shader
    glUseProgram(m_shaderProgram);

    // Set uniforms
    glUniformMatrix4fv(m_uniformTransform, 1, GL_FALSE,
                      glm::value_ptr(m_currentBatch->transform));

    // Bind texture (if any)
    if (m_currentBatch->texture != 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_currentBatch->texture);
        glUniform1i(m_uniformTexture, 0);
        glUniform1i(m_uniformUseTexture, 1);
    } else {
        glUniform1i(m_uniformUseTexture, 0);
    }

    // Setup scissor (if any)
    if (m_currentBatch->scissor) {
        glEnable(GL_SCISSOR_TEST);
        const Rect& s = *m_currentBatch->scissor;
        glScissor(static_cast<int>(s.x),
                 static_cast<int>(s.y),
                 static_cast<int>(s.width),
                 static_cast<int>(s.height));
    } else {
        glDisable(GL_SCISSOR_TEST);
    }

    // Draw
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(m_currentBatch->indices.size()),
                   GL_UNSIGNED_INT,
                   nullptr);

    // Clear batch
    m_currentBatch->vertices.clear();
    m_currentBatch->indices.clear();
}
```

### Performance Targets

**Measured on GL3 reference backend:**
- ~1000 UI elements = ~100-200 draw calls (without batching)
- Each draw call: ~10-50 microseconds overhead
- Total UI rendering: ~2-5ms for complex UI

**With batching:**
- Same 1000 elements = ~10-20 draw calls (batched by texture)
- Total UI rendering: ~0.5-1ms
- **5-10x improvement**

**Target performance:**
- Complex UI (inventory grid, skill tree): < 2ms per frame
- Simple HUD: < 0.5ms per frame
- 60 FPS budget: 16.67ms total, UI should be < 10%

---

## Integration Patterns

### Main Loop Integration

**Critical order from RmlUI documentation:**

```cpp
void Game::MainLoop() {
    while (m_running) {
        float deltaTime = CalculateDeltaTime();

        // 1. PROCESS INPUT (before UI update)
        ProcessEvents();
        InjectInputIntoRmlUI();

        // 2. UPDATE GAME LOGIC
        UpdateGameState(deltaTime);

        // 3. UPDATE UI ELEMENTS
        // Bind game data to UI (health bars, resource counts, etc.)
        UpdateUIDataBindings();

        // 4. UPDATE RMLUI CONTEXT
        // This resolves layout, animations, etc.
        m_uiContext->Update();

        // 5. CRITICAL: DO NOT modify UI elements after this point

        // 6. RENDER GAME
        RenderGameWorld();
        RenderWorldSpaceUI();

        // 7. RENDER RMLUI
        m_uiBackend->BeginFrame();
        m_uiContext->Render();
        m_uiBackend->EndFrame();

        // 8. PRESENT
        SwapBuffers();
    }
}
```

**Why this order matters:**

1. **Input before Update**: UI needs latest input to handle hover, focus, clicks
2. **Update before Render**: Layout must be resolved before rendering
3. **No modifications between Update and Render**: Causes visual glitches and crashes

**Example violation:**
```cpp
// WRONG: Modifying element after Update
m_uiContext->Update();
auto element = document->GetElementById("health");
element->SetInnerRML("100");  // ❌ CAUSES BUGS!
m_uiContext->Render();
```

**Correct approach:**
```cpp
// RIGHT: Modify before Update
auto element = document->GetElementById("health");
element->SetInnerRML("100");  // ✅ Safe
m_uiContext->Update();
m_uiContext->Render();
```

### Initialization Sequence

```cpp
bool Game::InitializeUI() {
    // 1. Create OpenGL context (SDL, GLFW, etc.)
    CreateOpenGLContext();

    // 2. Initialize OpenGL loader (gl3w, GLAD, etc.)
    if (gl3wInit() != 0) {
        return false;
    }

    // 3. Create RmlUI backend
    m_primitives = new Renderer::Primitives();
    m_uiBackend = new UI::Internal::RmlUIBackend(m_primitives);

    // 4. Install interfaces (BEFORE Rml::Initialise)
    Rml::SetRenderInterface(m_uiBackend);
    Rml::SetSystemInterface(&m_systemInterface);  // Your implementation

    // 5. Initialize RmlUI
    if (!Rml::Initialise()) {
        return false;
    }

    // 6. Create context
    m_uiContext = Rml::CreateContext("main",
        Rml::Vector2i(m_windowWidth, m_windowHeight));

    if (!m_uiContext) {
        return false;
    }

    // 7. Load fonts (BEFORE loading documents)
    if (!Rml::LoadFontFace("assets/fonts/LatoLatin-Regular.ttf")) {
        return false;
    }

    // 8. Optional: Load debugger
#ifdef _DEBUG
    if (Rml::Debugger::Initialise(m_uiContext)) {
        m_debuggerEnabled = true;
    }
#endif

    // 9. Load documents
    auto document = m_uiContext->LoadDocument("assets/ui/main_menu.rml");
    if (document) {
        document->Show();
    }

    return true;
}
```

### Shutdown Sequence

```cpp
void Game::ShutdownUI() {
    // 1. Close all documents
    if (m_uiContext) {
        m_uiContext->UnloadAllDocuments();
    }

    // 2. Remove context
    if (m_uiContext) {
        Rml::RemoveContext(m_uiContext->GetName());
        m_uiContext = nullptr;
    }

    // 3. Shutdown RmlUI
    Rml::Shutdown();

    // 4. Delete backend (AFTER Rml::Shutdown)
    delete m_uiBackend;
    m_uiBackend = nullptr;

    delete m_primitives;
    m_primitives = nullptr;

    // 5. Destroy OpenGL context
    DestroyOpenGLContext();
}
```

**Critical:** Interfaces must remain alive until AFTER `Rml::Shutdown()`.

### Input Injection

```cpp
void Game::InjectInputIntoRmlUI() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_MOUSEMOTION:
                m_uiContext->ProcessMouseMove(
                    event.motion.x,
                    event.motion.y,
                    GetRmlKeyModifiers()
                );
                break;

            case SDL_MOUSEBUTTONDOWN:
                m_uiContext->ProcessMouseButtonDown(
                    ConvertSDLButton(event.button.button),
                    GetRmlKeyModifiers()
                );
                break;

            case SDL_MOUSEBUTTONUP:
                m_uiContext->ProcessMouseButtonUp(
                    ConvertSDLButton(event.button.button),
                    GetRmlKeyModifiers()
                );
                break;

            case SDL_MOUSEWHEEL:
                m_uiContext->ProcessMouseWheel(
                    static_cast<float>(event.wheel.y),
                    GetRmlKeyModifiers()
                );
                break;

            case SDL_KEYDOWN:
                m_uiContext->ProcessKeyDown(
                    ConvertSDLKey(event.key.keysym.sym),
                    GetRmlKeyModifiers()
                );

                // RmlUI does NOT convert key presses to text!
                // Must handle separately
                break;

            case SDL_TEXTINPUT:
                // Convert text input to RmlUI
                Rml::String text(event.text.text);
                for (size_t i = 0; i < text.size(); ) {
                    Rml::Character character = Rml::StringUtilities::ToCharacter(
                        text.data() + i,
                        text.data() + text.size(),
                        i
                    );
                    m_uiContext->ProcessTextInput(character);
                }
                break;
        }

        // Also handle game input
        HandleGameInput(event);
    }
}
```

**Important:** RmlUI does NOT translate key presses to text input. You must use `SDL_TEXTINPUT` events (or equivalent) and inject text separately.

### Viewport Handling

```cpp
void Game::OnWindowResize(int width, int height) {
    // Update OpenGL viewport
    glViewport(0, 0, width, height);

    // Update RmlUI context dimensions
    if (m_uiContext) {
        m_uiContext->SetDimensions(Rml::Vector2i(width, height));
    }

    // Update backend viewport (for scissor coordinate conversion)
    if (m_uiBackend) {
        m_uiBackend->SetViewport(width, height);
    }

    // Update projection matrix
    UpdateProjectionMatrix(width, height);
}
```

---

## State Management

### The Critical Problem

**RmlUI modifies OpenGL state:**
- Enables blending
- Changes blend function
- Enables/disables scissor test
- Binds textures, VAOs, shaders
- Modifies viewport (sometimes)

**Your game also uses OpenGL:**
- Different shaders
- Different blend modes
- Different textures
- Depth testing enabled/disabled

**If you don't manage state:** RmlUI's state changes corrupt game rendering, or game's state changes corrupt UI rendering.

### Solution: State Backup/Restore

**From GL3 reference backend:**

```cpp
struct GLState {
    // Blend state
    GLboolean blendEnabled;
    GLint blendSrc;
    GLint blendDst;
    GLint blendEquationRGB;
    GLint blendEquationAlpha;

    // Depth/stencil
    GLboolean depthTestEnabled;
    GLboolean depthMaskEnabled;
    GLboolean stencilTestEnabled;

    // Face culling
    GLboolean cullFaceEnabled;
    GLint cullFaceMode;
    GLint frontFace;

    // Scissor
    GLboolean scissorTestEnabled;
    GLint scissorBox[4];

    // Viewport
    GLint viewport[4];

    // Bindings
    GLint currentProgram;
    GLint currentTexture;
    GLint currentVAO;
    GLint currentArrayBuffer;
    GLint currentElementArrayBuffer;

    // Pixel store
    GLint unpackAlignment;
    GLint unpackRowLength;
};
```

### Backup Implementation

```cpp
void RmlUIBackend::BackupGLState() {
    // Blend
    m_savedState.blendEnabled = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &m_savedState.blendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &m_savedState.blendDst);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &m_savedState.blendEquationRGB);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &m_savedState.blendEquationAlpha);

    // Depth/stencil
    m_savedState.depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &m_savedState.depthMaskEnabled);
    m_savedState.stencilTestEnabled = glIsEnabled(GL_STENCIL_TEST);

    // Face culling
    m_savedState.cullFaceEnabled = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CULL_FACE_MODE, &m_savedState.cullFaceMode);
    glGetIntegerv(GL_FRONT_FACE, &m_savedState.frontFace);

    // Scissor
    m_savedState.scissorTestEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_SCISSOR_BOX, m_savedState.scissorBox);

    // Viewport
    glGetIntegerv(GL_VIEWPORT, m_savedState.viewport);

    // Bindings
    glGetIntegerv(GL_CURRENT_PROGRAM, &m_savedState.currentProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_savedState.currentTexture);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &m_savedState.currentVAO);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &m_savedState.currentArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &m_savedState.currentElementArrayBuffer);

    // Pixel store
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &m_savedState.unpackAlignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &m_savedState.unpackRowLength);
}
```

### Restore Implementation

```cpp
void RmlUIBackend::RestoreGLState() {
    // Blend
    if (m_savedState.blendEnabled) glEnable(GL_BLEND);
    else glDisable(GL_BLEND);
    glBlendFunc(m_savedState.blendSrc, m_savedState.blendDst);
    glBlendEquationSeparate(m_savedState.blendEquationRGB,
                           m_savedState.blendEquationAlpha);

    // Depth/stencil
    if (m_savedState.depthTestEnabled) glEnable(GL_DEPTH_TEST);
    else glDisable(GL_DEPTH_TEST);
    glDepthMask(m_savedState.depthMaskEnabled);
    if (m_savedState.stencilTestEnabled) glEnable(GL_STENCIL_TEST);
    else glDisable(GL_STENCIL_TEST);

    // Face culling
    if (m_savedState.cullFaceEnabled) glEnable(GL_CULL_FACE);
    else glDisable(GL_CULL_FACE);
    glCullFace(m_savedState.cullFaceMode);
    glFrontFace(m_savedState.frontFace);

    // Scissor
    if (m_savedState.scissorTestEnabled) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    glScissor(m_savedState.scissorBox[0],
             m_savedState.scissorBox[1],
             m_savedState.scissorBox[2],
             m_savedState.scissorBox[3]);

    // Viewport
    glViewport(m_savedState.viewport[0],
              m_savedState.viewport[1],
              m_savedState.viewport[2],
              m_savedState.viewport[3]);

    // Bindings
    glUseProgram(m_savedState.currentProgram);
    glBindTexture(GL_TEXTURE_2D, m_savedState.currentTexture);
    glBindVertexArray(m_savedState.currentVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_savedState.currentArrayBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_savedState.currentElementArrayBuffer);

    // Pixel store
    glPixelStorei(GL_UNPACK_ALIGNMENT, m_savedState.unpackAlignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, m_savedState.unpackRowLength);
}
```

### Setup UI State

```cpp
void RmlUIBackend::SetupGLState() {
    // Enable blending with premultiplied alpha
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // Disable depth test (UI is 2D overlay)
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    // Disable face culling (or ensure CCW front faces)
    glDisable(GL_CULL_FACE);

    // Disable stencil (unless using clip mask feature)
    glDisable(GL_STENCIL_TEST);

    // Set pixel unpack alignment for textures
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}
```

### Frame Lifecycle

```cpp
void RmlUIBackend::BeginFrame() {
    BackupGLState();
    SetupGLState();
}

void RmlUIBackend::EndFrame() {
    RestoreGLState();
}
```

**Usage in game loop:**
```cpp
// Backup GL state, setup for UI rendering
m_uiBackend->BeginFrame();

// RmlUI renders (modifies GL state freely)
m_uiContext->Render();

// Restore GL state for game rendering
m_uiBackend->EndFrame();
```

---

## Production Best Practices

### 1. Always Backup/Restore State

**Never skip this.** Even if "it works for now", state corruption will cause subtle bugs that are extremely hard to track down.

```cpp
// ❌ WRONG: No state management
m_uiContext->Render();

// ✅ RIGHT: Proper state management
m_uiBackend->BeginFrame();
m_uiContext->Render();
m_uiBackend->EndFrame();
```

### 2. Preserve Render Order

RmlUI renders in a specific order for correct visual output. **Never** batch or reorder RenderGeometry calls.

```cpp
// ❌ WRONG: Reordering by texture
std::sort(renderCalls.begin(), renderCalls.end(),
    [](auto& a, auto& b) { return a.texture < b.texture; });

// ✅ RIGHT: Render in order received
for (auto& call : renderCalls) {
    RenderGeometry(call);
}
```

### 3. Handle Viewport Changes

Update both OpenGL and RmlUI when window size changes:

```cpp
void OnResize(int width, int height) {
    glViewport(0, 0, width, height);
    m_uiContext->SetDimensions(Rml::Vector2i(width, height));
    m_uiBackend->SetViewport(width, height);  // For scissor Y-flip
    UpdateProjectionMatrix(width, height);
}
```

### 4. Use Visual Testing

RmlUI includes a visual test suite. Use it to verify your renderer:

```cpp
// Build RmlUI with samples
// Run: rmlui_visual_tests
// Compare your renderer output with reference backend
```

### 5. Integrate RmlUI Debugger

During development:

```cpp
#ifdef _DEBUG
    Rml::Debugger::Initialise(context);
#endif

// In game loop:
if (ImGui::IsKeyPressed(SDLK_F8)) {
    Rml::Debugger::SetVisible(!Rml::Debugger::IsVisible());
}
```

### 6. Profile Early

Don't assume RmlUI is slow or fast:

```cpp
auto start = std::chrono::high_resolution_clock::now();
m_uiContext->Update();
auto updateTime = std::chrono::high_resolution_clock::now() - start;

start = std::chrono::high_resolution_clock::now();
m_uiContext->Render();
auto renderTime = std::chrono::high_resolution_clock::now() - start;

LOG_DEBUG("UI Update: {}ms, Render: {}ms",
          updateTime.count() * 0.001,
          renderTime.count() * 0.001);
```

### 7. Load Fonts Before Documents

```cpp
// ✅ RIGHT order
Rml::LoadFontFace("font.ttf");
context->LoadDocument("menu.rml");

// ❌ WRONG order (text won't render)
context->LoadDocument("menu.rml");
Rml::LoadFontFace("font.ttf");
```

### 8. Handle Interface Lifetimes

```cpp
class Game {
    // Backend must outlive RmlUI
    std::unique_ptr<RmlUIBackend> m_backend;
    Rml::Context* m_context = nullptr;

    ~Game() {
        // Shutdown in correct order
        if (m_context) {
            Rml::RemoveContext(m_context->GetName());
        }
        Rml::Shutdown();
        // THEN backend is destroyed (unique_ptr)
    }
};
```

### 9. Implement High-Resolution Timer

```cpp
class SystemInterface : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        // Must return continuously increasing high-resolution time
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = now - m_startTime;
        return std::chrono::duration<double>(duration).count();
    }

private:
    std::chrono::high_resolution_clock::time_point m_startTime =
        std::chrono::high_resolution_clock::now();
};
```

Low-resolution timers (e.g., `SDL_GetTicks()` with millisecond precision) cause jerky animations.

### 10. Copy, Don't Link, the Reference Backend

**From RmlUI documentation:**

> "The provided backends are not intended to be used directly by client projects, but rather copied and modified as needed."

**Why?**
- Backends are examples, not libraries
- You need to customize for your project (texture loading, etc.)
- Easier to debug when you own the code
- No version compatibility issues

---

## Common Pitfalls

### 1. Face Culling Blank Screen

**Symptom:** UI renders blank, no errors in log.

**Cause:** DirectX-style face culling enabled (CW front faces), but RmlUI uses CCW winding.

**Fix:**
```cpp
// Option A: Disable face culling
glDisable(GL_CULL_FACE);

// Option B: Set correct winding
glEnable(GL_CULL_FACE);
glFrontFace(GL_CCW);
glCullFace(GL_BACK);
```

### 2. Upside-Down UI

**Symptom:** UI is vertically flipped.

**Cause:** Forgot to flip Y-axis in projection matrix.

**Fix:**
```cpp
// WRONG: Normal orthographic projection
glm::ortho(0, width, 0, height, -1, 1);

// RIGHT: Flipped Y for RmlUI
glm::ortho(0, width, height, 0, -1, 1);
//                    ^^^^^^ swapped
```

### 3. Scissor Regions Incorrect

**Symptom:** Scrolling containers show content outside bounds.

**Cause:** Forgot to flip Y-coordinate for scissor region.

**Fix:**
```cpp
// Must flip Y for OpenGL
int flippedY = viewportHeight - region.p1.y;
glScissor(region.p0.x, flippedY, width, height);
```

### 4. Crash on Shutdown

**Symptom:** Crash when closing application.

**Cause:** Backend destroyed before `Rml::Shutdown()`.

**Fix:**
```cpp
// RIGHT order:
Rml::RemoveContext(context->GetName());
Rml::Shutdown();  // THEN...
delete backend;    // ...destroy backend
```

### 5. Text Not Rendering

**Symptoms:**
- Blank text elements
- Console warning: "Font face ... not found"

**Causes:**
- Font not loaded before document
- Font file path incorrect
- Font doesn't contain required glyphs

**Fix:**
```cpp
// Verify font loaded successfully
if (!Rml::LoadFontFace("font.ttf")) {
    LOG_ERROR("Failed to load font");
}

// Test with debugger font
// In RML: <span style="font-family: rmlui-debugger-font;">Test</span>
```

### 6. Slow Animations

**Symptom:** Animations jerky or too slow.

**Cause:** Low-resolution timer in `SystemInterface::GetElapsedTime()`.

**Fix:**
```cpp
// WRONG: Low resolution (milliseconds)
return SDL_GetTicks() / 1000.0;

// RIGHT: High resolution (nanoseconds)
auto now = std::chrono::high_resolution_clock::now();
return std::chrono::duration<double>(now - m_start).count();
```

### 7. State Corruption

**Symptoms:**
- Game world renders with wrong blending
- UI renders with game's depth testing
- Random visual artifacts

**Cause:** No state backup/restore.

**Fix:**
```cpp
// MUST implement BeginFrame/EndFrame
m_backend->BeginFrame();  // Backup + setup
m_context->Render();
m_backend->EndFrame();     // Restore
```

### 8. Modifying Elements After Update

**Symptom:** Crash or visual glitches.

**Cause:** Modifying elements between `Update()` and `Render()`.

**Fix:**
```cpp
// WRONG:
context->Update();
element->SetInnerRML("text");  // ❌ Causes bugs!
context->Render();

// RIGHT:
element->SetInnerRML("text");  // ✅ Before Update
context->Update();
context->Render();
```

### 9. Incorrect Initialization Order

**Symptom:** Crash in `Rml::Initialise()` or when loading fonts.

**Cause:** Interfaces not set before initialization.

**Fix:**
```cpp
// RIGHT order:
Rml::SetRenderInterface(backend);    // 1. Set interfaces
Rml::SetSystemInterface(&system);
Rml::Initialise();                   // 2. Initialize
auto context = Rml::CreateContext(); // 3. Create context
Rml::LoadFontFace("font.ttf");       // 4. Load fonts
context->LoadDocument("menu.rml");   // 5. Load documents
```

### 10. Premultiplied Alpha Confusion

**Symptom:** UI has white halos around transparent edges, or colors look wrong.

**Cause:** Incorrect blend function.

**Fix:**
```cpp
// WRONG: Standard alpha blending
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

// RIGHT: Premultiplied alpha blending (RmlUI requirement)
glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
```

---

## Performance Considerations

### Expected Performance (from GL3 backend analysis)

**Reference implementation (no batching):**
- Simple UI (few elements): ~0.1-0.5ms
- Complex UI (inventory grid): ~2-5ms
- Very complex UI (skill tree): ~5-10ms

**With batching (our Primitive API):**
- Simple UI: ~0.05-0.2ms
- Complex UI: ~0.5-2ms
- Very complex UI: ~2-5ms

**Estimated improvement: 2-5x reduction in render time**

### Profiling Strategy

```cpp
struct UIPerformanceMetrics {
    float updateTimeMs;
    float renderTimeMs;
    int drawCalls;
    int triangles;
    int textureBinds;
};

UIPerformanceMetrics ProfileFrame() {
    UIPerformanceMetrics metrics;

    auto start = std::chrono::high_resolution_clock::now();
    m_context->Update();
    auto updateEnd = std::chrono::high_resolution_clock::now();

    ResetGLCounters();
    m_backend->BeginFrame();
    m_context->Render();
    m_backend->EndFrame();
    auto renderEnd = std::chrono::high_resolution_clock::now();

    metrics.updateTimeMs = DurationMs(start, updateEnd);
    metrics.renderTimeMs = DurationMs(updateEnd, renderEnd);
    metrics.drawCalls = GetGLDrawCallCount();
    metrics.triangles = GetGLTriangleCount();
    metrics.textureBinds = GetGLTextureBindCount();

    return metrics;
}
```

### Optimization Targets

**If UI render time > 2ms:**
1. Check draw call count (should be < 100 for complex UI)
2. Implement batching if not already done
3. Reduce texture changes (use texture atlases)
4. Profile geometry compilation (one-time cost)

**If UI update time > 1ms:**
1. Reduce layout thrashing (avoid reading layout properties during updates)
2. Minimize DOM manipulations
3. Cache element references
4. Use data bindings instead of manual updates

### Batching Effectiveness

```cpp
// Measure batching effectiveness
struct BatchStats {
    int totalRenderGeometryCalls;
    int actualDrawCalls;
    float batchingRatio;  // Higher is better
};

BatchStats stats = m_backend->GetBatchStats();
stats.batchingRatio = static_cast<float>(stats.totalRenderGeometryCalls) /
                      static_cast<float>(stats.actualDrawCalls);

// Target: 5-10x batching ratio for complex UIs
// Example: 1000 RenderGeometry calls → 100 actual draw calls = 10x
```

### Memory Usage

**Expected memory usage:**
- RmlUI library: ~1-2 MB
- Fonts (SDF atlases): ~2-10 MB per font
- UI textures: ~10-50 MB (depending on assets)
- Geometry buffers: ~1-5 MB (for compiled geometry)
- Total: ~15-70 MB for typical game UI

**Optimization:**
- Use texture atlases (reduce texture memory and bind count)
- Share fonts across documents
- Release unused documents (call `Close()`)
- Reuse documents instead of creating new ones

---

## Testing and Validation

### Visual Testing

**RmlUI provides a visual test suite:**

```bash
# Build RmlUI with samples
cmake -DBUILD_SAMPLES=ON -DBUILD_TESTING=ON ..
make

# Run visual tests
./rmlui_visual_tests

# Opens window showing test cases
# Press Space to cycle through tests
# Compare your backend output with reference screenshots
```

**Test cases include:**
- Basic shapes (rects, borders, rounded corners)
- Text rendering (different sizes, styles, colors)
- Transforms (rotation, scale, skew)
- Filters (blur, drop-shadow, brightness)
- Clipping (scissor regions, overflow)
- Gradients (linear, radial, conic)
- Box model (margins, padding, borders)

### Integration Testing

```cpp
class UIRenderTest {
public:
    void TestScissorRegion() {
        // Create document with overflow: scroll
        auto doc = context->LoadDocument("test_scroll.rml");
        doc->Show();

        // Render
        context->Update();
        backend->BeginFrame();
        context->Render();
        backend->EndFrame();

        // Verify scissor was enabled
        ASSERT_TRUE(wasScissorEnabled);

        // Verify scissor region Y-flipped correctly
        ASSERT_EQ(scissorRegion.y, viewportHeight - expectedBottom);
    }

    void TestStateRestoration() {
        // Set game state
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        GLint blendSrc, blendDst;
        glGetIntegerv(GL_BLEND_SRC, &blendSrc);
        glGetIntegerv(GL_BLEND_DST, &blendDst);

        // Render UI (modifies state)
        backend->BeginFrame();
        context->Render();
        backend->EndFrame();

        // Verify state restored
        ASSERT_TRUE(glIsEnabled(GL_DEPTH_TEST));
        ASSERT_TRUE(glIsEnabled(GL_CULL_FACE));
        GLint restoredSrc, restoredDst;
        glGetIntegerv(GL_BLEND_SRC, &restoredSrc);
        glGetIntegerv(GL_BLEND_DST, &restoredDst);
        ASSERT_EQ(restoredSrc, blendSrc);
        ASSERT_EQ(restoredDst, blendDst);
    }
};
```

### Debug Visualization

```cpp
void RenderDebugOverlay() {
    if (!debugModeEnabled) return;

    // Show scissor regions
    for (auto& scissor : activeScissorRegions) {
        DrawDebugRect(scissor, Color::Red);
    }

    // Show element bounds
    auto hoveredElement = context->GetHoverElement();
    if (hoveredElement) {
        auto bounds = hoveredElement->GetAbsoluteBounds();
        DrawDebugRect(bounds, Color::Green);
    }

    // Show performance metrics
    DrawText(fmt::format("UI: {:.2f}ms ({} draw calls)",
                        uiRenderTime, drawCallCount),
             10, 10);
}
```

### Logging and Diagnostics

```cpp
class RmlUIBackend : public Rml::RenderInterface {
    void RenderGeometry(...) override {
#ifdef RMLUI_DEBUG
        LOG_DEBUG("RenderGeometry: handle={}, translation=({}, {}), texture={}",
                  geometry, translation.x, translation.y, texture);
#endif
        // ... render
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
#ifdef RMLUI_DEBUG
        LOG_DEBUG("SetScissor: ({}, {}) - ({}, {})",
                  region.p0.x, region.p0.y,
                  region.p1.x, region.p1.y);
#endif
        // ... setup scissor
    }
};
```

---

## Conclusion

This guide provides production-ready guidance for integrating RmlUI with OpenGL, based on:

1. **Official RmlUI documentation** and best practices
2. **GL3 reference backend** analysis and patterns
3. **Real-world production requirements** (state management, performance)
4. **Our custom architecture** (Primitive API, batching, isolation layer)

**Key Takeaways:**

✅ **State Management is Critical** - Always backup/restore OpenGL state
✅ **Coordinate System Matters** - Y-axis must be flipped for OpenGL
✅ **Render Order is Sacred** - Never batch or reorder RenderGeometry calls
✅ **Batching Happens Below** - RmlUI backend calls Primitive API, which batches
✅ **Reference Backend is Your Friend** - Copy, study, and adapt it

**Next Steps:**

1. Implement `RmlUIBackend` class (start with direct OpenGL, no Primitive API)
2. Test with simple RML document (colored rectangles)
3. Verify state backup/restore works
4. Add scissor support and test with scrolling container
5. Add transform support and test with CSS transforms
6. Integrate with Primitive API (optional optimization)
7. Profile and validate performance targets

**Remember:** Start simple, test incrementally, and always validate against the visual test suite.

---

## References

- **RmlUI Official Documentation**: https://mikke89.github.io/RmlUiDoc/
- **RmlUI GitHub Repository**: https://github.com/mikke89/RmlUi
- **Reference Backend**: `Backends/RmlUi_Renderer_GL3.cpp`
- **RenderInterface Header**: `Include/RmlUi/Core/RenderInterface.h`
- **Visual Test Suite**: `Tests/VisualTests/` in RmlUI repository

**Related Documentation:**
- [primitive-rendering-api.md](./primitive-rendering-api.md) - Our Primitive API design
- [rmlui-integration-architecture.md](./rmlui-integration-architecture.md) - Integration architecture
- [rendering-boundaries.md](./rendering-boundaries.md) - Usage boundaries
- [library-isolation-strategy.md](./library-isolation-strategy.md) - Isolation pattern
