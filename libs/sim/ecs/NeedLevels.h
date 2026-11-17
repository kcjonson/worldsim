#pragma once

// The levels each need moves between, from its thresholds in data (assets/config/colonists/needs.xml).
// See /docs/technical/interruptable-crafts.md (Needs in three levels).

#include "components/Needs.h"

#include <assets/NeedsConfig.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ecs {

	class NeedLevels {
	  public:
		using Thresholds = std::array<float, assets::kNeedThresholdNames.size()>;

		/// Replaces every need's thresholds with `config`'s. Returns every error: a name that is no need, a
		/// need the config leaves out. Nothing is kept when there are errors.
		std::vector<std::string> load(const assets::NeedsConfig& config);

		/// 0 at or above the need's first threshold, and one more below each threshold after.
		[[nodiscard]] std::size_t levelOf(NeedType need, float value) const;

		[[nodiscard]] const Thresholds& thresholdsOf(NeedType need) const { return thresholds[static_cast<std::size_t>(need)]; }

	  private:
		std::array<Thresholds, static_cast<std::size_t>(NeedType::Count)> thresholds{};
	};

} // namespace ecs
