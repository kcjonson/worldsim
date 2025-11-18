// Batch Renderer implementation.
// Accumulates 2D geometry and renders in optimized batches to minimize draw calls.

#include "primitives/batch_renderer.h"
#include "coordinate_system/coordinate_system.h"
#include "shader/shader_loader.h"
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
		// Compile shader
		m_shader = CompileShader();

		if (m_shader == 0) {
			std::cerr << "Failed to compile primitive shader!" << std::endl;
			return;
		}

		// Get uniform locations
		m_projectionLoc = glGetUniformLocation(m_shader, "u_projection");
		m_transformLoc = glGetUniformLocation(m_shader, "u_transform");

		// Create VAO/VBO/IBO
		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glGenBuffers(1, &m_ibo);

		glBindVertexArray(m_vao);

		// Set up vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

		// Position attribute (location = 0)
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, position));

		// RectLocalPos attribute (location = 1)
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, rectLocalPos));

		// Color attribute (location = 2)
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, color));

		// BorderData attribute (location = 3)
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, borderData));

		// ShapeParams attribute (location = 4)
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, shapeParams));

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

		if (m_shader != 0) {
			glDeleteProgram(m_shader);
			m_shader = 0;
		}
	}

	void BatchRenderer::AddQuad( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Rect&						bounds,
		const Foundation::Color&					fillColor,
		const std::optional<Foundation::BorderStyle>& border,
		float										cornerRadius
	) { // NOLINT(readability-convert-member-functions-to-static)
		uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());

		// Calculate rect center and half-dimensions for SDF
		Foundation::Vec2 center = bounds.Center();
		float			 halfW = bounds.width * 0.5F;
		float			 halfH = bounds.height * 0.5F;

		// Fill color
		Foundation::Vec4 colorVec = fillColor.ToVec4();

		// Pack border data (color RGB + width)
		Foundation::Vec4 borderData(0.0F, 0.0F, 0.0F, 0.0F);
		if (border.has_value()) {
			borderData = Foundation::Vec4(border->color.r, border->color.g, border->color.b, border->width);
			// Use corner radius from border if provided
			if (border->cornerRadius > 0.0F) {
				cornerRadius = border->cornerRadius;
			}
		}

		// Pack shape parameters (halfWidth, halfHeight, cornerRadius, borderPosition)
		float borderPosEnum = 1.0F; // Default to Center
		if (border.has_value()) {
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
		Foundation::Vec4 shapeParams(halfW, halfH, cornerRadius, borderPosEnum);

		// Add 4 vertices with rect-local coordinates
		// Top-left corner
		m_vertices.push_back(
			{bounds.TopLeft(),
			 Foundation::Vec2(-halfW, -halfH), // Rect-local: top-left
			 colorVec,
			 borderData,
			 shapeParams}
		);

		// Top-right corner
		m_vertices.push_back(
			{bounds.TopRight(),
			 Foundation::Vec2(halfW, -halfH), // Rect-local: top-right
			 colorVec,
			 borderData,
			 shapeParams}
		);

		// Bottom-right corner
		m_vertices.push_back(
			{bounds.BottomRight(),
			 Foundation::Vec2(halfW, halfH), // Rect-local: bottom-right
			 colorVec,
			 borderData,
			 shapeParams}
		);

		// Bottom-left corner
		m_vertices.push_back(
			{bounds.BottomLeft(),
			 Foundation::Vec2(-halfW, halfH), // Rect-local: bottom-left
			 colorVec,
			 borderData,
			 shapeParams}
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

		// Default SDF data (not used for tessellated shapes, but required for vertex format)
		Foundation::Vec2 zeroVec2(0.0F, 0.0F);
		Foundation::Vec4 zeroVec4(0.0F, 0.0F, 0.0F, 0.0F);

		// Add all vertices
		for (size_t i = 0; i < vertexCount; ++i) {
			m_vertices.push_back({
				vertices[i],
				zeroVec2, // rectLocalPos (unused)
				colorVec,
				zeroVec4, // borderData (unused)
				zeroVec4  // shapeParams (unused)
			});
		}

		// Add all indices (offset by baseIndex)
		for (size_t i = 0; i < indexCount; ++i) {
			m_indices.push_back(baseIndex + indices[i]);
		}
	}

	void BatchRenderer::Flush() {
		if (m_vertices.empty()) {
			return;
		}

		// Upload vertex data to GPU
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(PrimitiveVertex), m_vertices.data(), GL_DYNAMIC_DRAW);

		// Upload index data to GPU
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t), m_indices.data(), GL_DYNAMIC_DRAW);

		// Bind shader and VAO
		glUseProgram(m_shader);
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

		// Draw
		glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()), GL_UNSIGNED_INT, nullptr);

		m_drawCallCount++;

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

	GLuint BatchRenderer::CompileShader() { // NOLINT(readability-convert-member-functions-to-static)
		// Load shaders from disk (build directory)
		return ShaderLoader::LoadShaderProgram("build/shaders/primitive.vert", "build/shaders/primitive.frag");
	}

} // namespace Renderer
