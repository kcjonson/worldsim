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

		// Serialize to JSON for HTTP API
		std::string ToJSON() const;
	};

} // namespace Foundation
