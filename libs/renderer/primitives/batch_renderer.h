#pragma once

// Batch Renderer - Accumulates geometry and minimizes draw calls.
//
// This is the internal batching implementation used by the Primitives API.
// It accumulates draw commands and flushes them to the GPU in optimized batches.

#include "graphics/color.h"
#include "graphics/rect.h"
#include "math/types.h"
#include <GL/glew.h>
#include <vector>

namespace Renderer {

	// Forward declaration
	class CoordinateSystem;

	// Vertex format for 2D primitives
	struct PrimitiveVertex {
		Foundation::Vec2 m_position;
		Foundation::Vec2 m_texCoord;
		Foundation::Vec4 m_color;
	};

	// Batch accumulator - collects geometry before GPU upload
	class BatchRenderer {
	  public:
		BatchRenderer();
		~BatchRenderer();

		// Initialize OpenGL resources (shaders, VBOs)
		void Init();

		// Cleanup OpenGL resources
		void Shutdown();

		// Add geometry to current batch
		void AddQuad(const Foundation::Rect& bounds, const Foundation::Color& color);
		void AddTriangles(
			const Foundation::Vec2*	 vertices,
			const uint16_t*			 indices,
			size_t					 vertexCount,
			size_t					 indexCount,
			const Foundation::Color& color
		);

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
			uint32_t m_drawCalls = 0;
			uint32_t m_vertexCount = 0;
			uint32_t m_triangleCount = 0;
		};

		// Statistics
		size_t		GetVertexCount() const { return m_vertices.size(); }
		size_t		GetDrawCallCount() const { return m_drawCallCount; }
		RenderStats GetStats() const;

	  private:
		// Vertex data (CPU-side accumulation)
		std::vector<PrimitiveVertex> m_vertices;
		std::vector<uint32_t>		 m_indices;

		// OpenGL resources
		GLuint m_vao = 0;
		GLuint m_vbo = 0;
		GLuint m_ibo = 0;
		GLuint m_shader = 0;

		// Uniform locations
		GLint m_projectionLoc = -1;
		GLint m_transformLoc = -1;

		// Viewport dimensions
		int m_viewportWidth = 800;
		int m_viewportHeight = 600;

		// Coordinate system (optional, for DPI-aware rendering)
		CoordinateSystem* m_coordinateSystem = nullptr;

		// Statistics
		size_t m_drawCallCount = 0;

		// Helper: Compile shader program
		GLuint CompileShader();
	};

} // namespace Renderer
