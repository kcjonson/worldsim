// GPUTimer implementation using OpenGL timer queries.

#include "metrics/GPUTimer.h"
#include <GL/glew.h>
#include "utils/Log.h"

namespace Renderer {

	GPUTimer::GPUTimer() {
		// Check if timer queries are supported
		if (GLEW_ARB_timer_query || GLEW_VERSION_3_3) {
			supported = true;
			// Create query objects using RAII wrappers
			for (auto& query : queries) {
				query = GLQuery::create();
			}
			LOG_INFO(Renderer, "GPUTimer: timer queries supported (ARB=%d, GL3.3=%d)",
					 GLEW_ARB_timer_query ? 1 : 0, GLEW_VERSION_3_3 ? 1 : 0);
		} else {
			LOG_WARNING(Renderer, "GPUTimer: timer queries NOT supported");
		}
	}

	void GPUTimer::begin() {
		if (!supported || !enabled || inQuery) {
			return;
		}

		// If we have a completed query from 2 frames ago, read its result
		if (hasResult) {
			int previousQuery = (currentQuery + 1) % kQueryCount;

			if (queries[previousQuery].isResultAvailable()) {
				GLuint64 timeNs = queries[previousQuery].getResult();
				lastTimeMs = static_cast<float>(timeNs) / 1000000.0F; // ns to ms
			}
		}

		// Begin new query using RAII wrapper
		queries[currentQuery].begin(GL_TIME_ELAPSED);
		inQuery = true;
	}

	void GPUTimer::end() {
		if (!supported || !enabled || !inQuery) {
			return;
		}

		GLQuery::end(GL_TIME_ELAPSED);
		inQuery = false;
		hasResult = true;

		// Advance to next query slot
		currentQuery = (currentQuery + 1) % kQueryCount;
	}

} // namespace Renderer
