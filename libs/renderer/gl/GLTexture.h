#pragma once

// GLTexture - RAII wrapper for OpenGL texture objects.
// Automatically handles glGenTextures/glDeleteTextures lifecycle.

#include <GL/glew.h>
#include <utility>

namespace Renderer {

/// RAII wrapper for OpenGL 2D texture objects.
/// Movable but not copyable - only one owner of the GPU resource.
class GLTexture {
  public:
	/// Default constructor - creates an empty (invalid) texture handle
	GLTexture() = default;

	/// Create a texture with specified parameters
	/// @param width Texture width in pixels
	/// @param height Texture height in pixels
	/// @param internalFormat Internal format (e.g., GL_RGBA8)
	/// @param format Data format (e.g., GL_RGBA)
	/// @param type Data type (e.g., GL_UNSIGNED_BYTE)
	/// @param data Pointer to pixel data (can be nullptr for uninitialized texture)
	GLTexture(int width, int height, GLenum internalFormat, GLenum format, GLenum type, const void* data)
		: m_width(width)
		, m_height(height) {
		glGenTextures(1, &m_handle);
		glBindTexture(GL_TEXTURE_2D, m_handle);
		glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), width, height, 0, format, type, data);
		// Default filtering - caller can change after construction
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		// Note: texture remains bound for caller to set additional parameters
	}

	/// Destructor - releases the GPU resource
	~GLTexture() {
		release();
	}

	// Non-copyable
	GLTexture(const GLTexture&) = delete;
	GLTexture& operator=(const GLTexture&) = delete;

	/// Move constructor - transfers ownership
	GLTexture(GLTexture&& other) noexcept
		: m_handle(other.m_handle)
		, m_width(other.m_width)
		, m_height(other.m_height) {
		other.m_handle = 0;
		other.m_width = 0;
		other.m_height = 0;
	}

	/// Move assignment - releases current resource and takes ownership
	GLTexture& operator=(GLTexture&& other) noexcept {
		if (this != &other) {
			release();
			m_handle = other.m_handle;
			m_width = other.m_width;
			m_height = other.m_height;
			other.m_handle = 0;
			other.m_width = 0;
			other.m_height = 0;
		}
		return *this;
	}

	/// Get the raw OpenGL handle
	[[nodiscard]] GLuint handle() const { return m_handle; }

	/// Check if this texture is valid (has a GPU resource)
	[[nodiscard]] bool isValid() const { return m_handle != 0; }

	/// Get texture dimensions
	[[nodiscard]] int width() const { return m_width; }
	[[nodiscard]] int height() const { return m_height; }

	/// Implicit conversion to GLuint for convenience with GL calls
	operator GLuint() const { return m_handle; } // NOLINT(google-explicit-constructor)

	/// Bind this texture to GL_TEXTURE_2D
	void bind() const {
		glBindTexture(GL_TEXTURE_2D, m_handle);
	}

	/// Unbind (bind texture 0)
	static void unbind() {
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	/// Release the GPU resource (makes this texture invalid)
	void release() {
		if (m_handle != 0) {
			glDeleteTextures(1, &m_handle);
			m_handle = 0;
			m_width = 0;
			m_height = 0;
		}
	}

  private:
	GLuint m_handle = 0;
	int m_width = 0;
	int m_height = 0;
};

} // namespace Renderer
