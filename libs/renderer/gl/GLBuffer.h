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

	/// Create an empty buffer (allocates GL resource but doesn't upload data)
	/// Use this for buffers that will be filled later via glBufferData.
	/// @param target Buffer target (GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, etc.)
	static GLBuffer create(GLenum target) {
		GLBuffer buffer;
		buffer.bufferTarget = target;
		glGenBuffers(1, &buffer.bufferHandle);
		return buffer;
	}

	/// Create and initialize a buffer with data
	/// @param target Buffer target (GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, etc.)
	/// @param size Size of data in bytes
	/// @param data Pointer to data (can be nullptr for uninitialized buffer)
	/// @param usage Usage hint (GL_STATIC_DRAW, GL_DYNAMIC_DRAW, etc.)
	/// Note: Leaves the buffer BOUND after construction for vertex attribute setup.
	/// Call unbind() explicitly if needed.
	GLBuffer(GLenum target, GLsizeiptr size, const void* data, GLenum usage)
		: bufferTarget(target) {
		glGenBuffers(1, &bufferHandle);
		glBindBuffer(target, bufferHandle);
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
		: bufferHandle(other.bufferHandle)
		, bufferTarget(other.bufferTarget) {
		other.bufferHandle = 0;
	}

	/// Move assignment - releases current resource and takes ownership
	GLBuffer& operator=(GLBuffer&& other) noexcept {
		if (this != &other) {
			release();
			bufferHandle = other.bufferHandle;
			bufferTarget = other.bufferTarget;
			other.bufferHandle = 0;
		}
		return *this;
	}

	/// Get the raw OpenGL handle
	[[nodiscard]] GLuint handle() const { return bufferHandle; }

	/// Check if this buffer is valid (has a GPU resource)
	[[nodiscard]] bool isValid() const { return bufferHandle != 0; }

	/// Implicit conversion to GLuint for convenience with GL calls
	operator GLuint() const { return bufferHandle; } // NOLINT(google-explicit-constructor)

	/// Bind this buffer to its target
	void bind() const {
		glBindBuffer(bufferTarget, bufferHandle);
	}

	/// Unbind this buffer from its target
	void unbind() const {
		glBindBuffer(bufferTarget, 0);
	}

	/// Release the GPU resource (makes this buffer invalid)
	void release() {
		if (bufferHandle != 0) {
			glDeleteBuffers(1, &bufferHandle);
			bufferHandle = 0;
		}
	}

  private:
	GLuint bufferHandle = 0;
	GLenum bufferTarget = GL_ARRAY_BUFFER;
};

} // namespace Renderer
