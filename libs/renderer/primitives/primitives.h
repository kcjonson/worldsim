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
// Uses C++20 designated initializers (.Args{} pattern) for clean, readable code.

#include "graphics/color.h"
#include "graphics/primitive_styles.h"
#include "graphics/rect.h"
#include "math/types.h"
#include <string>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// Forward declarations
	class Renderer;
	class CoordinateSystem;

} // namespace Renderer

// Forward declaration from ui namespace
namespace ui {
	class FontRenderer;
	class TextBatchRenderer;
}

namespace Renderer {
	namespace Primitives { // NOLINT(readability-identifier-naming)

		// --- Initialization ---

		void Init(Renderer* renderer);
		void Shutdown();

		// Set the coordinate system (must be called after Init)
		void SetCoordinateSystem(CoordinateSystem* coordSystem);

		// Set the font renderer for text rendering.
		//
		// This function stores a FontRenderer instance that Text shapes can retrieve
		// and use for rendering. This dependency injection pattern avoids circular
		// dependencies between the renderer and ui libraries.
		//
		// Should be called during application initialization if text rendering is needed.
		// Pass nullptr to disable text rendering.
		//
		// Example:
		//   auto fontRenderer = std::make_unique<ui::FontRenderer>();
		//   fontRenderer->Initialize();
		//   Renderer::Primitives::SetFontRenderer(fontRenderer.get());
		void SetFontRenderer(ui::FontRenderer* fontRenderer);

		// Get the current font renderer instance.
		//
		// Returns the FontRenderer previously set via SetFontRenderer(), or nullptr
		// if no font renderer has been configured. Used internally by Text shapes
		// to access font rendering capabilities.
		//
		// Returns: Pointer to FontRenderer, or nullptr if not set
		ui::FontRenderer* GetFontRenderer();

		// Set the text batch renderer for batched SDF text rendering.
		//
		// The TextBatchRenderer collects all text draw calls and renders them
		// in sorted z-order after all other rendering is complete.
		//
		// Example:
		//   auto textBatchRenderer = std::make_unique<ui::TextBatchRenderer>();
		//   textBatchRenderer->Initialize(&fontRenderer);
		//   Renderer::Primitives::SetTextBatchRenderer(textBatchRenderer.get());
		void SetTextBatchRenderer(ui::TextBatchRenderer* batchRenderer);

		// Get the current text batch renderer instance.
		//
		// Returns: Pointer to TextBatchRenderer, or nullptr if not set
		ui::TextBatchRenderer* GetTextBatchRenderer();

		// Set a callback to flush text rendering at end of frame.
		//
		// This allows the ui library to register TextBatchRenderer::Flush() without
		// creating a circular dependency (renderer → ui → renderer).
		//
		// The callback will be invoked by EndFrame() after flushing shape batches.
		using FlushCallback = void (*)();
		void SetTextFlushCallback(FlushCallback callback);

		// --- Frame Lifecycle ---

		void BeginFrame();
		void EndFrame(); // Flushes all batches

		// Set viewport dimensions for projection matrix
		void SetViewport(int width, int height);

		// Get current viewport dimensions
		void GetViewport(int& width, int& height);

		// --- Coordinate System Helpers ---

		// Get screen-space projection matrix (requires SetCoordinateSystem)
		Foundation::Mat4 GetScreenSpaceProjection();

		// Get world-space projection matrix (requires SetCoordinateSystem)
		Foundation::Mat4 GetWorldSpaceProjection();

		// Percentage-based layout helpers (requires SetCoordinateSystem)
		float			 PercentWidth(float percent);
		float			 PercentHeight(float percent);
		Foundation::Vec2 PercentSize(float widthPercent, float heightPercent);
		Foundation::Vec2 PercentPosition(float xPercent, float yPercent);

		// --- Drawing Functions ---

		// Arguments for DrawRect
		struct RectArgs {
			Foundation::Rect	  bounds;
			Foundation::RectStyle style;
			const char*			  id = nullptr; // Optional: for inspection/debugging
			int					  zIndex = 0;	// Optional: explicit draw order
		};

		// Arguments for DrawLine
		struct LineArgs {
			Foundation::Vec2	  start;
			Foundation::Vec2	  end;
			Foundation::LineStyle style;
			const char*			  id = nullptr;
			int					  zIndex = 0;
		};

		// Arguments for DrawTriangles
		struct TrianglesArgs {
			const Foundation::Vec2* vertices = nullptr; // Vertex positions (array)
			const uint16_t*			indices = nullptr;	// Triangle indices (array)
			size_t					vertexCount = 0;	// Number of vertices
			size_t					indexCount = 0;		// Number of indices (triangles * 3)
			Foundation::Color		color;				// Fill color
			const char*				id = nullptr;		// Optional: for inspection/debugging
			int						zIndex = 0;			// Optional: explicit draw order
		};

		// Draw a rectangle with optional fill and border
		void DrawRect(const RectArgs& args);

		// Draw a line
		void DrawLine(const LineArgs& args);

		// Draw triangles from a mesh (for vector graphics tessellation)
		void DrawTriangles(const TrianglesArgs& args);

		// Arguments for DrawCircle
		struct CircleArgs {
			Foundation::Vec2		center;
			float					radius;
			Foundation::CircleStyle style;
			const char*				id = nullptr;
			int						zIndex = 0;
		};

		// Draw a circle with optional fill and border.
		//
		// Circles are tessellated into a 64-segment triangle fan on the CPU,
		// providing smooth appearance without requiring special shaders.
		// Borders are rendered as connected line segments.
		//
		// Parameters:
		//   - center: Circle center position in current coordinate space
		//   - radius: Circle radius in pixels
		//   - style.fill: Fill color (set alpha to 0 to disable fill)
		//   - style.border: Optional border (color and width)
		//   - id: Optional debug identifier
		//   - zIndex: Draw order (higher values drawn later)
		void DrawCircle(const CircleArgs& args);

		// Arguments for DrawText
		struct TextArgs {
			std::string			 text;
			Foundation::Vec2	 position; // Top-left position
			float				 scale = 1.0F; // Text scale (1.0F = 16px base size)
			Foundation::Color	 color = Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F); // RGBA
			const char*			 id = nullptr;
			float				 zIndex = 0.0F;
		};

		// Draw text using the font renderer.
		//
		// IMPORTANT: Requires SetFontRenderer() to be called during initialization.
		// Text rendering uses batched command queue for proper z-ordering with shapes.
		//
		// Parameters:
		//   - text: String to render
		//   - position: Top-left position in current coordinate space
		//   - scale: Text size scale (1.0F = 16px base font size)
		//   - color: RGBA text color
		//   - zIndex: Draw order (higher values drawn later, allows proper layering with shapes)
		void DrawText(const TextArgs& args);

		// --- State Management ---

		// Scissor/clipping (for scrollable containers)
		void			 PushScissor(const Foundation::Rect& clipRect);
		void			 PopScissor();
		Foundation::Rect GetCurrentScissor();

		// Transform stack (for world-space rendering)
		void			 PushTransform(const Foundation::Mat4& transform);
		void			 PopTransform();
		Foundation::Mat4 GetCurrentTransform();

		// --- Statistics ---

		struct RenderStats {
			uint32_t drawCalls = 0;
			uint32_t vertexCount = 0;
			uint32_t triangleCount = 0;
		};

		RenderStats GetStats();

	} // namespace Primitives
} // namespace Renderer
