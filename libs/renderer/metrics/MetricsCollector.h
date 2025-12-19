#pragma once

// Metrics collector for frame timing and rendering statistics.
// Tracks FPS, frame time, and min/max values over a rolling window.

#include "metrics/PerformanceMetrics.h"
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
		Foundation::PerformanceMetrics getCurrentMetrics() const;

		// Set rendering stats (called by renderer)
		void setRenderStats(uint32_t drawCalls, uint32_t vertexCount, uint32_t triangleCount);

		// Set timing breakdown (called by game scene for profiling)
		void setTimingBreakdown(float tileRenderMs, float entityRenderMs, float updateMs,
								uint32_t tileCount, uint32_t entityCount, uint32_t visibleChunkCount);

		// Set ECS system timings (called by game scene after ECS update)
		void setEcsSystemTimings(const std::vector<Foundation::EcsSystemTiming>& timings);

		// Set GPU render time (called by game scene after reading GPU timer)
		void setGpuRenderTime(float gpuMs);

		// Set main loop timing breakdown (called by Application after each frame)
		void setMainLoopTimings(float pollEventsMs, float inputHandleMs, float sceneUpdateMs,
								float sceneRenderMs, float swapBuffersMs);

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

		// Timing breakdown
		float	 tileRenderMs{};
		float	 entityRenderMs{};
		float	 updateMs{};
		uint32_t tileCount{};
		uint32_t entityCount{};
		uint32_t visibleChunkCount{};

		// ECS system timings
		std::vector<Foundation::EcsSystemTiming> ecsSystemTimings;

		// GPU timing
		float gpuRenderMs{};

		// Main loop timing breakdown
		float m_pollEventsMs{};
		float m_inputHandleMs{};
		float m_sceneUpdateMs{};
		float m_sceneRenderMs{};
		float m_swapBuffersMs{};

		// Helper: Get current Unix timestamp in milliseconds
		uint64_t getCurrentTimestamp() const;

		// Helper: Calculate FPS from frame time
		float calculateFPS(float frameTimeMs) const;

		// Helper: Get min/max frame times from sample window
		void getFrameTimeMinMax(float& outMin, float& outMax) const;

		// Helper: Compute histogram buckets from sample window
		void computeHistogram(uint32_t& out0to8, uint32_t& out8to16, uint32_t& out16to33, uint32_t& out33plus) const;

		// Helper: Compute 1% low (99th percentile frame time)
		float compute1PercentLow() const;

		// Helper: Count frames exceeding threshold in sample window
		uint32_t countSpikes(float thresholdMs) const;

		// Scratch buffer for percentile calculation (avoids per-frame allocation)
		mutable std::vector<float> percentileScratch;
	};

} // namespace Renderer
