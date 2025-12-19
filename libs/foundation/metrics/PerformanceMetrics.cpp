// Performance metrics serialization implementation.

#include "metrics/PerformanceMetrics.h"
#include <iomanip>
#include <sstream>

namespace Foundation {

	std::string PerformanceMetrics::toJSON() const {
		std::ostringstream json;
		json << std::fixed << std::setprecision(2);

		json << "{";
		json << "\"timestamp\":" << timestamp << ",";
		json << "\"fps\":" << fps << ",";
		json << "\"frameTimeMs\":" << frameTimeMs << ",";
		json << "\"frameTimeMinMs\":" << frameTimeMinMs << ",";
		json << "\"frameTimeMaxMs\":" << frameTimeMaxMs << ",";
		json << "\"drawCalls\":" << drawCalls << ",";
		json << "\"vertexCount\":" << vertexCount << ",";
		json << "\"triangleCount\":" << triangleCount << ",";
		// Timing breakdown
		json << "\"tileRenderMs\":" << tileRenderMs << ",";
		json << "\"entityRenderMs\":" << entityRenderMs << ",";
		json << "\"updateMs\":" << updateMs << ",";
		json << "\"tileCount\":" << tileCount << ",";
		json << "\"entityCount\":" << entityCount << ",";
		json << "\"visibleChunkCount\":" << visibleChunkCount << ",";
		// Histogram
		json << "\"histogram0to8ms\":" << histogram0to8ms << ",";
		json << "\"histogram8to16ms\":" << histogram8to16ms << ",";
		json << "\"histogram16to33ms\":" << histogram16to33ms << ",";
		json << "\"histogram33plusMs\":" << histogram33plusMs << ",";
		json << "\"histogramTotal\":" << histogramTotal << ",";
		// Spike detection
		json << "\"frameTime1PercentLow\":" << frameTime1PercentLow << ",";
		json << "\"spikeCount16ms\":" << spikeCount16ms << ",";
		json << "\"spikeCount33ms\":" << spikeCount33ms << ",";
		// ECS system timings
		json << "\"ecsSystems\":[";
		for (size_t i = 0; i < ecsSystems.size(); ++i) {
			if (i > 0) json << ",";
			json << "{\"name\":\"" << (ecsSystems[i].name ? ecsSystems[i].name : "Unknown")
				 << "\",\"durationMs\":" << ecsSystems[i].durationMs << "}";
		}
		json << "],";
		// GPU timing
		json << "\"gpuRenderMs\":" << gpuRenderMs << ",";
		// System resources
		json << "\"memoryUsedBytes\":" << memoryUsedBytes << ",";
		json << "\"memoryPeakBytes\":" << memoryPeakBytes << ",";
		json << "\"cpuUsagePercent\":" << cpuUsagePercent << ",";
		json << "\"cpuCoreCount\":" << cpuCoreCount << ",";
		json << "\"inputLatencyMs\":" << inputLatencyMs << ",";
		// Main loop timing breakdown
		json << "\"pollEventsMs\":" << pollEventsMs << ",";
		json << "\"inputHandleMs\":" << inputHandleMs << ",";
		json << "\"sceneUpdateMs\":" << sceneUpdateMs << ",";
		json << "\"sceneRenderMs\":" << sceneRenderMs << ",";
		json << "\"swapBuffersMs\":" << swapBuffersMs;
		json << "}";

		return json.str();
	}

} // namespace Foundation
