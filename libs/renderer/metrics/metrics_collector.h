#pragma once

// Metrics collector for frame timing and rendering statistics.
// Tracks FPS, frame time, and min/max values over a rolling window.

#include "metrics/performance_metrics.h"
#include <chrono>
#include <vector>

namespace Renderer { // NOLINT(readability-identifier-naming)

	class MetricsCollector {
	  public:
		MetricsCollector();

		// Frame lifecycle - call at start and end of each frame
		void beginFrame();
		void endFrame();

		// Get current metrics snapshot
		Foundation::PerformanceMetrics GetCurrentMetrics() const;

		// Set rendering stats (called by renderer)
		void setRenderStats(uint32_t drawCalls, uint32_t vertexCount, uint32_t triangleCount);

	  private:
		using Clock = std::chrono::high_resolution_clock;
		using TimePoint = std::chrono::time_point<Clock>;

		TimePoint		   frameStart;
		std::vector<float> frameTimeSamples; // Rolling window of frame times (last 60 frames)
		size_t			   currentSampleIndex;

		// Current rendering stats
		uint32_t drawCalls;
		uint32_t vertexCount;
		uint32_t triangleCount;

		// Helper: Get current Unix timestamp in milliseconds
		uint64_t getCurrentTimestamp() const;

		// Helper: Calculate FPS from frame time
		float calculateFPS(float frameTimeMs) const;

		// Helper: Get min/max frame times from sample window
		void getFrameTimeMinMax(float& outMin, float& outMax) const;
	};

} // namespace Renderer
