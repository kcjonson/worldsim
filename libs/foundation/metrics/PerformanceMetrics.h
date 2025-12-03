#pragma once

// Performance metrics data structure for observability.
// Used by ui-sandbox debug server to stream real-time performance data.

#include <cstdint>
#include <string>

namespace Foundation { // NOLINT(readability-identifier-naming)

	struct PerformanceMetrics {
		uint64_t timestamp{};	   // Unix timestamp in milliseconds
		float	 fps{};			   // Frames per second
		float	 frameTimeMs{};	   // Current frame time in milliseconds
		float	 frameTimeMinMs{}; // Minimum frame time over last second
		float	 frameTimeMaxMs{}; // Maximum frame time over last second
		uint32_t drawCalls{};	   // Number of draw calls this frame
		uint32_t vertexCount{};	   // Number of vertices rendered this frame
		uint32_t triangleCount{};  // Number of triangles rendered this frame

		// Timing breakdown (for profiling bottlenecks)
		float	 tileRenderMs{};   // Time spent rendering tiles
		float	 entityRenderMs{}; // Time spent rendering entities
		float	 updateMs{};	   // Time spent in update loop
		uint32_t tileCount{};	   // Number of tiles rendered
		uint32_t entityCount{};	   // Number of entities rendered

		// Serialize to JSON for HTTP API
		std::string toJSON() const;
	};

} // namespace Foundation
