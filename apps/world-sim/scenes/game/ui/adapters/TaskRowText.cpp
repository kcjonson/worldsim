#include "TaskRowText.h"

#include <format>

namespace world_sim::adapters {

	std::string joinNames(std::span<const std::string> names) {
		constexpr std::size_t kShown = 2;
		std::string			  joined;
		for (std::size_t index = 0; index < names.size() && index < kShown; ++index) {
			joined += index == 0 ? names[index] : ", " + names[index];
		}
		if (names.size() > kShown) {
			joined += std::format(" +{}", names.size() - kShown);
		}
		return joined;
	}

	std::string quantityText(uint32_t needed, uint32_t onTheWay) {
		if (needed > 0 && onTheWay > 0) {
			return std::format("needs {}, {} on the way", needed, onTheWay);
		}
		if (needed > 0) {
			return std::format("needs {}", needed);
		}
		if (onTheWay > 0) {
			return std::format("{} on the way", onTheWay);
		}
		return {};
	}

} // namespace world_sim::adapters
