// Batch Renderer implementation.
// Accumulates 2D geometry and renders in optimized batches to minimize draw calls.

#include "primitives/batch_renderer.h"
#include "coordinate_system/coordinate_system.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace Renderer {

	// Vertex shader source
	static const char* kVertexShaderSource = R"(
#version 330 core

layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
layout(location = 2) in vec4 a_color;

uniform mat4 u_projection;
uniform mat4 u_transform;

out vec2 v_texCoord;
out vec4 v_color;

void main() {
	v_texCoord = a_texCoord;
	v_color = a_color;
	gl_Position = u_projection * u_transform * vec4(a_position, 0.0, 1.0);
}
)";

	// Fragment shader source
	static const char* kFragmentShaderSource = R"(
#version 330 core

in vec2 v_texCoord;
in vec4 v_color;

out vec4 FragColor;

void main() {
	FragColor = v_color;
}
)";

	BatchRenderer::BatchRenderer() {
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
		if (m_vao) {
			glDeleteVertexArrays(1, &m_vao);
			m_vao = 0;
		}

		if (m_vbo) {
			glDeleteBuffers(1, &m_vbo);
			m_vbo = 0;
		}

		if (m_ibo) {
			glDeleteBuffers(1, &m_ibo);
			m_ibo = 0;
		}

		if (m_shader) {
			glDeleteProgram(m_shader);
			m_shader = 0;
		}
	}

	void BatchRenderer::AddQuad(const Foundation::Rect& bounds, const Foundation::Color& color) {
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

	void BatchRenderer::AddTriangles(
		const Foundation::Vec2*	 vertices,
		const uint16_t*			 indices,
		size_t					 vertexCount,
		size_t					 indexCount,
		const Foundation::Color& color
	) {
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
		if (m_vertices.empty())
			return;

		// Upload vertex data to GPU
		glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
		glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(PrimitiveVertex), m_vertices.data(), GL_DYNAMIC_DRAW);

		// Upload index data to GPU
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t), m_indices.data(), GL_DYNAMIC_DRAW);

		// Bind shader and VAO
		glUseProgram(m_shader);
		glBindVertexArray(m_vao);

		// Create projection matrix using viewport dimensions (NOT CoordinateSystem)
		// Worldsim uses 1:1 pixel mapping with framebuffer (physical pixels)
		// CoordinateSystem is only used for percentage layout helpers
		Foundation::Mat4 projection =
			glm::ortho(0.0f, static_cast<float>(m_viewportWidth), static_cast<float>(m_viewportHeight), 0.0f, -1.0f, 1.0f);
		Foundation::Mat4 transform = Foundation::Mat4(1.0f);

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

	void BatchRenderer::SetCoordinateSystem(CoordinateSystem* coordSystem) {
		m_coordinateSystem = coordSystem;
	}

	BatchRenderer::RenderStats BatchRenderer::GetStats() const {
		RenderStats stats;
		stats.drawCalls = static_cast<uint32_t>(m_drawCallCount);
		stats.vertexCount = static_cast<uint32_t>(m_vertices.size());
		stats.triangleCount = static_cast<uint32_t>(m_indices.size() / 3);
		return stats;
	}

	GLuint BatchRenderer::CompileShader() {
		// Compile vertex shader
		GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
		glShaderSource(vertexShader, 1, &kVertexShaderSource, nullptr);
		glCompileShader(vertexShader);

		GLint success = 0;
		glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
		if (!success) {
			char infoLog[512];
			glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
			std::cerr << "Vertex shader compilation failed: " << infoLog << std::endl;
			glDeleteShader(vertexShader);
			return 0;
		}

		// Compile fragment shader
		GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
		glShaderSource(fragmentShader, 1, &kFragmentShaderSource, nullptr);
		glCompileShader(fragmentShader);

		glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
		if (!success) {
			char infoLog[512];
			glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
			std::cerr << "Fragment shader compilation failed: " << infoLog << std::endl;
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);
			return 0;
		}

		// Link shader program
		GLuint program = glCreateProgram();
		glAttachShader(program, vertexShader);
		glAttachShader(program, fragmentShader);
		glLinkProgram(program);

		glGetProgramiv(program, GL_LINK_STATUS, &success);
		if (!success) {
			char infoLog[512];
			glGetProgramInfoLog(program, 512, nullptr, infoLog);
			std::cerr << "Shader program linking failed: " << infoLog << std::endl;
			glDeleteProgram(program);
			glDeleteShader(vertexShader);
			glDeleteShader(fragmentShader);
			return 0;
		}

		// Clean up shaders (no longer needed after linking)
		glDeleteShader(vertexShader);
		glDeleteShader(fragmentShader);

		return program;
	}

} // namespace Renderer
