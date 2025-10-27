#pragma once

// Primitive Rendering API - Unified 2D drawing interface.
//
// This API provides immediate-mode drawing functions used by:
// - RmlUI backend (screen-space UI panels)
// - Game world rendering (tiles, entities)
// - World-space UI (health bars, tooltips)
// - Custom UI components
//
// Implementation uses batching to minimize draw calls while maintaining a simple API.

#include "graphics/color.h"
#include "graphics/rect.h"
#include "math/types.h"
#include <string>

namespace Renderer {

// Forward declarations
class Renderer;

namespace Primitives {

// --- Initialization ---

void Init(Renderer* renderer);
void Shutdown();

// --- Frame Lifecycle ---

void BeginFrame();
void EndFrame(); // Flushes all batches

// Set viewport dimensions for projection matrix
void SetViewport(int width, int height);

// --- Drawing Functions ---

// Filled rectangles
void DrawRect(const Foundation::Rect& bounds, const Foundation::Color& color);

// Rectangle borders (outline only)
void DrawRectBorder(const Foundation::Rect& bounds, const Foundation::Color& color, float borderWidth, float cornerRadius = 0.0f);

// Lines
void DrawLine(const Foundation::Vec2& start, const Foundation::Vec2& end, const Foundation::Color& color, float width = 1.0f);

// --- State Management ---

// Scissor/clipping (for scrollable containers)
void PushScissor(const Foundation::Rect& clipRect);
void PopScissor();
Foundation::Rect GetCurrentScissor();

// Transform stack (for world-space rendering)
void PushTransform(const Foundation::Mat4& transform);
void PopTransform();
Foundation::Mat4 GetCurrentTransform();

// --- Statistics ---

struct RenderStats {
	uint32_t drawCalls;
	uint32_t vertexCount;
	uint32_t triangleCount;
};

RenderStats GetStats();

} // namespace Primitives
} // namespace Renderer
