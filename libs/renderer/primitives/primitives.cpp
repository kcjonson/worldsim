// Primitive Rendering API implementation.
// Provides immediate-mode 2D drawing functions with internal batching.

#include "primitives/primitives.h"
#include "primitives/batch_renderer.h"
#include <stack>
#include <glm/gtc/matrix_transform.hpp>

namespace Renderer {
namespace Primitives {

// Internal state
static BatchRenderer* s_batchRenderer = nullptr;
static std::stack<Foundation::Rect> s_scissorStack;
static std::stack<Foundation::Mat4> s_transformStack;
static Foundation::Rect s_currentScissor;
static Foundation::Mat4 s_currentTransform = Foundation::Mat4(1.0f);

// --- Initialization ---

void Init(Renderer* renderer) {
	s_batchRenderer = new BatchRenderer();
	s_batchRenderer->Init();

	// Initialize identity transform
	s_currentTransform = Foundation::Mat4(1.0f);
}

void Shutdown() {
	if (s_batchRenderer) {
		s_batchRenderer->Shutdown();
		delete s_batchRenderer;
		s_batchRenderer = nullptr;
	}
}

// --- Frame Lifecycle ---

void BeginFrame() {
	if (s_batchRenderer) {
		s_batchRenderer->BeginFrame();
	}
}

void EndFrame() {
	if (s_batchRenderer) {
		s_batchRenderer->EndFrame();
	}
}

void SetViewport(int width, int height) {
	if (s_batchRenderer) {
		s_batchRenderer->SetViewport(width, height);
	}
}

// --- Drawing Functions ---

void DrawRect(const Foundation::Rect& bounds, const Foundation::Color& color) {
	if (s_batchRenderer) {
		s_batchRenderer->AddQuad(bounds, color);
	}
}

void DrawRectBorder(const Foundation::Rect& bounds, const Foundation::Color& color, float borderWidth, float cornerRadius) {
	// For now, draw 4 lines (no corner radius support yet)
	// TODO: Add corner radius support with tessellation

	DrawLine(bounds.TopLeft(), bounds.TopRight(), color, borderWidth);
	DrawLine(bounds.TopRight(), bounds.BottomRight(), color, borderWidth);
	DrawLine(bounds.BottomRight(), bounds.BottomLeft(), color, borderWidth);
	DrawLine(bounds.BottomLeft(), bounds.TopLeft(), color, borderWidth);
}

void DrawLine(const Foundation::Vec2& start, const Foundation::Vec2& end, const Foundation::Color& color, float width) {
	// Draw line as thin rectangle
	Foundation::Vec2 dir = end - start;
	float length = glm::length(dir);

	if (length < 0.001f)
		return; // Too short to draw

	Foundation::Vec2 normal = Foundation::Vec2(-dir.y, dir.x) / length;
	Foundation::Vec2 offset = normal * (width * 0.5f);

	// Create quad for line
	Foundation::Vec2 p0 = start - offset;
	Foundation::Vec2 p1 = start + offset;
	Foundation::Vec2 p2 = end + offset;
	Foundation::Vec2 p3 = end - offset;

	// Calculate bounding box
	float minX = glm::min(glm::min(p0.x, p1.x), glm::min(p2.x, p3.x));
	float maxX = glm::max(glm::max(p0.x, p1.x), glm::max(p2.x, p3.x));
	float minY = glm::min(glm::min(p0.y, p1.y), glm::min(p2.y, p3.y));
	float maxY = glm::max(glm::max(p0.y, p1.y), glm::max(p2.y, p3.y));

	Foundation::Rect bounds(minX, minY, maxX - minX, maxY - minY);
	DrawRect(bounds, color);
}

// --- Scissor Stack ---

void PushScissor(const Foundation::Rect& clipRect) {
	// Intersect with current scissor (nested clipping)
	if (!s_scissorStack.empty()) {
		s_currentScissor = Foundation::Rect::Intersection(s_currentScissor, clipRect);
	} else {
		s_currentScissor = clipRect;
	}

	s_scissorStack.push(s_currentScissor);

	// TODO: Apply to OpenGL state
	// For now, just track it
}

void PopScissor() {
	if (!s_scissorStack.empty()) {
		s_scissorStack.pop();

		if (!s_scissorStack.empty()) {
			s_currentScissor = s_scissorStack.top();
		} else {
			s_currentScissor = Foundation::Rect(); // Clear scissor
		}

		// TODO: Apply to OpenGL state
	}
}

Foundation::Rect GetCurrentScissor() {
	return s_currentScissor;
}

// --- Transform Stack ---

void PushTransform(const Foundation::Mat4& transform) {
	s_transformStack.push(s_currentTransform);
	s_currentTransform = s_currentTransform * transform;

	// TODO: Apply to batch renderer
}

void PopTransform() {
	if (!s_transformStack.empty()) {
		s_currentTransform = s_transformStack.top();
		s_transformStack.pop();

		// TODO: Apply to batch renderer
	}
}

Foundation::Mat4 GetCurrentTransform() {
	return s_currentTransform;
}

// --- Statistics ---

RenderStats GetStats() {
	RenderStats stats = {0, 0, 0};

	if (s_batchRenderer) {
		auto batchStats = s_batchRenderer->GetStats();
		stats.drawCalls = batchStats.drawCalls;
		stats.vertexCount = batchStats.vertexCount;
		stats.triangleCount = batchStats.triangleCount;
	}

	return stats;
}

} // namespace Primitives
} // namespace Renderer
