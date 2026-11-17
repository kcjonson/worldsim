#include "NeedLevels.h"

#include <algorithm>
#include <string_view>

namespace ecs {

	std::vector<std::string> NeedLevels::load(const assets::NeedsConfig& config) {
		std::vector<std::string>										  errors;
		std::array<Thresholds, static_cast<std::size_t>(NeedType::Count)> loaded{};
		std::array<bool, static_cast<std::size_t>(NeedType::Count)>		  given{};
		for (const assets::NeedThresholdsDef& def : config.needs) {
			const auto found = std::ranges::find(kNeedLabels, std::string_view(def.need), [](const char* label) { return std::string_view(label); });
			if (found == kNeedLabels.end()) {
				errors.push_back("needs.xml: no need is called '" + def.need + "'");
				continue;
			}
			const auto index = static_cast<std::size_t>(found - kNeedLabels.begin());
			loaded[index] = def.thresholds;
			given[index] = true;
		}
		for (std::size_t index = 0; index < given.size(); ++index) {
			if (!given[index]) {
				errors.push_back(std::string("needs.xml: no <Need name=\"") + kNeedLabels[index] + "\">");
			}
		}
		if (errors.empty()) {
			thresholds = loaded;
		}
		return errors;
	}

	std::size_t NeedLevels::levelOf(NeedType need, float value) const {
		const Thresholds& needThresholds = thresholdsOf(need);
		return static_cast<std::size_t>(std::ranges::count_if(needThresholds, [value](float threshold) { return value < threshold; }));
	}

} // namespace ecs
