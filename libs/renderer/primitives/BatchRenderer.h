#pragma once

// Uber Batch Renderer - Unified rendering for shapes and text.
//
// This is the internal batching implementation used by the Primitives API.
// It accumulates draw commands for both shapes (SDF) and text (MSDF) and
// renders them in a single pass with correct z-ordering.
//
// Also provides GPU instancing for efficient rendering of many identical meshes.

#include "gl/GLBuffer.h"
#include "gl/GLVertexArray.h"
#include "graphics/Color.h"
#include "graphics/PrimitiveStyles.h"
#include "graphics/Rect.h"
#include "math/Types.h"
#include "primitives/InstanceData.h"
#include "shader/Shader.h"
#include "vector/Tessellator.h"
#include <GL/glew.h>
#include <cstdint>
#include <optional>
#include <vector>
#include <glm/vec4.hpp>

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
	//   location 8: a_data3 (vec4) - diagonal neighbors for tiles (NW, NE, SE, SW)
	struct UberVertex {
		Foundation::Vec2 position;	 // Screen-space position
		Foundation::Vec2 texCoord;	 // UV for text, rectLocalPos for shapes
		Foundation::Vec4 color;		 // Fill color RGBA
		Foundation::Vec4 data1;		 // Border: (color.rgb, width) for shapes, unused for text
		Foundation::Vec4 data2;		 // Shape: (halfW, halfH, cornerRadius, borderPos), Text: (pixelRange, 0, 0, -1)
		Foundation::Vec4 clipBounds; // Clip rect (minX, minY, maxX, maxY), or (0,0,0,0) for no clipping
		Foundation::Vec4 data3;		 // Diagonal neighbors for tiles (NW, NE, SE, SW), unused for shapes/text
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
		// If inputColors is provided, uses per-vertex colors; otherwise uses uniform color
		void addTriangles(
			const Foundation::Vec2*	  inputVertices,
			const uint16_t*			  inputIndices,
			size_t					  vertexCount,
			size_t					  indexCount,
			const Foundation::Color&  color,
			const Foundation::Color*  inputColors = nullptr
		);

		// Add a tile quad with adjacency-packed data
		void addTileQuad(
			const Foundation::Rect&  bounds,
			const Foundation::Color& color,
			uint8_t				 edgeMask,
			uint8_t				 cornerMask,
			uint8_t				 surfaceId,
			uint8_t				 hardEdgeMask,
			int32_t				 tileX,
			int32_t				 tileY,
			uint8_t				 neighborN,
			uint8_t				 neighborE,
			uint8_t				 neighborS,
			uint8_t				 neighborW,
			uint8_t				 neighborNW,
			uint8_t				 neighborNE,
			uint8_t				 neighborSE,
			uint8_t				 neighborSW
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

		// Set tile atlas texture and rects (uvMin.xy, uvMax.xy per surface id).
		void setTileAtlas(GLuint atlasTexture, const std::vector<glm::vec4>& rects);

		// --- Rendering ---

		// Flush accumulated geometry to GPU and render
		void flush();

		// Frame lifecycle
		void beginFrame();
		void endFrame();

		// Set viewport dimensions for projection matrix.
		// IMPORTANT: Must be called before drawInstanced() to ensure correct projection.
		void setViewport(int width, int height);

		// Get current viewport dimensions
		void getViewport(int& width, int& height) const;

		// Set coordinate system (for DPI-aware projection matrices)
		void setCoordinateSystem(CoordinateSystem* coordSystem);

		// Get current coordinate system (may be nullptr)
		[[nodiscard]] CoordinateSystem* getCoordinateSystem() const { return coordinateSystem; }

		// --- Clipping ---

		// Set current clip bounds (applied to all subsequent vertices)
		// bounds: (minX, minY, maxX, maxY) in screen coordinates
		void setClipBounds(const Foundation::Vec4& bounds);

		// Clear clip bounds (disables clipping)
		void clearClipBounds();

		// Get current clip bounds
		const Foundation::Vec4& getClipBounds() const { return currentClipBounds; }

		// --- Transform ---

		// Set the current transform matrix.
		// The transform is baked into vertex positions when vertices are added (not at flush time).
		// This allows different transforms for different parts of the scene within a single batch.
		// Used for content offset (scrolling) in containers.
		void setTransform(const Foundation::Mat4& transform);

		// Get current transform matrix
		const Foundation::Mat4& getTransform() const { return currentTransform; }

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

		// --- GPU Instancing (World-Space) ---

		/// Upload a tessellated mesh to GPU for instanced rendering.
		/// The mesh is uploaded once and reused for all instances.
		/// @param mesh Tessellated mesh to upload
		/// @param maxInstances Maximum number of instances to support (default 10000)
		/// @return Handle for subsequent draw calls
		InstancedMeshHandle uploadInstancedMesh(
			const renderer::TessellatedMesh& mesh,
			uint32_t maxInstances = 10000
		);

		/// Release GPU resources for an instanced mesh.
		/// @param handle Handle to release (will be invalidated)
		void releaseInstancedMesh(InstancedMeshHandle& handle);

		/// Draw multiple instances of a mesh with GPU instancing.
		/// Transforms are computed on GPU using camera uniforms.
		/// @param handle Mesh handle from uploadInstancedMesh
		/// @param instances Array of per-instance data (world position, rotation, scale, color)
		/// @param count Number of instances to draw
		/// @param cameraPosition Camera world position (center of view)
		/// @param cameraZoom Camera zoom level (1.0 = normal)
		/// @param pixelsPerMeter World scale factor
		void drawInstanced(
			const InstancedMeshHandle& handle,
			const InstanceData* instances,
			uint32_t count,
			Foundation::Vec2 cameraPosition,
			float cameraZoom,
			float pixelsPerMeter
		);

	  private:
		// Vertex data (CPU-side accumulation)
		std::vector<UberVertex>	 vertices;
		std::vector<uint32_t>	 indices;

		// OpenGL resources (RAII wrappers for automatic cleanup)
		GLVertexArray vao;
		GLBuffer vbo;
		GLBuffer ibo;
		Shader shader;

		// Uniform locations (standard batched rendering)
		GLint projectionLoc = -1;
		GLint transformLoc = -1;
		GLint atlasLoc = -1;
		GLint viewportHeightLoc = -1;
		GLint pixelRatioLoc = -1;
		GLint tileAtlasLoc = -1;
		GLint tileAtlasRectsLoc = -1;
		GLint tileAtlasCountLoc = -1;

		// Uniform locations (instanced rendering)
		GLint cameraPositionLoc = -1;
		GLint cameraZoomLoc = -1;
		GLint pixelsPerMeterLoc = -1;
		GLint viewportSizeLoc = -1;
		GLint instancedLoc = -1;

		// Viewport dimensions
		int viewportWidth = 800;
		int viewportHeight = 600;

		// Coordinate system (optional, for DPI-aware rendering)
		CoordinateSystem* coordinateSystem = nullptr;

		// Font atlas for text rendering
		GLuint fontAtlas = 0;
		float  fontPixelRange = 4.0F;

		// Tile atlas for ground textures
		GLuint tileAtlas = 0;
		std::vector<glm::vec4> tileAtlasRects;

		// Current clip bounds (applied to all vertices)
		// (0,0,0,0) means no clipping
		Foundation::Vec4 currentClipBounds{0.0F, 0.0F, 0.0F, 0.0F};

		// Current transform matrix (baked into vertex positions at add-time)
		Foundation::Mat4 currentTransform{1.0F}; // Identity
		bool			 transformIsIdentity = true; // Cached to avoid per-vertex checks

		// Statistics
		size_t drawCallCount = 0;
	size_t frameVertexCount = 0;	// Cumulative vertex count for the frame
	size_t frameTriangleCount = 0;	// Cumulative triangle count for the frame
	};

} // namespace Renderer
