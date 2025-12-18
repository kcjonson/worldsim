// Primitive Rendering API implementation.
// Provides immediate-mode 2D drawing functions with internal batching.

#include "primitives/Primitives.h"
#include "CoordinateSystem/CoordinateSystem.h"
#include "graphics/ClipTypes.h"
#include "primitives/BatchRenderer.h"
#include <font/FontRenderer.h>
#include <utils/Log.h>
#include <GL/glew.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <numbers>
#include <optional>
#include <stack>
#include <vector>

// Text rendering is implemented via the unified uber shader in BatchRenderer.
// Text shapes call BatchRenderer::addTextQuad() directly to batch text with shapes.

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
	static FrameUpdateCallback			  g_frameUpdateCallback = nullptr;
	static std::stack<Foundation::Rect>	  g_scissorStack;
	static std::stack<Foundation::Mat4>	  g_transformStack;
	static Foundation::Rect				  g_currentScissor;
	static Foundation::Mat4				  g_currentTransform = Foundation::Mat4(1.0F);

	// Clip stack for shader-based clipping (preserves batching)
	// Each entry stores the ClipSettings and the computed bounds
	struct ClipStackEntry {
		Foundation::ClipSettings settings;
		Foundation::Vec4		 bounds; // Computed (minX, minY, maxX, maxY)
	};
	static std::stack<ClipStackEntry> g_clipStack;

	// Command queue for batched rendering
	static std::vector<DrawCommand> g_commandQueue;

	// --- Initialization ---

	void init(Renderer* renderer) {
		g_batchRenderer = std::make_unique<BatchRenderer>();
		g_batchRenderer->init();

		// Initialize identity transform
		g_currentTransform = Foundation::Mat4(1.0F);
	}

	void shutdown() {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->shutdown();
			g_batchRenderer.reset();
		}
		g_coordinateSystem = nullptr;
	}

	void setCoordinateSystem(CoordinateSystem* coordSystem) {
		g_coordinateSystem = coordSystem;
		// Also update the batch renderer with the coordinate system (even if nullptr)
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setCoordinateSystem(g_coordinateSystem);
		}
	}

	void setFontRenderer(ui::FontRenderer* fontRenderer) {
		g_fontRenderer = fontRenderer;
	}

	ui::FontRenderer* getFontRenderer() {
		return g_fontRenderer;
	}

	void setFontAtlas(unsigned int atlasTexture, float pixelRange) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setFontAtlas(atlasTexture, pixelRange);
		}
	}

	void setTileAtlas(unsigned int atlasTexture, const std::vector<glm::vec4>& rects) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setTileAtlas(atlasTexture, rects);
		}
	}

	BatchRenderer* getBatchRenderer() {
		return g_batchRenderer.get();
	}

	void setFrameUpdateCallback(FrameUpdateCallback callback) {
		g_frameUpdateCallback = callback;
	}

	// --- Batch Key Helpers ---

	// Get batch key for solid color primitives (no texture)
	static BatchKey GetColorBatchKey(bool hasAlpha = false) {
		BatchKey key;
		key.shader = g_batchRenderer ? g_batchRenderer->getShaderProgram() : 0;
		key.texture = 0; // No texture for solid colors
		key.blendMode = hasAlpha ? BatchKey::BlendMode::Alpha : BatchKey::BlendMode::None;
		return key;
	}

	// Get batch key for text rendering (uses font atlas texture)
	static BatchKey GetTextBatchKey(GLuint fontAtlasTexture) {
		BatchKey key;
		// TODO: Get text shader program (different from color shader)
		key.shader = g_batchRenderer ? g_batchRenderer->getShaderProgram() : 0;
		key.texture = fontAtlasTexture;
		key.blendMode = BatchKey::BlendMode::Alpha; // Text always uses alpha blending
		return key;
	}

	// --- Frame Lifecycle ---

	void beginFrame() {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->beginFrame();
		}

		// Invoke frame update callback for FontRenderer cache LRU tracking
		if (g_frameUpdateCallback != nullptr) {
			g_frameUpdateCallback();
		}
	}

	void endFrame() {
		// Flush all batched geometry (shapes + text) in a single draw call
		// The uber shader handles both SDF shapes and MSDF text
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->endFrame();
		}
	}

	void setViewport(int width, int height) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setViewport(width, height);
		}
	}

	void getViewport(int& width, int& height) {
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->getViewport(width, height);
		} else {
			width = 800;
			height = 600;
		}
	}

	void getLogicalViewport(int& width, int& height) {
		if (g_coordinateSystem != nullptr) {
			glm::vec2 windowSize = g_coordinateSystem->getWindowSize();
			width = static_cast<int>(windowSize.x);
			height = static_cast<int>(windowSize.y);
		} else if (g_batchRenderer != nullptr) {
			// Fallback to physical viewport if no coordinate system
			g_batchRenderer->getViewport(width, height);
		} else {
			width = 800;
			height = 600;
		}
	}

	// --- Coordinate System Helpers ---

	Foundation::Mat4 getScreenSpaceProjection() {
		if (g_coordinateSystem != nullptr) {
			return g_coordinateSystem->CreateScreenSpaceProjection();
		}
		// Fallback to default projection
		return glm::ortho(0.0F, 800.0F, 600.0F, 0.0F, -1.0F, 1.0F);
	}

	Foundation::Mat4 getWorldSpaceProjection() {
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

	void drawRect(const RectArgs& args) {
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
		g_batchRenderer->addQuad(args.bounds, args.style.fill, args.style.border, cornerRadius);
	}

	void drawLine(const LineArgs& args) {
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
		g_batchRenderer->addQuad(bounds, args.style.color, std::nullopt, 0.0F);
	}

	void drawTriangles(const TrianglesArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		g_batchRenderer->addTriangles(
			args.vertices, args.indices, args.vertexCount, args.indexCount, args.color, args.colors
		);
	}

	void drawTile(const TileArgs& args) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		g_batchRenderer->addTileQuad(
			args.bounds, args.color,
			args.edgeMask, args.cornerMask, args.surfaceId, args.hardEdgeMask,
			args.tileX, args.tileY,
			args.neighborN, args.neighborE, args.neighborS, args.neighborW,
			args.neighborNW, args.neighborNE, args.neighborSE, args.neighborSW
		);
	}

	void drawCircle(const CircleArgs& args) {
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
			drawTriangles(
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
				drawLine({.start = start, .end = end, .style = {.color = border.color, .width = border.width}});
			}
		}
	}

	void drawText(const TextArgs& args) {
		if (g_fontRenderer == nullptr || g_batchRenderer == nullptr) {
			LOG_WARNING(Engine, "drawText called but renderer not initialized (font=%p, batch=%p)",
				static_cast<void*>(g_fontRenderer), static_cast<void*>(g_batchRenderer.get()));
			return;
		}

		// Generate glyph quads from the font renderer
		std::vector<ui::FontRenderer::GlyphQuad> quads;
		g_fontRenderer->generateGlyphQuads(
			args.text,
			glm::vec2(args.position.x, args.position.y),
			args.scale,
			glm::vec4(args.color.r, args.color.g, args.color.b, args.color.a),
			quads
		);

		// Add each glyph quad to the batch renderer
		for (const auto& quad : quads) {
			g_batchRenderer->addTextQuad(
				Foundation::Vec2(quad.position.x, quad.position.y),
				Foundation::Vec2(quad.size.x, quad.size.y),
				Foundation::Vec2(quad.uvMin.x, quad.uvMin.y),
				Foundation::Vec2(quad.uvMax.x, quad.uvMax.y),
				Foundation::Color(quad.color.r, quad.color.g, quad.color.b, quad.color.a)
			);
		}
	}

	// --- Clip Stack (Shader-based, batching-friendly) ---

	// Helper to compute Vec4 bounds from ClipSettings
	static Foundation::Vec4 ComputeClipBounds(const Foundation::ClipSettings& settings) {
		// Check for ClipRect (fast path)
		if (const auto* clipRect = std::get_if<Foundation::ClipRect>(&settings.shape)) {
			if (clipRect->bounds.has_value()) {
				const auto& rect = clipRect->bounds.value();
				return Foundation::Vec4(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
			}
			// If no explicit bounds, use full viewport (no clipping)
			return Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
		}

		// Stub for future complex shapes (rounded rect, circle, path)
		// These will use stencil buffer in future phases
		if (std::holds_alternative<Foundation::ClipRoundedRect>(settings.shape)) {
			// TODO: Phase 3 - use stencil buffer for rounded rect
			// For now, use bounding box approximation
			const auto& rr = std::get<Foundation::ClipRoundedRect>(settings.shape);
			if (rr.bounds.has_value()) {
				const auto& rect = rr.bounds.value();
				return Foundation::Vec4(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
			}
		}

		if (std::holds_alternative<Foundation::ClipCircle>(settings.shape)) {
			// TODO: Phase 3 - use stencil buffer for circle
			// For now, use bounding box approximation
			const auto& circle = std::get<Foundation::ClipCircle>(settings.shape);
			float		minX = circle.center.x - circle.radius;
			float		minY = circle.center.y - circle.radius;
			float		maxX = circle.center.x + circle.radius;
			float		maxY = circle.center.y + circle.radius;
			return Foundation::Vec4(minX, minY, maxX, maxY);
		}

		if (std::holds_alternative<Foundation::ClipPath>(settings.shape)) {
			// TODO: Phase 3 - use stencil buffer for path
			// For now, compute bounding box of path vertices
			const auto& path = std::get<Foundation::ClipPath>(settings.shape);
			if (!path.vertices.empty()) {
				float minX = path.vertices[0].x;
				float minY = path.vertices[0].y;
				float maxX = minX;
				float maxY = minY;
				for (const auto& v : path.vertices) {
					minX = std::min(minX, v.x);
					minY = std::min(minY, v.y);
					maxX = std::max(maxX, v.x);
					maxY = std::max(maxY, v.y);
				}
				return Foundation::Vec4(minX, minY, maxX, maxY);
			}
		}

		// No clipping
		return Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
	}

	// Helper to intersect two clip bounds
	static Foundation::Vec4 IntersectClipBounds(const Foundation::Vec4& a, const Foundation::Vec4& b) {
		// If either is empty (0,0,0,0), use the other
		bool aEmpty = (a.z <= a.x || a.w <= a.y);
		bool bEmpty = (b.z <= b.x || b.w <= b.y);

		if (aEmpty)
			return b;
		if (bEmpty)
			return a;

		// Intersect the two rectangles
		float minX = std::max(a.x, b.x);
		float minY = std::max(a.y, b.y);
		float maxX = std::min(a.z, b.z);
		float maxY = std::min(a.w, b.w);

		// Check for no intersection (empty result)
		if (maxX <= minX || maxY <= minY) {
			return Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
		}

		return Foundation::Vec4(minX, minY, maxX, maxY);
	}

	void pushClip(const Foundation::ClipSettings& settings) {
		if (g_batchRenderer == nullptr) {
			return;
		}

		// Compute bounds for this clip
		Foundation::Vec4 bounds = ComputeClipBounds(settings);

		// Intersect with current clip (nested clipping)
		if (!g_clipStack.empty()) {
			bounds = IntersectClipBounds(g_clipStack.top().bounds, bounds);
		}

		// Push to stack
		g_clipStack.push({settings, bounds});

		// Update batch renderer with new clip bounds
		g_batchRenderer->setClipBounds(bounds);
	}

	void popClip() {
		if (g_clipStack.empty()) {
			return;
		}

		g_clipStack.pop();

		// Restore parent clip or clear if stack is empty
		if (g_batchRenderer != nullptr) {
			if (g_clipStack.empty()) {
				g_batchRenderer->clearClipBounds();
			} else {
				g_batchRenderer->setClipBounds(g_clipStack.top().bounds);
			}
		}
	}

	Foundation::Vec4 getCurrentClipBounds() {
		if (g_clipStack.empty()) {
			return Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
		}
		return g_clipStack.top().bounds;
	}

	bool IsClipActive() {
		if (g_clipStack.empty()) {
			return false;
		}
		const auto& bounds = g_clipStack.top().bounds;
		// Clip is active if bounds form a valid rectangle (maxX > minX, maxY > minY)
		return (bounds.z > bounds.x) && (bounds.w > bounds.y);
	}

	// --- Convenience Functions for Future Clip Shapes ---
	// These delegate to pushClip() with the appropriate ClipShape variant.
	// Currently use bounding-box approximation; Phase 3 will add stencil-buffer support.

	void pushClipRoundedRect(const Foundation::Rect& bounds, float cornerRadius) {
		Foundation::ClipSettings settings;
		settings.shape = Foundation::ClipRoundedRect{
			.bounds = bounds,
			.cornerRadius = cornerRadius
		};
		pushClip(settings);
	}

	void pushClipCircle(const Foundation::Vec2& center, float radius) {
		Foundation::ClipSettings settings;
		settings.shape = Foundation::ClipCircle{
			.center = center,
			.radius = radius
		};
		pushClip(settings);
	}

	void pushClipPath(const std::vector<Foundation::Vec2>& vertices) {
		Foundation::ClipSettings settings;
		settings.shape = Foundation::ClipPath{.vertices = vertices};
		pushClip(settings);
	}

	// --- Scissor Stack (Legacy) ---

	void PushScissor(const Foundation::Rect& clipRect) {
		// Intersect with current scissor (nested clipping)
		if (!g_scissorStack.empty()) {
			g_currentScissor = Foundation::Rect::intersection(g_currentScissor, clipRect);
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

	Foundation::Rect getCurrentScissor() {
		return g_currentScissor;
	}

	// --- Transform Stack ---

	void PushTransform(const Foundation::Mat4& transform) {
		g_transformStack.push(g_currentTransform);
		g_currentTransform = g_currentTransform * transform;

		// Apply to batch renderer (will be baked into vertex positions at add-time)
		if (g_batchRenderer != nullptr) {
			g_batchRenderer->setTransform(g_currentTransform);
		}
	}

	void PopTransform() {
		if (!g_transformStack.empty()) {
			g_currentTransform = g_transformStack.top();
			g_transformStack.pop();

			// Apply to batch renderer (will be baked into vertex positions at add-time)
			if (g_batchRenderer != nullptr) {
				g_batchRenderer->setTransform(g_currentTransform);
			}
		}
	}

	Foundation::Mat4 getCurrentTransform() {
		return g_currentTransform;
	}

	// --- Statistics ---

	RenderStats getStats() {
		RenderStats stats = {};

		if (g_batchRenderer != nullptr) {
			auto batchStats = g_batchRenderer->getStats();
			stats.drawCalls = batchStats.drawCalls;
			stats.vertexCount = batchStats.vertexCount;
			stats.triangleCount = batchStats.triangleCount;
		}

		return stats;
	}

} // namespace Renderer::Primitives
