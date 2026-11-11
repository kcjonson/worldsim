#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <glm/vec2.hpp>

namespace ecs {

	/// Lays `quantity` loose units of `defName` on the ground at (x, y). Every loose drop goes
	/// through one of these: the app lands them as ResourceStack piles, merged into same-def piles
	/// already at the spot, hauled away later. Piles go only on valid ground off every unbuilt
	/// footprint, searched outward from (x, y); when none lies within the search, they land at
	/// `fallback` (where the goods came from), searched outward the same way first. A drop at
	/// the dropper's feet passes (x, y) as its fallback.
	using DropResourceCallback = std::function<void(const std::string& defName, float x, float y, uint32_t quantity, glm::vec2 fallback)>;

} // namespace ecs
