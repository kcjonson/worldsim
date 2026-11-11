#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ecs {

	/// Lays `quantity` loose units of `defName` on the ground at (x, y). Every loose drop goes
	/// through one of these: the app lands them as ResourceStack piles, merged into same-def piles
	/// already at the spot, hauled away later.
	using DropResourceCallback = std::function<void(const std::string& defName, float x, float y, uint32_t quantity)>;

} // namespace ecs
