// GPUTimer implementation using OpenGL timer queries.

#include "metrics/GPUTimer.h"
#include <GL/glew.h>

namespace Renderer {

	GPUTimer::GPUTimer() {
		// Check if timer queries are supported
		if (GLEW_ARB_timer_query || GLEW_VERSION_3_3) {
			supported = true;
			glGenQueries(kQueryCount, queries);
		}
	}

	GPUTimer::~GPUTimer() {
		if (supported && queries[0] != 0) {
			glDeleteQueries(kQueryCount, queries);
		}
	}

	void GPUTimer::begin() {
		if (!supported || !enabled || inQuery) {
			return;
		}

		// If we have a completed query from 2 frames ago, read its result
		if (hasResult) {
			int previousQuery = (currentQuery + 1) % kQueryCount;

			GLint available = 0;
			glGetQueryObjectiv(queries[previousQuery], GL_QUERY_RESULT_AVAILABLE, &available);

			if (available != 0) {
				GLuint64 timeNs = 0;
				glGetQueryObjectui64v(queries[previousQuery], GL_QUERY_RESULT, &timeNs);
				lastTimeMs = static_cast<float>(timeNs) / 1000000.0F; // ns to ms
			}
		}

		// Begin new query
		glBeginQuery(GL_TIME_ELAPSED, queries[currentQuery]);
		inQuery = true;
	}

	void GPUTimer::end() {
		if (!supported || !enabled || !inQuery) {
			return;
		}

		glEndQuery(GL_TIME_ELAPSED);
		inQuery = false;
		hasResult = true;

		// Advance to next query slot
		currentQuery = (currentQuery + 1) % kQueryCount;
	}

} // namespace Renderer
