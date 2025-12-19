#pragma once

// GLFramebuffer - RAII wrapper for OpenGL framebuffer objects.
// Automatically handles glGenFramebuffers/glDeleteFramebuffers lifecycle.

#include <GL/glew.h>
#include <utility>

namespace Renderer {

/// RAII wrapper for OpenGL Framebuffer Objects (FBOs).
/// Movable but not copyable - only one owner of the GPU resource.
class GLFramebuffer {
  public:
	/// Default constructor - creates an empty (invalid) framebuffer handle
	GLFramebuffer() = default;

	/// Create a framebuffer
	static GLFramebuffer create() {
		GLFramebuffer fbo;
		glGenFramebuffers(1, &fbo.fboHandle);
		return fbo;
	}

	/// Destructor - releases the GPU resource
	~GLFramebuffer() {
		release();
	}

	// Non-copyable
	GLFramebuffer(const GLFramebuffer&) = delete;
	GLFramebuffer& operator=(const GLFramebuffer&) = delete;

	/// Move constructor - transfers ownership
	GLFramebuffer(GLFramebuffer&& other) noexcept
		: fboHandle(other.fboHandle) {
		other.fboHandle = 0;
	}

	/// Move assignment - releases current resource and takes ownership
	GLFramebuffer& operator=(GLFramebuffer&& other) noexcept {
		if (this != &other) {
			release();
			fboHandle = other.fboHandle;
			other.fboHandle = 0;
		}
		return *this;
	}

	/// Get the raw OpenGL handle
	[[nodiscard]] GLuint handle() const { return fboHandle; }

	/// Check if this framebuffer is valid (has a GPU resource)
	[[nodiscard]] bool isValid() const { return fboHandle != 0; }

	/// Implicit conversion to GLuint for convenience with GL calls
	operator GLuint() const { return fboHandle; } // NOLINT(google-explicit-constructor)

	/// Bind this framebuffer
	void bind() const {
		glBindFramebuffer(GL_FRAMEBUFFER, fboHandle);
	}

	/// Unbind (bind framebuffer 0 - the default framebuffer)
	static void unbind() {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	/// Release the GPU resource (makes this framebuffer invalid)
	void release() {
		if (fboHandle != 0) {
			glDeleteFramebuffers(1, &fboHandle);
			fboHandle = 0;
		}
	}

  private:
	GLuint fboHandle = 0;
};

} // namespace Renderer
