#include "assets/TurnQuanta.h"

#include <cmath>
#include <numbers>

namespace assets {

	uint32_t turnQuantaFromDegrees(double degrees) {
		const auto		count = static_cast<long long>(kTurnQuantaPerTurn);
		const long long rounded = std::llround(degrees / kTurnQuantumDegrees);
		return static_cast<uint32_t>(((rounded % count) + count) % count);
	}

	float turnRadiansFromQuanta(uint32_t turnQuanta) {
		return static_cast<float>(static_cast<double>(turnQuanta) * kTurnQuantumDegrees * std::numbers::pi / 180.0);
	}

	uint32_t turnQuantaFromRadians(float radians) {
		return turnQuantaFromDegrees(static_cast<double>(radians) * 180.0 / std::numbers::pi);
	}

} // namespace assets
