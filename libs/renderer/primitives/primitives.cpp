// Primitive Rendering API implementation.
// Provides immediate-mode 2D drawing functions with internal batching.

#include "primitives/primitives.h"
#include "coordinate_system/coordinate_system.h"
#include "primitives/batch_renderer.h"
#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <numbers>
#include <optional>
#include <stack>
#include <vector>

// Forward declaration is enough for pointer usage
// Full include would require adding ui library as dependency to renderer

namespace Renderer::Primitives {

	// --- Command Queue Data Structures ---

	// Batch key - identifies which draw commands can be batched together
	// Commands with the same batch key share GPU state (shader, texture, blend mode)
	struct BatchKey {
		GLuint shader = 0;
		GLuint texture = 0;
		enum class BlendMode { None, Alpha, Additive };
		BlendMode blendMode = BlendMode::None;

		bool operator==(const BatchKey& other) const {
			return shader == other.shader && texture == other.texture && blendMode == other.blendMode;
		}

		bool operator<(const BatchKey& other) const {
			// Sort order: shader → texture → blend mode
			if (shader != other.shader)
				return shader < other.shader;
			if (texture != other.texture)
				return texture < other.texture;
			return static_cast<int>(blendMode) < static_cast<int>(other.blendMode);
		}
	};

	// Draw command for deferred rendering
	struct DrawCommand {
		BatchKey					 batchKey;		 // GPU state for batching
		float						 zIndex = 0.0F;	 // Render order
		bool						 isTransparent = false; // Opaque vs transparent pass
		std::optional<Foundation::Rect> scissor;		 // Optional clipping region
		const char*					 id = nullptr;	 // Debug identifier

		// Vertex data (triangles, lines, etc.)
		std::vector<float> vertices;
		GLenum			   primitiveType = GL_TRIANGLES; // GL_TRIANGLES, GL_LINES, etc.
	};

	// Internal state
	static std::unique_ptr<BatchRenderer> g_batchRenderer = nullptr;
	static CoordinateSystem*			  g_coordinateSystem = nullptr;
	static ui::FontRenderer*			  g_fontRenderer = nullptr;
	static ui::TextBatchRenderer*		  g_textBatchRenderer = nullptr;
	static std::stack<Foundation::Rect>	  g_scissorStack;
	static std::stack<Foundation::Mat4>	  g_transformStack;
	static Foundation::Rect				  g_currentScissor;
	static Foundation::Mat4				  g_currentTransform = Foundation::Mat4(1.0F);

	// Command queue for batched rendering
	static std::vector<DrawCommand> g_commandQueue;

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

	void SetFontRenderer(ui::FontRenderer* fontRenderer) {
		g_fontRenderer = fontRenderer;
	}

	ui::FontRenderer* GetFontRenderer() {
		return g_fontRenderer;
	}

	void SetTextBatchRenderer(ui::TextBatchRenderer* batchRenderer) {
		g_textBatchRenderer = batchRenderer;
	}

	ui::TextBatchRenderer* GetTextBatchRenderer() {
		return g_textBatchRenderer;
	}

	// --- Batch Key Helpers ---

	// Get batch key for solid color primitives (no texture)
	static BatchKey GetColorBatchKey(bool hasAlpha = false) {
		BatchKey key;
		key.shader = g_batchRenderer ? g_batchRenderer->GetShaderProgram() : 0;
		key.texture = 0; // No texture for solid colors
		key.blendMode = hasAlpha ? BatchKey::BlendMode::Alpha : BatchKey::BlendMode::None;
		return key;
	}

	// Get batch key for text rendering (uses font atlas texture)
	static BatchKey GetTextBatchKey(GLuint fontAtlasTexture) {
		BatchKey key;
		// TODO: Get text shader program (different from color shader)
		key.shader = g_batchRenderer ? g_batchRenderer->GetShaderProgram() : 0;
		key.texture = fontAtlasTexture;
		key.blendMode = BatchKey::BlendMode::Alpha; // Text always uses alpha blending
		return key;
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

		// Use SDF-based rendering for fill, border, and corner radius
		// Extract corner radius from border if present, otherwise use 0
		float cornerRadius = 0.0F;
		if (args.style.border.has_value() && args.style.border.value().cornerRadius > 0.0F) {
			cornerRadius = args.style.border.value().cornerRadius;
		}

		// Single AddQuad call handles fill, border, and rounded corners via GPU fragment shader
		g_batchRenderer->AddQuad(args.bounds, args.style.fill, args.style.border, cornerRadius);
	}

	void DrawLine(const LineArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Draw line as thin rectangle
		Foundation::Vec2 dir = args.end - args.start;
		float			 length = glm::length(dir);

		if (length < 0.001F) {
			return; // Too short to draw
		}

		Foundation::Vec2 normal = Foundation::Vec2(-dir.y, dir.x) / length;
		Foundation::Vec2 offset = normal * (args.style.width * 0.5F);

		// Create quad for line
		Foundation::Vec2 p0 = args.start - offset;
		Foundation::Vec2 p1 = args.start + offset;
		Foundation::Vec2 p2 = args.end + offset;
		Foundation::Vec2 p3 = args.end - offset;

		// Calculate bounding box
		float minX = glm::min(glm::min(p0.x, p1.x), glm::min(p2.x, p3.x));
		float maxX = glm::max(glm::max(p0.x, p1.x), glm::max(p2.x, p3.x));
		float minY = glm::min(glm::min(p0.y, p1.y), glm::min(p2.y, p3.y));
		float maxY = glm::max(glm::max(p0.y, p1.y), glm::max(p2.y, p3.y));

		Foundation::Rect bounds(minX, minY, maxX - minX, maxY - minY);
		g_batchRenderer->AddQuad(bounds, args.style.color, std::nullopt, 0.0F);
	}

	void DrawTriangles(const TrianglesArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		g_batchRenderer->AddTriangles(args.vertices, args.indices, args.vertexCount, args.indexCount, args.color);
	}

	void DrawCircle(const CircleArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Tessellate circle into triangle fan
		constexpr int	segments = 64; // Enough for smooth circles
		constexpr float angleStep = (2.0F * std::numbers::pi_v<float>) / static_cast<float>(segments);
		constexpr int	vertexCount = segments + 1; // Center + perimeter vertices
		constexpr int	indexCount = segments * 3;	// Each segment creates a triangle

		// Ensure vertex count fits in uint16_t index buffer
		static_assert(vertexCount <= 65535, "Circle vertex count exceeds uint16_t index range");

		// Use thread-local buffers to avoid allocations on every call
		static thread_local std::vector<Foundation::Vec2> vertices;
		static thread_local std::vector<uint16_t>		  indices;
		vertices.clear();
		indices.clear();
		vertices.reserve(vertexCount);
		indices.reserve(indexCount);

		// Center vertex
		vertices.push_back(args.center);

		// Perimeter vertices
		for (int i = 0; i < segments; ++i) {
			float angle = static_cast<float>(i) * angleStep;
			float x = args.center.x + args.radius * std::cos(angle);
			float y = args.center.y + args.radius * std::sin(angle);
			vertices.emplace_back(x, y);
		}

		// Generate triangle fan indices
		for (int i = 0; i < segments; ++i) {
			indices.push_back(0);											  // Center
			indices.push_back(static_cast<uint16_t>(i + 1));				  // Current perimeter vertex
			indices.push_back(static_cast<uint16_t>(1 + (i + 1) % segments)); // Next perimeter vertex
		}

		// Draw filled circle
		if (args.style.fill.a > 0.0F) {
			DrawTriangles(
				{.vertices = vertices.data(),
				 .indices = indices.data(),
				 .vertexCount = vertices.size(),
				 .indexCount = indices.size(),
				 .color = args.style.fill,
				 .id = args.id,
				 .zIndex = args.zIndex}
			);
		}

		// Draw border if specified
		if (args.style.border.has_value()) {
			const auto& border = args.style.border.value();

			// Draw border as connected line segments
			for (int i = 0; i < segments; ++i) {
				Foundation::Vec2 start = vertices[i + 1];
				Foundation::Vec2 end = vertices[1 + (i + 1) % segments];
				DrawLine({.start = start, .end = end, .style = {.color = border.color, .width = border.width}});
			}
		}
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
			stats.drawCalls = batchStats.drawCalls;
			stats.vertexCount = batchStats.vertexCount;
			stats.triangleCount = batchStats.triangleCount;
		}

		return stats;
	}

} // namespace Renderer::Primitives
