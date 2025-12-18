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

#include "graphics/ClipTypes.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include <cstdint>
#include <string>
#include <vector>
#include <glm/vec4.hpp>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// Forward declarations
	class Renderer;
	class CoordinateSystem;
	class BatchRenderer;

} // namespace Renderer

// Forward declaration from ui namespace
namespace ui {
	class FontRenderer;
}

namespace Renderer {
	namespace Primitives { // NOLINT(readability-identifier-naming)

		// --- Initialization ---

		void init(Renderer* renderer);
		void shutdown();

		// Set the coordinate system (must be called after Init)
		void setCoordinateSystem(CoordinateSystem* coordSystem);

		// Get viewport dimensions in logical pixels.
		//
		// Returns window size in logical (DPI-independent) coordinates, suitable for
		// UI layout and mouse coordinate conversion. On Retina displays, this returns
		// half the framebuffer dimensions; on standard displays, it matches getViewport().
		//
		// Use this instead of getViewport() when you need dimensions that match GLFW's
		// window coordinates (what mouse events report, what UI layout uses).
		void getLogicalViewport(int& width, int& height);

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
		//   Renderer::Primitives::setFontRenderer(fontRenderer.get());
		void setFontRenderer(ui::FontRenderer* fontRenderer);

		// Get the current font renderer instance.
		//
		// Returns the FontRenderer previously set via SetFontRenderer(), or nullptr
		// if no font renderer has been configured. Used internally by Text shapes
		// to access font rendering capabilities.
		//
		// Returns: Pointer to FontRenderer, or nullptr if not set
		ui::FontRenderer* getFontRenderer();

		// Set the font atlas texture for text rendering.
		//
		// This configures the BatchRenderer with the MSDF font atlas texture
		// that will be used for text rendering. Must be called after Init()
		// and before rendering any text.
		//
		// Parameters:
		//   - atlasTexture: OpenGL texture ID of the MSDF font atlas
		//   - pixelRange: Distance field pixel range (default 4.0, from atlas generation)
		void setFontAtlas(unsigned int atlasTexture, float pixelRange = 4.0F);

		// Set tile atlas texture (GL texture) and UV rects per surface index.
		void setTileAtlas(unsigned int atlasTexture, const std::vector<glm::vec4>& rects);

		// Get the internal batch renderer for direct text rendering.
		//
		// Used internally by Text shapes to call AddTextQuad() for batched
		// text rendering with proper z-ordering alongside shapes.
		//
		// Returns: Pointer to BatchRenderer, or nullptr if not initialized
		BatchRenderer* getBatchRenderer();

		// Set a callback to update frame counter for FontRenderer cache LRU tracking.
		//
		// This allows the ui library to register FontRenderer::updateFrame() without
		// creating a circular dependency (renderer → ui → renderer).
		//
		// The callback will be invoked by BeginFrame() before rendering.
		using FrameUpdateCallback = void (*)();
		void setFrameUpdateCallback(FrameUpdateCallback callback);

		// --- Frame Lifecycle ---

		void beginFrame();
		void endFrame(); // Flushes all batches

		// Set viewport dimensions for projection matrix
		void setViewport(int width, int height);

		// Get current viewport dimensions
		void getViewport(int& width, int& height);

		// --- Coordinate System Helpers ---

		// Get screen-space projection matrix (requires SetCoordinateSystem)
		Foundation::Mat4 getScreenSpaceProjection();

		// Get world-space projection matrix (requires SetCoordinateSystem)
		Foundation::Mat4 getWorldSpaceProjection();

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
			const Foundation::Vec2*	 vertices = nullptr;	 // Vertex positions (array)
			const uint16_t*			 indices = nullptr;		 // Triangle indices (array)
			size_t					 vertexCount = 0;		 // Number of vertices
			size_t					 indexCount = 0;		 // Number of indices (triangles * 3)
			Foundation::Color		 color;					 // Fill color (used if colors == nullptr)
			const Foundation::Color* colors = nullptr;		 // Optional: per-vertex colors (array, same size as vertices)
			const char*				 id = nullptr;			 // Optional: for inspection/debugging
			int						 zIndex = 0;			 // Optional: explicit draw order
		};

		// Draw a rectangle with optional fill and border
		void drawRect(const RectArgs& args);

		// Draw a line
		void drawLine(const LineArgs& args);

		// Draw triangles from a mesh (for vector graphics tessellation)
		void drawTriangles(const TrianglesArgs& args);

		// Arguments for DrawTile (tile-specific packing for adjacency data)
		struct TileArgs {
			Foundation::Rect  bounds;         // Screen-space quad
			Foundation::Color color;          // Base color
			uint8_t          edgeMask = 0;    // N,E,S,W bits (0-3)
			uint8_t          cornerMask = 0;  // NW,NE,SE,SW bits (0-3)
			uint8_t          surfaceId = 0;   // Surface type id (0-255)
			uint8_t          hardEdgeMask = 0;// Family-based hard edges (8 dirs)
			int32_t          tileX = 0;       // World tile coordinate X (for procedural edge variation)
			int32_t          tileY = 0;       // World tile coordinate Y (for procedural edge variation)
			// Cardinal neighbor surface IDs for soft edge blending (same-family surfaces)
			uint8_t          neighborN = 0;   // North neighbor surface ID
			uint8_t          neighborE = 0;   // East neighbor surface ID
			uint8_t          neighborS = 0;   // South neighbor surface ID
			uint8_t          neighborW = 0;   // West neighbor surface ID
			// Diagonal neighbor surface IDs for corner blending
			uint8_t          neighborNW = 0;  // Northwest neighbor surface ID
			uint8_t          neighborNE = 0;  // Northeast neighbor surface ID
			uint8_t          neighborSE = 0;  // Southeast neighbor surface ID
			uint8_t          neighborSW = 0;  // Southwest neighbor surface ID
		};

		// Draw a tile quad with adjacency-packed data for shader use
		void drawTile(const TileArgs& args);

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
		void drawCircle(const CircleArgs& args);

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
		void drawText(const TextArgs& args);

		// --- State Management ---

		// === Clipping System (Shader-based, batching-friendly) ===
		//
		// PushClip/PopClip provide per-vertex clip bounds that preserve batching.
		// All vertices added after PushClip() will be clipped to the specified region.
		// Supports nested clipping (intersection of all active clip regions).
		//
		// Fast path: ClipRect with ClipMode::Inside uses shader-based clipping
		// (zero GL state changes, full batching preserved).
		//
		// Slow path: Complex shapes (rounded rect, circle, path) or ClipMode::Outside
		// will use stencil buffer in future phases (stub functions for now).
		//
		// See /docs/technical/ui-framework/clipping.md for design details.

		// Push a new clip region. Nested clips are intersected.
		// For ClipRect with ClipMode::Inside, this uses the fast shader path.
		void pushClip(const Foundation::ClipSettings& settings);

		// Pop the most recent clip region, restoring the parent clip (if any).
		void popClip();

		// Get current clip bounds as Vec4 (minX, minY, maxX, maxY).
		// Returns (0,0,0,0) if no clip is active.
		Foundation::Vec4 getCurrentClipBounds();

		// Check if any clip region is currently active.
		bool IsClipActive();

		// === Convenience Functions for Future Clip Shapes ===
		//
		// These functions provide a simpler API for common clip shapes.
		// Currently they use bounding-box approximation; future phases will
		// implement proper stencil-buffer clipping for accurate shapes.

		// Push a rounded rectangle clip region.
		// NOTE: Currently uses bounding box approximation. Accurate rounded rect
		// clipping requires stencil buffer (Phase 3).
		void pushClipRoundedRect(const Foundation::Rect& bounds, float cornerRadius);

		// Push a circular clip region.
		// NOTE: Currently uses bounding box approximation. Accurate circular
		// clipping requires stencil buffer (Phase 3).
		void pushClipCircle(const Foundation::Vec2& center, float radius);

		// Push an arbitrary path clip region.
		// NOTE: Currently uses bounding box approximation. Accurate path
		// clipping requires stencil buffer (Phase 3).
		void pushClipPath(const std::vector<Foundation::Vec2>& vertices);

		// === ClipMode::Outside Support ===
		//
		// ClipMode::Outside is defined in ClipTypes.h but not yet implemented.
		// When implemented, it will allow "punch hole" effects where content
		// is visible OUTSIDE the clip shape (e.g., spotlight effects).
		// This requires stencil buffer with inverted test (Phase 3).

		// Legacy scissor functions (deprecated - use PushClip/PopClip instead)
		// Scissor/clipping (for scrollable containers)
		void			 PushScissor(const Foundation::Rect& clipRect);
		void			 PopScissor();
		Foundation::Rect getCurrentScissor();

		// Transform stack (for world-space rendering)
		void			 PushTransform(const Foundation::Mat4& transform);
		void			 PopTransform();
		Foundation::Mat4 getCurrentTransform();

		// --- Statistics ---

		struct RenderStats {
			uint32_t drawCalls = 0;
			uint32_t vertexCount = 0;
			uint32_t triangleCount = 0;
		};

		RenderStats getStats();

	} // namespace Primitives
} // namespace Renderer
