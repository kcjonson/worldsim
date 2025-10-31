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
	class CoordinateSystem;

	namespace Primitives {

		// --- Initialization ---

		void Init(Renderer* renderer);
		void Shutdown();

		// Set the coordinate system (must be called after Init)
		void SetCoordinateSystem(CoordinateSystem* coordSystem);

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
			Foundation::Rect	  m_bounds;
			Foundation::RectStyle m_style;
			const char*			  m_id = nullptr; // Optional: for inspection/debugging
			int					  m_zIndex = 0;	  // Optional: explicit draw order
		};

		// Arguments for DrawLine
		struct LineArgs {
			Foundation::Vec2	  m_start;
			Foundation::Vec2	  m_end;
			Foundation::LineStyle m_style;
			const char*			  m_id = nullptr;
			int					  m_zIndex = 0;
		};

		// Arguments for DrawTriangles
		struct TrianglesArgs {
			const Foundation::Vec2* m_vertices = nullptr;	 // Vertex positions (array)
			const uint16_t*			m_indices = nullptr;	 // Triangle indices (array)
			size_t					m_vertexCount = 0;		 // Number of vertices
			size_t					m_indexCount = 0;		 // Number of indices (triangles * 3)
			Foundation::Color		m_color;				 // Fill color
			const char*				m_id = nullptr;			 // Optional: for inspection/debugging
			int						m_zIndex = 0;			 // Optional: explicit draw order
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
			uint32_t m_drawCalls = 0;
			uint32_t m_vertexCount = 0;
			uint32_t m_triangleCount = 0;
		};

		RenderStats GetStats();

	} // namespace Primitives
} // namespace Renderer
