// GPUTimer implementation using OpenGL timer queries.

#include "metrics/GPUTimer.h"
#include <GL/glew.h>

namespace Renderer {

GPUTimer::GPUTimer() {
	// Check if timer queries are supported
	if (GLEW_ARB_timer_query || GLEW_VERSION_3_3) {
		m_supported = true;
		glGenQueries(kQueryCount, m_queries);
	}
}

GPUTimer::~GPUTimer() {
	if (m_supported && m_queries[0] != 0) {
		glDeleteQueries(kQueryCount, m_queries);
	}
}

void GPUTimer::begin() {
	if (!m_supported || !m_enabled || m_inQuery) {
		return;
	}

	// If we have a completed query from 2 frames ago, read its result
	if (m_hasResult) {
		int previousQuery = (m_currentQuery + 1) % kQueryCount;

		GLint available = 0;
		glGetQueryObjectiv(m_queries[previousQuery], GL_QUERY_RESULT_AVAILABLE, &available);

		if (available != 0) {
			GLuint64 timeNs = 0;
			glGetQueryObjectui64v(m_queries[previousQuery], GL_QUERY_RESULT, &timeNs);
			m_lastTimeMs = static_cast<float>(timeNs) / 1000000.0F; // ns to ms
		}
	}

	// Begin new query
	glBeginQuery(GL_TIME_ELAPSED, m_queries[m_currentQuery]);
	m_inQuery = true;
}

void GPUTimer::end() {
	if (!m_supported || !m_enabled || !m_inQuery) {
		return;
	}

	glEndQuery(GL_TIME_ELAPSED);
	m_inQuery = false;
	m_hasResult = true;

	// Advance to next query slot
	m_currentQuery = (m_currentQuery + 1) % kQueryCount;
}

} // namespace Renderer
