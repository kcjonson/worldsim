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
		glGenQueries(1, &query.queryHandle);
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
		: queryHandle(other.queryHandle) {
		other.queryHandle = 0;
	}

	/// Move assignment - releases current resource and takes ownership
	GLQuery& operator=(GLQuery&& other) noexcept {
		if (this != &other) {
			release();
			queryHandle = other.queryHandle;
			other.queryHandle = 0;
		}
		return *this;
	}

	/// Get the raw OpenGL handle
	[[nodiscard]] GLuint handle() const { return queryHandle; }

	/// Check if this query is valid (has a GPU resource)
	[[nodiscard]] bool isValid() const { return queryHandle != 0; }

	/// Implicit conversion to GLuint for convenience with GL calls
	operator GLuint() const { return queryHandle; } // NOLINT(google-explicit-constructor)

	/// Begin a query (e.g., GL_TIME_ELAPSED)
	void begin(GLenum target) const {
		glBeginQuery(target, queryHandle);
	}

	/// End a query
	static void end(GLenum target) {
		glEndQuery(target);
	}

	/// Check if result is available
	[[nodiscard]] bool isResultAvailable() const {
		GLint available = 0;
		glGetQueryObjectiv(queryHandle, GL_QUERY_RESULT_AVAILABLE, &available);
		return available != 0;
	}

	/// Get the query result (blocks if not yet available)
	[[nodiscard]] GLuint64 getResult() const {
		GLuint64 result = 0;
		glGetQueryObjectui64v(queryHandle, GL_QUERY_RESULT, &result);
		return result;
	}

	/// Release the GPU resource (makes this query invalid)
	void release() {
		if (queryHandle != 0) {
			glDeleteQueries(1, &queryHandle);
			queryHandle = 0;
		}
	}

  private:
	GLuint queryHandle = 0;
};

} // namespace Renderer
