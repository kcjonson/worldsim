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

		// Get current viewport dimensions
		void GetViewport(int& width, int& height);

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
			const Foundation::Vec2* vertices;	  // Vertex positions (array)
			const uint16_t*			indices;	  // Triangle indices (array)
			size_t					vertexCount;  // Number of vertices
			size_t					indexCount;	  // Number of indices (triangles * 3)
			Foundation::Color		color;		  // Fill color
			const char*				id = nullptr; // Optional: for inspection/debugging
			int						zIndex = 0;	  // Optional: explicit draw order
		};

		// Draw a rectangle with optional fill and border
		void DrawRect(const RectArgs& args);

		// Draw a line
		void DrawLine(const LineArgs& args);

		// Draw triangles from a mesh (for vector graphics tessellation)
		void DrawTriangles(const TrianglesArgs& args);

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
			uint32_t drawCalls;
			uint32_t vertexCount;
			uint32_t triangleCount;
		};

		RenderStats GetStats();

	} // namespace Primitives
} // namespace Renderer
