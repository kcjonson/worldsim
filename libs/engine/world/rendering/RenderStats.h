#pragma once

// Per-frame draw metrics for the entity render paths.

#include <cstdint>

namespace engine::world {

	/// The orchestrator owns the canonical counters, resets them once at the top of
	/// the instanced path, and passes them by reference so each sub-renderer ADDS its
	/// contribution (raw GL draws bypass the BatchRenderer stats, so the metrics
	/// system reads them from here).
	struct RenderStats {
		uint32_t entities = 0;
		uint32_t drawCalls = 0;
		uint32_t triangles = 0;
	};

} // namespace engine::world
