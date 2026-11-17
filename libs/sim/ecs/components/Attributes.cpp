#include "Attributes.h"

namespace ecs {

	Attributes rollAttributes(const AttributeRolls& rolls, std::mt19937& rng) {
		constexpr double kEngineRange = 4294967296.0; // 2^32, the span of mt19937's output
		Attributes		 attributes{};
		for (std::size_t index = 0; index < kAttributeFields.size(); ++index) {
			const assets::AttributeRange& range = rolls.ranges[index];
			const double				  unit = static_cast<double>(rng()) / kEngineRange;
			attributes.*(kAttributeFields[index].value) = static_cast<float>(static_cast<double>(range.min) + unit * static_cast<double>(range.max - range.min));
		}
		return attributes;
	}

} // namespace ecs
