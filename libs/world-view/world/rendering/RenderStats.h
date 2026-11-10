#pragma once

// Per-frame draw metrics for the entity render paths.

#include <cstdint>

namespace worldview {

	/// The orchestrator owns the canonical counters, resets them once at the top of
	/// the instanced path, and passes them by reference so each sub-renderer ADDS its
	/// contribution (raw GL draws bypass Primitives::getStats(), so the metrics
	/// system reads them from here).
	struct RenderStats {
		uint32_t entities = 0;
		uint32_t drawCalls = 0;
		uint32_t triangles = 0;

		// The depth-sorted upright stream (InstancedEntityRenderer::emitSorted): tall statics
		// and actors. Those draws go through the batch renderer, so the batch totals already
		// hold them; these split them out for perf captures, in the batch renderer's units
		// (an actor's triangle counts as two, an instanced mesh as its indices / 3 per instance).
		uint32_t uprightDrawCalls = 0;
		uint32_t uprightTriangles = 0;
	};

} // namespace worldview
