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

		return metrics;
	}

	void MetricsCollector::setRenderStats(uint32_t drawCalls, uint32_t vertexCount, uint32_t triangleCount) {
		drawCalls = drawCalls;
		vertexCount = vertexCount;
		triangleCount = triangleCount;
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

} // namespace Renderer
