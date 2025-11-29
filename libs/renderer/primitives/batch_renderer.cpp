// Uber Batch Renderer implementation.
// Accumulates 2D geometry (shapes + text) and renders in optimized batches.

#include "primitives/batch_renderer.h"
#include "coordinate_system/coordinate_system.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace Renderer {

	BatchRenderer::BatchRenderer() { // NOLINT(cppcoreguidelines-pro-type-member-init,modernize-use-equals-default)
		// Reserve space for vertices to minimize allocations
		m_vertices.reserve(10000);
		m_indices.reserve(15000);
	}

	BatchRenderer::~BatchRenderer() {
		Shutdown();
	}

	void BatchRenderer::Init() {
		// Load uber shader (unified shapes + text)
		if (!m_shader.LoadFromFile("uber.vert", "uber.frag")) {
			std::cerr << "Failed to load uber shaders!" << std::endl;
			return;
		}

		// Get uniform locations
		m_projectionLoc = glGetUniformLocation(m_shader.GetProgram(), "u_projection");
		m_transformLoc = glGetUniformLocation(m_shader.GetProgram(), "u_transform");
		m_atlasLoc = glGetUniformLocation(m_shader.GetProgram(), "u_atlas");
		m_viewportHeightLoc = glGetUniformLocation(m_shader.GetProgram(), "u_viewportHeight");
		m_pixelRatioLoc = glGetUniformLocation(m_shader.GetProgram(), "u_pixelRatio");

		// Create VAO/VBO/IBO
		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glGenBuffers(1, &m_ibo);

		glBindVertexArray(m_vao);

		// Set up vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

		// Position attribute (location = 0)
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, position));

		// TexCoord attribute (location = 1) - UV for text, rectLocalPos for shapes
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, texCoord));

		// Color attribute (location = 2)
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, color));

		// Data1 attribute (location = 3) - borderData for shapes
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, data1));

		// Data2 attribute (location = 4) - shapeParams for shapes, (pixelRange, 0, 0, -1) for text
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, data2));

		// ClipBounds attribute (location = 5) - (minX, minY, maxX, maxY) for clipping
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(UberVertex), (void*)offsetof(UberVertex, clipBounds));

		// Bind index buffer
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);

		glBindVertexArray(0);
	}

	void BatchRenderer::Shutdown() {
		if (m_vao != 0) {
			glDeleteVertexArrays(1, &m_vao);
			m_vao = 0;
		}

		if (m_vbo != 0) {
			glDeleteBuffers(1, &m_vbo);
			m_vbo = 0;
		}

		if (m_ibo != 0) {
			glDeleteBuffers(1, &m_ibo);
			m_ibo = 0;
		}

		// Shader cleanup handled by RAII destructor
	}

	void BatchRenderer::AddQuad( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Rect&						bounds,
		const Foundation::Color&					fillColor,
		const std::optional<Foundation::BorderStyle>& border,
		float										cornerRadius
	) { // NOLINT(readability-convert-member-functions-to-static)
		uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());

		// Calculate rect center and half-dimensions for SDF
		float halfW = bounds.width * 0.5F;
		float halfH = bounds.height * 0.5F;

		// Fill color
		Foundation::Vec4 colorVec = fillColor.ToVec4();

		// Pack border data (color RGB + width)
		Foundation::Vec4 borderData(0.0F, 0.0F, 0.0F, 0.0F);
		float borderWidth = 0.0F;
		float borderPosEnum = 1.0F; // Default to Center
		if (border.has_value()) {
			borderData = Foundation::Vec4(border->color.r, border->color.g, border->color.b, border->width);
			borderWidth = border->width;
			// Use corner radius from border if provided
			if (border->cornerRadius > 0.0F) {
				cornerRadius = border->cornerRadius;
			}
			switch (border->position) {
				case Foundation::BorderPosition::Inside:
					borderPosEnum = 0.0F;
					break;
				case Foundation::BorderPosition::Center:
					borderPosEnum = 1.0F;
					break;
				case Foundation::BorderPosition::Outside:
					borderPosEnum = 2.0F;
					break;
			}
		}

		// Calculate how much the border extends outside the shape bounds
		// Inside (0): border entirely inside, no expansion needed
		// Center (1): border straddles edge, half extends outside
		// Outside (2): border entirely outside, full width extends outside
		float borderOuterExtent = 0.0F;
		if (borderPosEnum == 1.0F) {
			borderOuterExtent = borderWidth * 0.5F; // Center: half outside
		} else if (borderPosEnum == 2.0F) {
			borderOuterExtent = borderWidth; // Outside: full width outside
		}

		// Expand the quad to cover the border that extends outside the shape
		float expandedHalfW = halfW + borderOuterExtent;
		float expandedHalfH = halfH + borderOuterExtent;

		// Calculate expanded screen-space bounds
		float centerX = bounds.x + halfW;
		float centerY = bounds.y + halfH;

		// Pack shape parameters (halfWidth, halfHeight, cornerRadius, borderPosition)
		// Note: shapeParams still uses the ORIGINAL halfW/halfH for SDF calculation
		Foundation::Vec4 shapeParams(halfW, halfH, cornerRadius, borderPosEnum);

		// Add 4 vertices with expanded screen positions but rect-local coordinates
		// that extend beyond the original shape bounds
		// Top-left corner
		m_vertices.push_back(
			{Foundation::Vec2(centerX - expandedHalfW, centerY - expandedHalfH),
			 Foundation::Vec2(-expandedHalfW, -expandedHalfH), // Rect-local: top-left (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 m_currentClipBounds}
		);

		// Top-right corner
		m_vertices.push_back(
			{Foundation::Vec2(centerX + expandedHalfW, centerY - expandedHalfH),
			 Foundation::Vec2(expandedHalfW, -expandedHalfH), // Rect-local: top-right (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 m_currentClipBounds}
		);

		// Bottom-right corner
		m_vertices.push_back(
			{Foundation::Vec2(centerX + expandedHalfW, centerY + expandedHalfH),
			 Foundation::Vec2(expandedHalfW, expandedHalfH), // Rect-local: bottom-right (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 m_currentClipBounds}
		);

		// Bottom-left corner
		m_vertices.push_back(
			{Foundation::Vec2(centerX - expandedHalfW, centerY + expandedHalfH),
			 Foundation::Vec2(-expandedHalfW, expandedHalfH), // Rect-local: bottom-left (expanded)
			 colorVec,
			 borderData,
			 shapeParams,
			 m_currentClipBounds}
		);

		// Add 6 indices (2 triangles)
		m_indices.push_back(baseIndex + 0);
		m_indices.push_back(baseIndex + 1);
		m_indices.push_back(baseIndex + 2);

		m_indices.push_back(baseIndex + 0);
		m_indices.push_back(baseIndex + 2);
		m_indices.push_back(baseIndex + 3);
	}

	void BatchRenderer::AddTriangles( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Vec2*	 vertices,
		const uint16_t*			 indices,
		size_t					 vertexCount,
		size_t					 indexCount,
		const Foundation::Color& color
	) { // NOLINT(readability-convert-member-functions-to-static)
		uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());

		Foundation::Vec4 colorVec = color.ToVec4();

		// Default data (not used for tessellated shapes, but required for vertex format)
		// Use borderPosition=1 (Center) so shader treats these as shapes, not text
		Foundation::Vec2 zeroVec2(0.0F, 0.0F);
		Foundation::Vec4 zeroVec4(0.0F, 0.0F, 0.0F, 0.0F);
		Foundation::Vec4 shapeParams(0.0F, 0.0F, 0.0F, 1.0F); // borderPos=1 marks as shape

		// Add all vertices
		for (size_t i = 0; i < vertexCount; ++i) {
			m_vertices.push_back({
				vertices[i],
				zeroVec2,			  // texCoord (unused for triangles)
				colorVec,
				zeroVec4,			  // data1 (unused)
				shapeParams,		  // data2 with borderPos >= 0 marks as shape
				m_currentClipBounds	  // clip bounds
			});
		}

		// Add all indices (offset by baseIndex)
		for (size_t i = 0; i < indexCount; ++i) {
			m_indices.push_back(baseIndex + indices[i]);
		}
	}

	void BatchRenderer::AddTextQuad(
		const Foundation::Vec2&	 position,
		const Foundation::Vec2&	 size,
		const Foundation::Vec2&	 uvMin,
		const Foundation::Vec2&	 uvMax,
		const Foundation::Color& color
	) {
		uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());

		Foundation::Vec4 colorVec = color.ToVec4();

		// Text data packing:
		// data1 = unused (0,0,0,0)
		// data2 = (pixelRange, 0, 0, -1) where -1 signals text rendering mode
		Foundation::Vec4 zeroVec4(0.0F, 0.0F, 0.0F, 0.0F);
		Foundation::Vec4 textParams(m_fontPixelRange, 0.0F, 0.0F, kRenderModeText);

		// Add 4 vertices for glyph quad
		// Note: UV Y coordinates are flipped for OpenGL coordinate system

		// Top-left
		m_vertices.push_back({
			position,
			Foundation::Vec2(uvMin.x, uvMax.y), // UV flipped
			colorVec,
			zeroVec4,
			textParams,
			m_currentClipBounds
		});

		// Top-right
		m_vertices.push_back({
			Foundation::Vec2(position.x + size.x, position.y),
			Foundation::Vec2(uvMax.x, uvMax.y), // UV flipped
			colorVec,
			zeroVec4,
			textParams,
			m_currentClipBounds
		});

		// Bottom-right
		m_vertices.push_back({
			Foundation::Vec2(position.x + size.x, position.y + size.y),
			Foundation::Vec2(uvMax.x, uvMin.y), // UV flipped
			colorVec,
			zeroVec4,
			textParams,
			m_currentClipBounds
		});

		// Bottom-left
		m_vertices.push_back({
			Foundation::Vec2(position.x, position.y + size.y),
			Foundation::Vec2(uvMin.x, uvMin.y), // UV flipped
			colorVec,
			zeroVec4,
			textParams,
			m_currentClipBounds
		});

		// Add 6 indices (2 triangles)
		m_indices.push_back(baseIndex + 0);
		m_indices.push_back(baseIndex + 1);
		m_indices.push_back(baseIndex + 2);

		m_indices.push_back(baseIndex + 0);
		m_indices.push_back(baseIndex + 2);
		m_indices.push_back(baseIndex + 3);
	}

	void BatchRenderer::SetFontAtlas(GLuint atlasTexture, float pixelRange) {
		m_fontAtlas = atlasTexture;
		m_fontPixelRange = pixelRange;
	}

	void BatchRenderer::Flush() {
		if (m_vertices.empty()) {
			return;
		}

		// Enable blending for transparency (shapes and text both need this)
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Disable depth testing for 2D rendering
		glDisable(GL_DEPTH_TEST);

		// Disable face culling (quads may be in either winding order)
		glDisable(GL_CULL_FACE);

		// Upload vertex data to GPU
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(UberVertex), m_vertices.data(), GL_DYNAMIC_DRAW);

		// Upload index data to GPU
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t), m_indices.data(), GL_DYNAMIC_DRAW);

		// Bind shader and VAO
		m_shader.Use();
		glBindVertexArray(m_vao);

		// Create projection matrix
		// If CoordinateSystem is set, use it for DPI-aware projection (logical pixels)
		// Otherwise fall back to viewport dimensions (may be incorrect on high-DPI displays)
		Foundation::Mat4 projection;
		if (m_coordinateSystem != nullptr) {
			projection = m_coordinateSystem->CreateScreenSpaceProjection();
		} else {
			projection = glm::ortho(0.0F, static_cast<float>(m_viewportWidth), static_cast<float>(m_viewportHeight), 0.0F, -1.0F, 1.0F);
		}
		Foundation::Mat4 transform = Foundation::Mat4(1.0F);

		glUniformMatrix4fv(m_projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
		glUniformMatrix4fv(m_transformLoc, 1, GL_FALSE, glm::value_ptr(transform));

		// Set viewport height and pixel ratio for clipping
		// gl_FragCoord uses physical pixels, so we need framebuffer height and pixel ratio
		float framebufferHeight = static_cast<float>(m_viewportHeight);
		float pixelRatio = 1.0F;
		if (m_coordinateSystem != nullptr) {
			// Get pixel ratio for DPI scaling
			pixelRatio = m_coordinateSystem->GetPixelRatio();
			// Get logical height and convert to framebuffer (physical) height
			glm::vec2 windowSize = m_coordinateSystem->GetWindowSize();
			framebufferHeight = windowSize.y * pixelRatio;
		}
		glUniform1f(m_viewportHeightLoc, framebufferHeight);
		glUniform1f(m_pixelRatioLoc, pixelRatio);

		// Bind font atlas texture (always bound, shader ignores it for shapes)
		if (m_fontAtlas != 0) {
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, m_fontAtlas);
			glUniform1i(m_atlasLoc, 0);
		}

		// Draw
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);

		m_drawCallCount++;

		// Cleanup
		glBindVertexArray(0);
		if (m_fontAtlas != 0) {
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		glDisable(GL_BLEND);

		// Clear buffers for next batch
		m_vertices.clear();
		m_indices.clear();
	}

	void BatchRenderer::BeginFrame() {
		m_drawCallCount = 0;
		m_vertices.clear();
		m_indices.clear();
	}

	void BatchRenderer::EndFrame() {
		Flush();
	}

	void BatchRenderer::SetViewport(int width, int height) {
		m_viewportWidth = width;
		m_viewportHeight = height;
	}

	void BatchRenderer::GetViewport(int& width, int& height) const {
		width = m_viewportWidth;
		height = m_viewportHeight;
	}

	// Sets the coordinate system to use for rendering.
	// Note: BatchRenderer does NOT take ownership of the CoordinateSystem pointer.
	// The caller is responsible for ensuring that the CoordinateSystem outlives the BatchRenderer.
	void BatchRenderer::SetCoordinateSystem(CoordinateSystem* coordSystem) {
		m_coordinateSystem = coordSystem;
	}

	BatchRenderer::RenderStats BatchRenderer::GetStats() const { // NOLINT(readability-convert-member-functions-to-static)
		RenderStats stats;
		stats.drawCalls = static_cast<uint32_t>(m_drawCallCount);
		stats.vertexCount = static_cast<uint32_t>(m_vertices.size());
		stats.triangleCount = static_cast<uint32_t>(m_indices.size() / 3);
		return stats;
	}

	void BatchRenderer::SetClipBounds(const Foundation::Vec4& bounds) {
		m_currentClipBounds = bounds;
	}

	void BatchRenderer::ClearClipBounds() {
		m_currentClipBounds = Foundation::Vec4(0.0F, 0.0F, 0.0F, 0.0F);
	}

} // namespace Renderer
