// Batch Renderer implementation.
// Accumulates 2D geometry and renders in optimized batches to minimize draw calls.

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
		// Load shader from files
		if (!m_shader.LoadFromFile("primitive.vert", "primitive.frag")) {
			std::cerr << "Failed to load primitive shaders!" << std::endl;
			return;
		}

		// Get uniform locations
		m_projectionLoc = glGetUniformLocation(m_shader.GetProgram(), "u_projection");
		m_transformLoc = glGetUniformLocation(m_shader.GetProgram(), "u_transform");

		// Create VAO/VBO/IBO
		glGenVertexArrays(1, &m_vao);
		glGenBuffers(1, &m_vbo);
		glGenBuffers(1, &m_ibo);

		glBindVertexArray(m_vao);

		// Set up vertex buffer
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

		// Position attribute
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, position));

		// TexCoord attribute
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, texCoord));

		// Color attribute
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, color));

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

		// Shader cleanup is handled automatically by RAII
	}

	void BatchRenderer::AddQuad( // NOLINT(readability-convert-member-functions-to-static)
		const Foundation::Rect&	 bounds,
		const Foundation::Color& color
	) { // NOLINT(readability-convert-member-functions-to-static)
		uint32_t baseIndex = static_cast<uint32_t>(m_vertices.size());

		Foundation::Vec4 colorVec = color.ToVec4();

		// Add 4 vertices (quad corners)
		m_vertices.push_back({bounds.TopLeft(), {0, 0}, colorVec});
		m_vertices.push_back({bounds.TopRight(), {1, 0}, colorVec});
		m_vertices.push_back({bounds.BottomRight(), {1, 1}, colorVec});
		m_vertices.push_back({bounds.BottomLeft(), {0, 1}, colorVec});

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

		// Add all vertices
		for (size_t i = 0; i < vertexCount; ++i) {
			// For now, use (0,0) for texCoords (not used for vector graphics)
			m_vertices.push_back({vertices[i], {0, 0}, colorVec});
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

} // namespace Renderer
