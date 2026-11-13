#pragma once

// TaskRowText - the pieces of a task row's detail line that are pure text, kept apart from the
// adapter so world-sim-tests can link them without the ECS.

#include <cstdint>
#include <span>
#include <string>

namespace world_sim::adapters {

	/// One or two names joined with ", "; more as the first two then "+N" ("Alice, Bob +2"); "" for none.
	[[nodiscard]] std::string joinNames(std::span<const std::string> names);

	/// "needs 12, 4 on the way", leaving out a part that is zero; "" when both are.
	[[nodiscard]] std::string quantityText(uint32_t needed, uint32_t onTheWay);

} // namespace world_sim::adapters
