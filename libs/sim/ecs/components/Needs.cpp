#include "Needs.h"

#include "../NeedLevels.h"

namespace ecs {

	namespace {
		/// Percent per game-minute, by NeedType. Temperature has no decay until the environment model plugs in.
		constexpr std::array<float, static_cast<std::size_t>(NeedType::Count)> kDecayRates = {
			0.08F,	// Hunger
			0.12F,	// Thirst
			0.05F,	// Energy
			0.03F,	// Bladder
			0.02F,	// Digestion: food takes longer to process than liquids
			0.015F, // Hygiene
			0.01F,	// Recreation
			0.0F,	// Temperature
		};
	} // namespace

	static_assert(std::tuple_size_v<NeedLevels::Thresholds> == 3, "a Need holds seek, urgent, and critical");

	NeedsComponent::NeedsComponent(const NeedLevels& levels) {
		for (std::size_t index = 0; index < needs.size(); ++index) {
			const auto					  need = static_cast<NeedType>(index);
			const NeedLevels::Thresholds& thresholds = levels.thresholdsOf(need);
			needs[index] = Need{
				.value = 100.0F,
				.decayRate = kDecayRates[index],
				.seekThreshold = thresholds[0],
				.urgentThreshold = thresholds[1],
				.criticalThreshold = thresholds[2],
				.moodCost = levels.moodCostOf(need),
			};
		}
	}

} // namespace ecs
