#pragma once

// GLVertexArray - RAII wrapper for OpenGL Vertex Array Objects (VAOs).
// Automatically handles glGenVertexArrays/glDeleteVertexArrays lifecycle.

#include <GL/glew.h>
#include <utility>

namespace Renderer {

/// RAII wrapper for OpenGL Vertex Array Objects.
/// VAOs store the configuration of vertex attributes - think of them as
/// "saved state" that remembers which buffers to use and how to interpret them.
/// Movable but not copyable - only one owner of the GPU resource.
class GLVertexArray {
  public:
	/// Default constructor - creates an empty (invalid) VAO handle
	GLVertexArray() = default;

	/// Create and initialize a VAO
	static GLVertexArray create() {
		GLVertexArray vao;
		glGenVertexArrays(1, &vao.m_handle);
		return vao;
	}

	/// Destructor - releases the GPU resource
	~GLVertexArray() {
		release();
	}

	// Non-copyable
	GLVertexArray(const GLVertexArray&) = delete;
	GLVertexArray& operator=(const GLVertexArray&) = delete;

	/// Move constructor - transfers ownership
	GLVertexArray(GLVertexArray&& other) noexcept
		: m_handle(other.m_handle) {
		other.m_handle = 0;
	}

	/// Move assignment - releases current resource and takes ownership
	GLVertexArray& operator=(GLVertexArray&& other) noexcept {
		if (this != &other) {
			release();
			m_handle = other.m_handle;
			other.m_handle = 0;
		}
		return *this;
	}

	/// Get the raw OpenGL handle
	[[nodiscard]] GLuint handle() const { return m_handle; }

	/// Check if this VAO is valid (has a GPU resource)
	[[nodiscard]] bool isValid() const { return m_handle != 0; }

	/// Implicit conversion to GLuint for convenience with GL calls
	operator GLuint() const { return m_handle; } // NOLINT(google-explicit-constructor)

	/// Bind this VAO (makes it the active vertex array)
	void bind() const {
		glBindVertexArray(m_handle);
	}

	/// Unbind (bind VAO 0)
	static void unbind() {
		glBindVertexArray(0);
	}

	/// Release the GPU resource (makes this VAO invalid)
	void release() {
		if (m_handle != 0) {
			glDeleteVertexArrays(1, &m_handle);
			m_handle = 0;
		}
	}

  private:
	GLuint m_handle = 0;
};

} // namespace Renderer
