// Primitive Rendering API implementation.
// Provides immediate-mode 2D drawing functions with internal batching.

#include "primitives/primitives.h"
#include "coordinate_system/coordinate_system.h"
#include "primitives/batch_renderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <stack>

namespace Renderer::Primitives {

	// Internal state
	static std::unique_ptr<BatchRenderer> g_batchRenderer = nullptr;
	static CoordinateSystem*			  g_coordinateSystem = nullptr;
	static std::stack<Foundation::Rect>   g_scissorStack;
	static std::stack<Foundation::Mat4>   g_transformStack;
	static Foundation::Rect				  g_currentScissor;
	static Foundation::Mat4				  g_currentTransform = Foundation::Mat4(1.0F);

	// --- Initialization ---

	void Init(Renderer* renderer) {
		g_batchRenderer = std::make_unique<BatchRenderer>();
		g_batchRenderer->Init();

		// Initialize identity transform
		g_currentTransform = Foundation::Mat4(1.0F);
	}

	void Shutdown() {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->Shutdown();
			g_batchRenderer.reset();
		}
		g_coordinateSystem = nullptr;
	}

	void SetCoordinateSystem(CoordinateSystem* coordSystem) {
		g_coordinateSystem = coordSystem;
		// Also update the batch renderer with the coordinate system (even if nullptr)
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->SetCoordinateSystem(g_coordinateSystem);
		}
	}

	// --- Frame Lifecycle ---

	void BeginFrame() {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->BeginFrame();
		}
	}

	void EndFrame() {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->EndFrame();
		}
	}

	void SetViewport(int width, int height) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->SetViewport(width, height);
		}
	}

	void GetViewport(int& width, int& height) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->GetViewport(width, height);
		} else {
			width = 800;
			height = 600;
		}
	}

	// --- Coordinate System Helpers ---

	Foundation::Mat4 GetScreenSpaceProjection() {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->CreateScreenSpaceProjection();
		}
		// Fallback to default projection
		return glm::ortho(0.0F, 800.0F, 600.0F, 0.0F, -1.0F, 1.0F);
	}

	Foundation::Mat4 GetWorldSpaceProjection() {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->CreateWorldSpaceProjection();
		}
		// Fallback to default projection
		return glm::ortho(-400.0F, 400.0F, -300.0F, 300.0F, -1.0F, 1.0F);
	}

	float PercentWidth(float percent) {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->PercentWidth(percent);
		}
		return 800.0F * (percent / 100.0F);
	}

	float PercentHeight(float percent) {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->PercentHeight(percent);
		}
		return 600.0F * (percent / 100.0F);
	}

	Foundation::Vec2 PercentSize(float widthPercent, float heightPercent) {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->PercentSize(widthPercent, heightPercent);
		}
		return Foundation::Vec2(800.0F * (widthPercent / 100.0F), 600.0F * (heightPercent / 100.0F));
	}

	Foundation::Vec2 PercentPosition(float xPercent, float yPercent) {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->PercentPosition(xPercent, yPercent);
		}
		return Foundation::Vec2(800.0F * (xPercent / 100.0F), 600.0F * (yPercent / 100.0F));
	}

	// --- Drawing Functions ---

	void DrawRect(const RectArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Draw fill if specified
		if (args.m_style.fill.a > 0.0F) {
			g_batchRenderer->AddQuad(args.m_bounds, args.m_style.fill);
		}

		// Draw border if specified
		if (args.m_style.border.has_value()) {
			const auto& border = args.m_style.border.value();

			// For now, draw 4 lines (no corner radius support yet)
			// TODO: Add corner radius support with tessellation
			DrawLine(
				{.m_start = args.m_bounds.TopLeft(), .m_end = args.m_bounds.TopRight(), .m_style = {.color = border.color, .width = border.width}}
			);
			DrawLine(
				{.m_start = args.m_bounds.TopRight(),
				 .m_end = args.m_bounds.BottomRight(),
				 .m_style = {.color = border.color, .width = border.width}}
			);
			DrawLine(
				{.m_start = args.m_bounds.BottomRight(),
				 .m_end = args.m_bounds.BottomLeft(),
				 .m_style = {.color = border.color, .width = border.width}}
			);
			DrawLine(
				{.m_start = args.m_bounds.BottomLeft(),
				 .m_end = args.m_bounds.TopLeft(),
				 .m_style = {.color = border.color, .width = border.width}}
			);
		}
	}

	void DrawLine(const LineArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Draw line as thin rectangle
		Foundation::Vec2 dir = args.m_end - args.m_start;
		float			 length = glm::length(dir);

		if (length < 0.001F) {
			return; // Too short to draw
		}

		Foundation::Vec2 normal = Foundation::Vec2(-dir.y, dir.x) / length;
		Foundation::Vec2 offset = normal * (args.m_style.width * 0.5F);

		// Create quad for line
		Foundation::Vec2 p0 = args.m_start - offset;
		Foundation::Vec2 p1 = args.m_start + offset;
		Foundation::Vec2 p2 = args.m_end + offset;
		Foundation::Vec2 p3 = args.m_end - offset;

		// Calculate bounding box
		float minX = glm::min(glm::min(p0.x, p1.x), glm::min(p2.x, p3.x));
		float maxX = glm::max(glm::max(p0.x, p1.x), glm::max(p2.x, p3.x));
		float minY = glm::min(glm::min(p0.y, p1.y), glm::min(p2.y, p3.y));
		float maxY = glm::max(glm::max(p0.y, p1.y), glm::max(p2.y, p3.y));

		Foundation::Rect bounds(minX, minY, maxX - minX, maxY - minY);
		g_batchRenderer->AddQuad(bounds, args.m_style.color);
	}

	void DrawTriangles(const TrianglesArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		g_batchRenderer->AddTriangles(args.m_vertices, args.m_indices, args.m_vertexCount, args.m_indexCount, args.m_color);
	}

	// --- Scissor Stack ---

	void PushScissor(const Foundation::Rect& clipRect) {
		// Intersect with current scissor (nested clipping)
		if (!g_scissorStack.empty()) {
			g_currentScissor = Foundation::Rect::Intersection(g_currentScissor, clipRect);
		} else {
			g_currentScissor = clipRect;
		}

		g_scissorStack.push(g_currentScissor);

		// TODO: Apply to OpenGL state
		// For now, just track it
	}

	void PopScissor() {
		if (!g_scissorStack.empty()) {
			g_scissorStack.pop();

			if (!g_scissorStack.empty()) {
				g_currentScissor = g_scissorStack.top();
			} else {
				g_currentScissor = Foundation::Rect(); // Clear scissor
			}

			// TODO: Apply to OpenGL state
		}
	}

	Foundation::Rect GetCurrentScissor() {
		return g_currentScissor;
	}

	// --- Transform Stack ---

	void PushTransform(const Foundation::Mat4& transform) {
		g_transformStack.push(g_currentTransform);
		g_currentTransform = g_currentTransform * transform;

		// TODO: Apply to batch renderer
	}

	void PopTransform() {
		if (!g_transformStack.empty()) {
			g_currentTransform = g_transformStack.top();
			g_transformStack.pop();

			// TODO: Apply to batch renderer
		}
	}

	Foundation::Mat4 GetCurrentTransform() {
		return g_currentTransform;
	}

	// --- Statistics ---

	RenderStats GetStats() {
		RenderStats stats = {};

		if (g_batchRenderer != nullptr) {
			auto batchStats = g_batchRenderer->GetStats();
			stats.m_drawCalls = batchStats.m_drawCalls;
			stats.m_vertexCount = batchStats.m_vertexCount;
			stats.m_triangleCount = batchStats.m_triangleCount;
		}

		return stats;
	}

} // namespace Renderer::Primitives
