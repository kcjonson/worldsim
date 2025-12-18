// Metrics collector implementation.

#include "metrics/MetricsCollector.h"
#include <algorithm>
#include <numeric>

namespace Renderer {

	MetricsCollector::MetricsCollector() // NOLINT(cppcoreguidelines-pro-type-member-init,modernize-use-equals-default)
		: currentSampleIndex(0),
		  drawCalls(0),
		  vertexCount(0),
		  triangleCount(0) {
		// Reserve space for 60 frames (1 second at 60 FPS)
		frameTimeSamples.resize(60, 16.67F); // Initialize with ~60 FPS
	}

	void MetricsCollector::beginFrame() {
		frameStart = Clock::now();
	}

	void MetricsCollector::endFrame() { // NOLINT(readability-convert-member-functions-to-static)
		auto  frameEnd = Clock::now();
		auto  duration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - frameStart);
		float frameTimeMs = duration.count() / 1000.0F;

		// Store in rolling buffer
		frameTimeSamples[currentSampleIndex] = frameTimeMs;
		currentSampleIndex = (currentSampleIndex + 1) % frameTimeSamples.size();
	}

	Foundation::PerformanceMetrics MetricsCollector::getCurrentMetrics() const {
		Foundation::PerformanceMetrics metrics;

		metrics.timestamp = getCurrentTimestamp();

		// Use most recent frame time
		size_t lastIndex = (currentSampleIndex + frameTimeSamples.size() - 1) % frameTimeSamples.size();
		metrics.frameTimeMs = frameTimeSamples[lastIndex];
		metrics.fps = calculateFPS(metrics.frameTimeMs);

		// Get min/max from sample window
		getFrameTimeMinMax(metrics.frameTimeMinMs, metrics.frameTimeMaxMs);

		// Rendering stats
		metrics.drawCalls = drawCalls;
		metrics.vertexCount = vertexCount;
		metrics.triangleCount = triangleCount;

		// Timing breakdown
		metrics.tileRenderMs = tileRenderMs;
		metrics.entityRenderMs = entityRenderMs;
		metrics.updateMs = updateMs;
		metrics.tileCount = tileCount;
		metrics.entityCount = entityCount;
		metrics.visibleChunkCount = visibleChunkCount;

		// Histogram
		computeHistogram(metrics.histogram0to8ms, metrics.histogram8to16ms, metrics.histogram16to33ms,
						 metrics.histogram33plusMs);
		metrics.histogramTotal = static_cast<uint32_t>(frameTimeSamples.size());

		// Spike detection
		metrics.frameTime1PercentLow = compute1PercentLow();
		metrics.spikeCount16ms = countSpikes(16.67F);
		metrics.spikeCount33ms = countSpikes(33.33F);

		// ECS system timings
		metrics.ecsSystems = ecsSystemTimings;

		// GPU timing
		metrics.gpuRenderMs = gpuRenderMs;

		return metrics;
	}

	void MetricsCollector::setRenderStats(uint32_t inDrawCalls, uint32_t inVertexCount, uint32_t inTriangleCount) {
		drawCalls = inDrawCalls;
		vertexCount = inVertexCount;
		triangleCount = inTriangleCount;
	}

	void MetricsCollector::setTimingBreakdown(float inTileRenderMs, float inEntityRenderMs, float inUpdateMs,
											  uint32_t inTileCount, uint32_t inEntityCount, uint32_t inVisibleChunkCount) {
		tileRenderMs = inTileRenderMs;
		entityRenderMs = inEntityRenderMs;
		updateMs = inUpdateMs;
		tileCount = inTileCount;
		entityCount = inEntityCount;
		visibleChunkCount = inVisibleChunkCount;
	}

	void MetricsCollector::setEcsSystemTimings(const std::vector<Foundation::EcsSystemTiming>& timings) {
		ecsSystemTimings = timings;
	}

	void MetricsCollector::setGpuRenderTime(float gpuMs) {
		gpuRenderMs = gpuMs;
	}

	uint64_t MetricsCollector::getCurrentTimestamp() const { // NOLINT(readability-convert-member-functions-to-static)
		auto now = std::chrono::system_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
		return static_cast<uint64_t>(ms.count());
	}

	float MetricsCollector::calculateFPS(float frameTimeMs) const { // NOLINT(readability-convert-member-functions-to-static)
		if (frameTimeMs < 0.001F) {
			return 0.0F; // Avoid division by zero
		}
		return 1000.0F / frameTimeMs;
	}

	void MetricsCollector::getFrameTimeMinMax( // NOLINT(readability-convert-member-functions-to-static)
		float& outMin,
		float& outMax
	) const {
		if (frameTimeSamples.empty()) {
			outMin = outMax = 0.0F;
			return;
		}

		auto minmax = std::minmax_element(frameTimeSamples.begin(), frameTimeSamples.end());
		outMin = *minmax.first;
		outMax = *minmax.second;
	}

	void MetricsCollector::computeHistogram(uint32_t& out0to8, uint32_t& out8to16, uint32_t& out16to33,
											uint32_t& out33plus) const {
		out0to8 = 0;
		out8to16 = 0;
		out16to33 = 0;
		out33plus = 0;

		for (float sample : frameTimeSamples) {
			if (sample < 8.0F) {
				out0to8++;
			} else if (sample < 16.67F) {
				out8to16++;
			} else if (sample < 33.33F) {
				out16to33++;
			} else {
				out33plus++;
			}
		}
	}

	float MetricsCollector::compute1PercentLow() const {
		if (frameTimeSamples.empty()) {
			return 0.0F;
		}

		// Use nth_element for O(n) instead of O(n log n) sort
		std::vector<float> samples = frameTimeSamples;

		// 99th percentile index (1% from the top = worst frames)
		size_t index = static_cast<size_t>(samples.size() * 0.99F);
		if (index >= samples.size()) {
			index = samples.size() - 1;
		}

		std::nth_element(samples.begin(), samples.begin() + static_cast<ptrdiff_t>(index), samples.end());
		return samples[index];
	}

	uint32_t MetricsCollector::countSpikes(float thresholdMs) const {
		uint32_t count = 0;
		for (float sample : frameTimeSamples) {
			if (sample > thresholdMs) {
				count++;
			}
		}
		return count;
	}

} // namespace Renderer
