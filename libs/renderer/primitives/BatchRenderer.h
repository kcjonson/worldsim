#pragma once

// Uber Batch Renderer - Unified rendering for shapes and text.
//
// This is the internal batching implementation used by the Primitives API.
// It accumulates draw commands for both shapes (SDF) and text (MSDF) and
// renders them in a single pass with correct z-ordering.

#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "shader/Shader.h"
#include <GL/glew.h>
#include <optional>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	// Forward declaration
	class CoordinateSystem;

	// Unified vertex format for shapes and text (uber shader)
	// Layout matches uber.vert attributes:
	//   location 0: a_position (vec2)
	//   location 1: a_texCoord (vec2) - UV for text, rectLocalPos for shapes
	//   location 2: a_color (vec4)
	//   location 3: a_data1 (vec4) - borderData for shapes, unused for text
	//   location 4: a_data2 (vec4) - shapeParams for shapes, (pixelRange, 0, 0, -1) for text
	//   location 5: a_clipBounds (vec4) - (minX, minY, maxX, maxY) or (0,0,0,0) for no clip
	struct UberVertex {
		Foundation::Vec2 position;	 // Screen-space position
		Foundation::Vec2 texCoord;	 // UV for text, rectLocalPos for shapes
		Foundation::Vec4 color;		 // Fill color RGBA
		Foundation::Vec4 data1;		 // Border: (color.rgb, width) for shapes, unused for text
		Foundation::Vec4 data2;		 // Shape: (halfW, halfH, cornerRadius, borderPos), Text: (pixelRange, 0, 0, -1)
		Foundation::Vec4 clipBounds; // Clip rect (minX, minY, maxX, maxY), or (0,0,0,0) for no clipping
	};

	// Render mode constants for data2.w
	constexpr float kRenderModeText = -1.0F; // Text rendering (MSDF)
	// Shapes use borderPosition (0, 1, 2) in data2.w

	// Batch accumulator - collects geometry before GPU upload
	class BatchRenderer { // NOLINT(cppcoreguidelines-special-member-functions)
	  public:
		BatchRenderer();
		~BatchRenderer();

		// Initialize OpenGL resources (shaders, VBOs)
		void init();

		// Cleanup OpenGL resources
		void shutdown();

		// --- Shape rendering (SDF) ---

		// Add shape quad to batch (with optional SDF border and corner radius)
		void addQuad(
			const Foundation::Rect&						  bounds,
			const Foundation::Color&					  fillColor,
			const std::optional<Foundation::BorderStyle>& border = std::nullopt,
			float										  cornerRadius = 0.0F
		);

		// Add raw triangles (for circles, polygons, etc.)
		void addTriangles(
			const Foundation::Vec2*	 inputVertices,
			const uint16_t*			 inputIndices,
			size_t					 vertexCount,
			size_t					 indexCount,
			const Foundation::Color& color
		);

		// --- Text rendering (MSDF) ---

		// Add text glyph quad to batch
		// position: screen-space top-left of glyph
		// size: glyph dimensions in screen pixels
		// uvMin/uvMax: texture coordinates in MSDF atlas
		// color: text color with alpha
		void addTextQuad(
			const Foundation::Vec2&	 position,
			const Foundation::Vec2&	 size,
			const Foundation::Vec2&	 uvMin,
			const Foundation::Vec2&	 uvMax,
			const Foundation::Color& color
		);

		// Set the MSDF font atlas texture (call once per font)
		void setFontAtlas(GLuint atlasTexture, float pixelRange = 4.0F);

		// --- Rendering ---

		// Flush accumulated geometry to GPU and render
		void flush();

		// Frame lifecycle
		void beginFrame();
		void endFrame();

		// Set viewport dimensions for projection matrix
		void setViewport(int width, int height);

		// Get current viewport dimensions
		void getViewport(int& width, int& height) const;

		// Set coordinate system (for DPI-aware projection matrices)
		void setCoordinateSystem(CoordinateSystem* coordSystem);

		// --- Clipping ---

		// Set current clip bounds (applied to all subsequent vertices)
		// bounds: (minX, minY, maxX, maxY) in screen coordinates
		void setClipBounds(const Foundation::Vec4& bounds);

		// Clear clip bounds (disables clipping)
		void clearClipBounds();

		// Get current clip bounds
		const Foundation::Vec4& getClipBounds() const { return currentClipBounds; }

		// Rendering statistics structure
		struct RenderStats {
			uint32_t drawCalls = 0;
			uint32_t vertexCount = 0;
			uint32_t triangleCount = 0;
		};

		// Statistics
		size_t		getVertexCount() const { return vertices.size(); }
		size_t		getDrawCallCount() const { return drawCallCount; }
		RenderStats getStats() const;

		// Shader access for batching
		GLuint getShaderProgram() const { return shader.getProgram(); }

	  private:
		// Vertex data (CPU-side accumulation)
		std::vector<UberVertex>	 vertices;
		std::vector<uint32_t>	 indices;

		// OpenGL resources
		GLuint vao = 0;
		GLuint vbo = 0;
		GLuint ibo = 0;
		Shader shader;

		// Uniform locations
		GLint projectionLoc = -1;
		GLint transformLoc = -1;
		GLint atlasLoc = -1;
		GLint viewportHeightLoc = -1;
		GLint pixelRatioLoc = -1;

		// Viewport dimensions
		int viewportWidth = 800;
		int viewportHeight = 600;

		// Coordinate system (optional, for DPI-aware rendering)
		CoordinateSystem* coordinateSystem = nullptr;

		// Font atlas for text rendering
		GLuint fontAtlas = 0;
		float  fontPixelRange = 4.0F;

		// Current clip bounds (applied to all vertices)
		// (0,0,0,0) means no clipping
		Foundation::Vec4 currentClipBounds{0.0F, 0.0F, 0.0F, 0.0F};

		// Statistics
		size_t drawCallCount = 0;
	};

} // namespace Renderer
