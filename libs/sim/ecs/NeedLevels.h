#pragma once

// Each need's thresholds and mood cost from data (assets/config/colonists/needs), which a colonist's
// NeedsComponent is built from. See /docs/technical/interruptable-crafts.md (Needs in three levels).

#include "components/Needs.h"

#include <assets/NeedsConfig.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ecs {

	class NeedLevels {
	  public:
		using Thresholds = std::array<float, assets::kNeedThresholdNames.size()>;

		/// Replaces every need's thresholds and mood cost with `config`'s, read from `folder`. Returns every
		/// error: a name that is no need (naming its file), a need the config leaves out (naming the folder).
		/// Nothing is kept when there are errors.
		std::vector<std::string> load(const assets::NeedsConfig& config, const std::filesystem::path& folder);

		[[nodiscard]] const Thresholds& thresholdsOf(NeedType need) const { return thresholds[static_cast<std::size_t>(need)]; }
		[[nodiscard]] float				moodCostOf(NeedType need) const { return moodCosts[static_cast<std::size_t>(need)]; }

	  private:
		std::array<Thresholds, static_cast<std::size_t>(NeedType::Count)> thresholds{};
		std::array<float, static_cast<std::size_t>(NeedType::Count)>	  moodCosts{};
	};

} // namespace ecs
