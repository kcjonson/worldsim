#pragma once

// GLBuffer - RAII wrapper for OpenGL buffer objects.
// Automatically handles glGenBuffers/glDeleteBuffers lifecycle.

#include <GL/glew.h>
#include <utility>

namespace Renderer {

/// RAII wrapper for OpenGL buffer objects (VBOs, IBOs, etc.)
/// Movable but not copyable - only one owner of the GPU resource.
class GLBuffer {
  public:
	/// Default constructor - creates an empty (invalid) buffer handle
	GLBuffer() = default;

	/// Create and initialize a buffer with data
	/// @param target Buffer target (GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, etc.)
	/// @param size Size of data in bytes
	/// @param data Pointer to data (can be nullptr for uninitialized buffer)
	/// @param usage Usage hint (GL_STATIC_DRAW, GL_DYNAMIC_DRAW, etc.)
	/// Note: Leaves the buffer BOUND after construction for vertex attribute setup.
	/// Call unbind() explicitly if needed.
	GLBuffer(GLenum target, GLsizeiptr size, const void* data, GLenum usage)
		: m_target(target) {
		glGenBuffers(1, &m_handle);
		glBindBuffer(target, m_handle);
		glBufferData(target, size, data, usage);
		// Note: buffer remains bound - this is intentional for VAO attribute setup
	}

	/// Destructor - releases the GPU resource
	~GLBuffer() {
		release();
	}

	// Non-copyable
	GLBuffer(const GLBuffer&) = delete;
	GLBuffer& operator=(const GLBuffer&) = delete;

	/// Move constructor - transfers ownership
	GLBuffer(GLBuffer&& other) noexcept
		: m_handle(other.m_handle)
		, m_target(other.m_target) {
		other.m_handle = 0;
	}

	/// Move assignment - releases current resource and takes ownership
	GLBuffer& operator=(GLBuffer&& other) noexcept {
		if (this != &other) {
			release();
			m_handle = other.m_handle;
			m_target = other.m_target;
			other.m_handle = 0;
		}
		return *this;
	}

	/// Get the raw OpenGL handle
	[[nodiscard]] GLuint handle() const { return m_handle; }

	/// Check if this buffer is valid (has a GPU resource)
	[[nodiscard]] bool isValid() const { return m_handle != 0; }

	/// Implicit conversion to GLuint for convenience with GL calls
	operator GLuint() const { return m_handle; } // NOLINT(google-explicit-constructor)

	/// Bind this buffer to its target
	void bind() const {
		glBindBuffer(m_target, m_handle);
	}

	/// Unbind this buffer from its target
	void unbind() const {
		glBindBuffer(m_target, 0);
	}

	/// Release the GPU resource (makes this buffer invalid)
	void release() {
		if (m_handle != 0) {
			glDeleteBuffers(1, &m_handle);
			m_handle = 0;
		}
	}

  private:
	GLuint m_handle = 0;
	GLenum m_target = GL_ARRAY_BUFFER;
};

} // namespace Renderer
