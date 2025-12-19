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
		glGenVertexArrays(1, &vao.vaoHandle);
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
		: vaoHandle(other.vaoHandle) {
		other.vaoHandle = 0;
	}

	/// Move assignment - releases current resource and takes ownership
	GLVertexArray& operator=(GLVertexArray&& other) noexcept {
		if (this != &other) {
			release();
			vaoHandle = other.vaoHandle;
			other.vaoHandle = 0;
		}
		return *this;
	}

	/// Get the raw OpenGL handle
	[[nodiscard]] GLuint handle() const { return vaoHandle; }

	/// Check if this VAO is valid (has a GPU resource)
	[[nodiscard]] bool isValid() const { return vaoHandle != 0; }

	/// Implicit conversion to GLuint for convenience with GL calls
	operator GLuint() const { return vaoHandle; } // NOLINT(google-explicit-constructor)

	/// Bind this VAO (makes it the active vertex array)
	void bind() const {
		glBindVertexArray(vaoHandle);
	}

	/// Unbind (bind VAO 0)
	static void unbind() {
		glBindVertexArray(0);
	}

	/// Release the GPU resource (makes this VAO invalid)
	void release() {
		if (vaoHandle != 0) {
			glDeleteVertexArrays(1, &vaoHandle);
			vaoHandle = 0;
		}
	}

  private:
	GLuint vaoHandle = 0;
};

} // namespace Renderer
