#pragma once

// GLQuery - RAII wrapper for OpenGL query objects.
// Automatically handles glGenQueries/glDeleteQueries lifecycle.
// Used for GPU timing queries (GL_TIME_ELAPSED), occlusion queries, etc.

#include <GL/glew.h>
#include <utility>

namespace Renderer {

/// RAII wrapper for OpenGL Query Objects.
/// Movable but not copyable - only one owner of the GPU resource.
class GLQuery {
  public:
	/// Default constructor - creates an empty (invalid) query handle
	GLQuery() = default;

	/// Create a query object
	static GLQuery create() {
		GLQuery query;
		glGenQueries(1, &query.m_handle);
		return query;
	}

	/// Destructor - releases the GPU resource
	~GLQuery() {
		release();
	}

	// Non-copyable
	GLQuery(const GLQuery&) = delete;
	GLQuery& operator=(const GLQuery&) = delete;

	/// Move constructor - transfers ownership
	GLQuery(GLQuery&& other) noexcept
		: m_handle(other.m_handle) {
		other.m_handle = 0;
	}

	/// Move assignment - releases current resource and takes ownership
	GLQuery& operator=(GLQuery&& other) noexcept {
		if (this != &other) {
			release();
			m_handle = other.m_handle;
			other.m_handle = 0;
		}
		return *this;
	}

	/// Get the raw OpenGL handle
	[[nodiscard]] GLuint handle() const { return m_handle; }

	/// Check if this query is valid (has a GPU resource)
	[[nodiscard]] bool isValid() const { return m_handle != 0; }

	/// Implicit conversion to GLuint for convenience with GL calls
	operator GLuint() const { return m_handle; } // NOLINT(google-explicit-constructor)

	/// Begin a query (e.g., GL_TIME_ELAPSED)
	void begin(GLenum target) const {
		glBeginQuery(target, m_handle);
	}

	/// End a query
	static void end(GLenum target) {
		glEndQuery(target);
	}

	/// Check if result is available
	[[nodiscard]] bool isResultAvailable() const {
		GLint available = 0;
		glGetQueryObjectiv(m_handle, GL_QUERY_RESULT_AVAILABLE, &available);
		return available != 0;
	}

	/// Get the query result (blocks if not yet available)
	[[nodiscard]] GLuint64 getResult() const {
		GLuint64 result = 0;
		glGetQueryObjectui64v(m_handle, GL_QUERY_RESULT, &result);
		return result;
	}

	/// Release the GPU resource (makes this query invalid)
	void release() {
		if (m_handle != 0) {
			glDeleteQueries(1, &m_handle);
			m_handle = 0;
		}
	}

  private:
	GLuint m_handle = 0;
};

} // namespace Renderer
