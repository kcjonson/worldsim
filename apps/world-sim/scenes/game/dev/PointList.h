#pragma once

// The 'x,y;x,y;...' list a dev verb's pts= carries, and the 400's message for a list with too few points.

#include <math/Types.h> // Foundation::Vec2

#include <cstddef>
#include <string>
#include <vector>

namespace world_sim {

	/// The points of a list in world metres, in order. A pair that is not two numbers is skipped.
	std::vector<Foundation::Vec2> parsePointList(const std::string& spec);

	/// Empty when `got` reaches `least`; otherwise the message naming the verb and how many points it needs.
	std::string pointCountRefusal(const std::string& verb, std::size_t least, std::size_t got);

} // namespace world_sim
