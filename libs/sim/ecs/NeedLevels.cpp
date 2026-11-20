#include "NeedLevels.h"

#include <algorithm>
#include <string_view>

namespace ecs {

	std::vector<std::string> NeedLevels::load(const assets::NeedsConfig& config, const std::filesystem::path& folder) {
		std::vector<std::string>										  errors;
		std::array<Thresholds, static_cast<std::size_t>(NeedType::Count)> loaded{};
		std::array<float, static_cast<std::size_t>(NeedType::Count)>	  loadedMoodCosts{};
		std::array<bool, static_cast<std::size_t>(NeedType::Count)>		  given{};
		for (const assets::NeedDef& def : config.needs) {
			const auto found =
				std::ranges::find(kNeedLabels, std::string_view(def.need), [](const char* label) { return std::string_view(label); });
			if (found == kNeedLabels.end()) {
				errors.push_back(def.source + ": <Need name=\"" + def.need + "\"> names no need");
				continue;
			}
			const auto index = static_cast<std::size_t>(found - kNeedLabels.begin());
			loaded[index] = def.thresholds;
			loadedMoodCosts[index] = def.moodCost;
			given[index] = true;
		}
		for (std::size_t index = 0; index < given.size(); ++index) {
			if (!given[index]) {
				errors.push_back(folder.generic_string() + ": no file gives <Need name=\"" + kNeedLabels[index] + "\">");
			}
		}
		if (errors.empty()) {
			thresholds = loaded;
			moodCosts = loadedMoodCosts;
		}
		return errors;
	}

} // namespace ecs
