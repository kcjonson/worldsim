#pragma once

#include "Needs.h"

#include <algorithm>
#include <array>

namespace ecs {

/// Configurable mood weighting for needs. Keep centralized so tuning is easy.
struct MoodWeights {
	std::array<float, static_cast<size_t>(NeedType::Count)> needWeights{};

	static MoodWeights Default() {
		MoodWeights w{};
		// Comfort / social drivers carry heavier mood impact
		w.needWeights[static_cast<size_t>(NeedType::Hygiene)] = 1.0F;
		w.needWeights[static_cast<size_t>(NeedType::Temperature)] = 1.0F;
		w.needWeights[static_cast<size_t>(NeedType::Recreation)] = 0.9F;

		// Core survival
		w.needWeights[static_cast<size_t>(NeedType::Energy)] = 0.7F;
		w.needWeights[static_cast<size_t>(NeedType::Hunger)] = 0.6F;
		w.needWeights[static_cast<size_t>(NeedType::Thirst)] = 0.6F;

		// Bodily functions (lighter unless accidents happen)
		w.needWeights[static_cast<size_t>(NeedType::Bladder)] = 0.3F;
		w.needWeights[static_cast<size_t>(NeedType::Digestion)] = 0.3F;

		return w;
	}
};

/// Compute penalty curve for a single need (0-1), with steeper drop below ~30%.
inline float needPenalty(float value) {
	constexpr float comfortable = 70.0F;
	constexpr float warning = 30.0F;
	if (value >= comfortable) {
		return 0.0F;
	}
	if (value >= warning) {
		float t = (comfortable - value) / (comfortable - warning); // 0..1
		return t * 0.3F; // mild penalty up to 0.3
	}
	float t = (warning - std::max(value, 0.0F)) / warning; // 0..1
	return 0.3F + t * 0.7F; // steeper drop to 1.0
}

/// Aggregate mood (0-100) from all needs using configured weights.
inline float computeMood(const NeedsComponent& needs, const MoodWeights& weights = MoodWeights::Default()) {
	float weightedPenalty = 0.0F;
	float totalWeight = 0.0F;

	for (size_t i = 0; i < static_cast<size_t>(NeedType::Count); ++i) {
		float weight = weights.needWeights[i];
		if (weight <= 0.0F) {
			continue;
		}
		totalWeight += weight;
		const auto& need = needs.needs[i];
		weightedPenalty += weight * needPenalty(need.value);
	}

	if (totalWeight <= 0.0F) {
		return 100.0F;
	}

	float normalized = weightedPenalty / totalWeight; // 0..1
	float mood = 100.0F * (1.0F - normalized);
	if (mood < 0.0F) mood = 0.0F;
	if (mood > 100.0F) mood = 100.0F;
	return mood;
}

}  // namespace ecs
