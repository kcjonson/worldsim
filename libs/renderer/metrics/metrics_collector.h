#pragma once

// Metrics collector for frame timing and rendering statistics.
// Tracks FPS, frame time, and min/max values over a rolling window.

#include "metrics/performance_metrics.h"
#include <chrono>
#include <vector>

namespace Renderer {

	class MetricsCollector {
	  public:
		MetricsCollector();

		// Frame lifecycle - call at start and end of each frame
		void BeginFrame();
		void EndFrame();

		// Get current metrics snapshot
		Foundation::PerformanceMetrics GetCurrentMetrics() const;

		// Set rendering stats (called by renderer)
		void SetRenderStats(uint32_t drawCalls, uint32_t vertexCount, uint32_t triangleCount);

	  private:
		using Clock = std::chrono::high_resolution_clock;
		using TimePoint = std::chrono::time_point<Clock>;

		TimePoint		   m_frameStart;
		std::vector<float> m_frameTimeSamples; // Rolling window of frame times (last 60 frames)
		size_t			   m_currentSampleIndex;

		// Current rendering stats
		uint32_t m_drawCalls;
		uint32_t m_vertexCount;
		uint32_t m_triangleCount;

		// Helper: Get current Unix timestamp in milliseconds
		uint64_t GetCurrentTimestamp() const;

		// Helper: Calculate FPS from frame time
		float CalculateFPS(float frameTimeMs) const;

		// Helper: Get min/max frame times from sample window
		void GetFrameTimeMinMax(float& outMin, float& outMax) const;
	};

} // namespace Renderer
