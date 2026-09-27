#include "metrics/GPUTimer.h"

#include <GL/glew.h>

namespace Renderer {

	void GPUTimer::begin() {
		if (open) {
			return;
		}
		if (!created) {
			for (Span& span : spans) {
				span.start = GLQuery::create();
				span.stop  = GLQuery::create();
			}
			created = true;
		}

		// The span about to be reused was issued kFrames spans ago. A result still
		// pending after that long is dropped rather than waited for.
		Span& span = spans[current];
		if (span.pending && span.stop.isResultAvailable()) {
			const GLuint64 startNs = span.start.getResult();
			const GLuint64 stopNs  = span.stop.getResult();
			lastTimeMs			   = static_cast<float>(static_cast<double>(stopNs - startNs) / 1.0e6);
		}
		span.pending = false;
		span.start.recordTimestamp();
		open = true;
	}

	void GPUTimer::end() {
		if (!open) {
			return;
		}
		Span& span = spans[current];
		span.stop.recordTimestamp();
		span.pending = true;
		open		 = false;
		current		 = (current + 1) % kFrames;
	}

} // namespace Renderer
