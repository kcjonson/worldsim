#include "Attributes.h"

#include "../RandomDraw.h"

namespace ecs {

	Attributes rollAttributes(const AttributeRolls& rolls, std::mt19937& rng) {
		Attributes attributes{};
		for (std::size_t index = 0; index < kAttributeFields.size(); ++index) {
			const assets::AttributeRange& range = rolls.ranges[index];
			attributes.*(kAttributeFields[index].value) = static_cast<float>(static_cast<double>(range.min) + drawUnit(rng) * static_cast<double>(range.max - range.min));
		}
		return attributes;
	}

} // namespace ecs
