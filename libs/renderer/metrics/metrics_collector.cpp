// Metrics collector implementation.

#include "metrics/metrics_collector.h"
#include <algorithm>
#include <numeric>

namespace Renderer {

	MetricsCollector::MetricsCollector() {
		// Reserve space for 60 frames (1 second at 60 FPS)
		m_frameTimeSamples.resize(60, 16.67F); // Initialize with ~60 FPS
	}

	void MetricsCollector::BeginFrame() {
		m_frameStart = Clock::now();
	}

	void MetricsCollector::EndFrame() { // NOLINT(readability-convert-member-functions-to-static)
		auto  frameEnd = Clock::now();
		auto  duration = std::chrono::duration_cast<std::chrono::microseconds>(frameEnd - m_frameStart);
		float frameTimeMs = duration.count() / 1000.0F;

		// Store in rolling buffer
		m_frameTimeSamples[m_currentSampleIndex] = frameTimeMs;
		m_currentSampleIndex = (m_currentSampleIndex + 1) % m_frameTimeSamples.size();
	}

	Foundation::PerformanceMetrics MetricsCollector::GetCurrentMetrics() const {
		Foundation::PerformanceMetrics metrics;

		metrics.timestamp = GetCurrentTimestamp();

		// Use most recent frame time
		size_t lastIndex = (m_currentSampleIndex + m_frameTimeSamples.size() - 1) % m_frameTimeSamples.size();
		metrics.frameTimeMs = m_frameTimeSamples[lastIndex];
		metrics.fps = CalculateFPS(metrics.frameTimeMs);

		// Get min/max from sample window
		GetFrameTimeMinMax(metrics.frameTimeMinMs, metrics.frameTimeMaxMs);

		// Rendering stats
		metrics.drawCalls = m_drawCalls;
		metrics.vertexCount = m_vertexCount;
		metrics.triangleCount = m_triangleCount;

		return metrics;
	}

	void MetricsCollector::SetRenderStats(uint32_t drawCalls, uint32_t vertexCount, uint32_t triangleCount) {
		m_drawCalls = drawCalls;
		m_vertexCount = vertexCount;
		m_triangleCount = triangleCount;
	}

	uint64_t MetricsCollector::GetCurrentTimestamp() const { // NOLINT(readability-convert-member-functions-to-static)
		auto now = std::chrono::system_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
		return static_cast<uint64_t>(ms.count());
	}

	float MetricsCollector::CalculateFPS(float frameTimeMs) const { // NOLINT(readability-convert-member-functions-to-static)
		if (frameTimeMs < 0.001F) {
			return 0.0F; // Avoid division by zero
		}
		return 1000.0F / frameTimeMs;
	}

	void MetricsCollector::GetFrameTimeMinMax( // NOLINT(readability-convert-member-functions-to-static)
		float& outMin,
		float& outMax
	) const {
		if (m_frameTimeSamples.empty()) {
			outMin = outMax = 0.0F;
			return;
		}

		auto minmax = std::minmax_element(m_frameTimeSamples.begin(), m_frameTimeSamples.end());
		outMin = *minmax.first;
		outMax = *minmax.second;
	}

} // namespace Renderer
