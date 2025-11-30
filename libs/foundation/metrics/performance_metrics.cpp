// Performance metrics serialization implementation.

#include "metrics/performance_metrics.h"
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
		json << "\"triangleCount\":" << triangleCount;
		json << "}";

		return json.str();
	}

} // namespace Foundation
