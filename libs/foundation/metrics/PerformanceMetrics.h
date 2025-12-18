#pragma once

// Performance metrics data structure for observability.
// Used by ui-sandbox debug server to stream real-time performance data.

#include <cstdint>
#include <string>
#include <vector>

namespace Foundation { // NOLINT(readability-identifier-naming)

	/// Per-system timing for ECS profiling
	struct EcsSystemTiming {
		const char* name{nullptr};
		float durationMs{};
	};

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
		float	 tileRenderMs{};	 // Time spent rendering tiles
		float	 entityRenderMs{};	 // Time spent rendering entities
		float	 updateMs{};		 // Time spent in update loop
		uint32_t tileCount{};		 // Number of tiles rendered
		uint32_t entityCount{};		 // Number of entities rendered
		uint32_t visibleChunkCount{}; // Number of chunks being rendered

		// Frame time histogram (for spike detection)
		uint32_t histogram0to8ms{};	  // Excellent (120+ FPS)
		uint32_t histogram8to16ms{};  // Good (60-120 FPS)
		uint32_t histogram16to33ms{}; // Acceptable (30-60 FPS)
		uint32_t histogram33plusMs{}; // Bad (<30 FPS)
		uint32_t histogramTotal{};	  // Total frames in histogram window

		// Spike detection
		float frameTime1PercentLow{}; // 99th percentile frame time (worst 1%)
		uint32_t spikeCount16ms{};	  // Frames > 16.6ms in recent window
		uint32_t spikeCount33ms{};	  // Frames > 33ms in recent window

		// ECS system timings (for per-system profiling)
		std::vector<EcsSystemTiming> ecsSystems;

		// GPU timing
		float gpuRenderMs{}; // Time GPU spent rendering (from previous frame)

		// System resources
		uint64_t memoryUsedBytes{};	   // Process resident memory (RSS)
		uint64_t memoryPeakBytes{};	   // Peak memory usage
		float	 cpuUsagePercent{};	   // CPU usage (0-100%, may exceed 100% on multi-core)
		uint32_t cpuCoreCount{};	   // Number of CPU cores (max CPU% = 100 * cores)
		float	 inputLatencyMs{};	   // Time from input event to frame start

		// Serialize to JSON for HTTP API
		std::string toJSON() const;
	};

} // namespace Foundation
