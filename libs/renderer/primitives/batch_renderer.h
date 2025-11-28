#pragma once

// Uber Batch Renderer - Unified rendering for shapes and text.
//
// This is the internal batching implementation used by the Primitives API.
// It accumulates draw commands for both shapes (SDF) and text (MSDF) and
// renders them in a single pass with correct z-ordering.

#include "graphics/color.h"
#include "graphics/primitive_styles.h"
#include "graphics/rect.h"
#include "math/types.h"
#include "shader/shader.h"
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
	struct UberVertex {
		Foundation::Vec2 position; // Screen-space position
		Foundation::Vec2 texCoord; // UV for text, rectLocalPos for shapes
		Foundation::Vec4 color;	   // Fill color RGBA
		Foundation::Vec4 data1;	   // Border: (color.rgb, width) for shapes, unused for text
		Foundation::Vec4 data2;	   // Shape: (halfW, halfH, cornerRadius, borderPos), Text: (pixelRange, 0, 0, -1)
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
		void Init();

		// Cleanup OpenGL resources
		void Shutdown();

		// --- Shape rendering (SDF) ---

		// Add shape quad to batch (with optional SDF border and corner radius)
		void AddQuad(
			const Foundation::Rect&						  bounds,
			const Foundation::Color&					  fillColor,
			const std::optional<Foundation::BorderStyle>& border = std::nullopt,
			float										  cornerRadius = 0.0F
		);

		// Add raw triangles (for circles, polygons, etc.)
		void AddTriangles(
			const Foundation::Vec2*	 vertices,
			const uint16_t*			 indices,
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
		void AddTextQuad(
			const Foundation::Vec2&	 position,
			const Foundation::Vec2&	 size,
			const Foundation::Vec2&	 uvMin,
			const Foundation::Vec2&	 uvMax,
			const Foundation::Color& color
		);

		// Set the MSDF font atlas texture (call once per font)
		void SetFontAtlas(GLuint atlasTexture, float pixelRange = 4.0F);

		// --- Rendering ---

		// Flush accumulated geometry to GPU and render
		void Flush();

		// Frame lifecycle
		void BeginFrame();
		void EndFrame();

		// Set viewport dimensions for projection matrix
		void SetViewport(int width, int height);

		// Get current viewport dimensions
		void GetViewport(int& width, int& height) const;

		// Set coordinate system (for DPI-aware projection matrices)
		void SetCoordinateSystem(CoordinateSystem* coordSystem);

		// Rendering statistics structure
		struct RenderStats {
			uint32_t drawCalls = 0;
			uint32_t vertexCount = 0;
			uint32_t triangleCount = 0;
		};

		// Statistics
		size_t		GetVertexCount() const { return m_vertices.size(); }
		size_t		GetDrawCallCount() const { return m_drawCallCount; }
		RenderStats GetStats() const;

		// Shader access for batching
		GLuint GetShaderProgram() const { return m_shader.GetProgram(); }

	  private:
		// Vertex data (CPU-side accumulation)
		std::vector<UberVertex>	 m_vertices;
		std::vector<uint32_t>	 m_indices;

		// OpenGL resources
		GLuint m_vao = 0;
		GLuint m_vbo = 0;
		GLuint m_ibo = 0;
		Shader m_shader;

		// Uniform locations
		GLint m_projectionLoc = -1;
		GLint m_transformLoc = -1;
		GLint m_atlasLoc = -1;

		// Viewport dimensions
		int m_viewportWidth = 800;
		int m_viewportHeight = 600;

		// Coordinate system (optional, for DPI-aware rendering)
		CoordinateSystem* m_coordinateSystem = nullptr;

		// Font atlas for text rendering
		GLuint m_fontAtlas = 0;
		float  m_fontPixelRange = 4.0F;

		// Statistics
		size_t m_drawCallCount = 0;
	};

} // namespace Renderer
