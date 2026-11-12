#pragma once

#include <optional>

namespace ecs {

	/// A walk's answer: its length in metres; or none (unreachable, or no walk within the bound asked);
	/// or far, when the search gave up before proving either. Far is never "no": a far target stays a
	/// candidate (nearestReachable) and reads as reachable to a yes/no question. Converts from a plain
	/// length or none.
	struct WalkAnswer {
		std::optional<float> walk;
		bool				 far = false;

		WalkAnswer(std::optional<float> length = std::nullopt)
			: walk(length) {}

		[[nodiscard]] static WalkAnswer farAway() {
			WalkAnswer answer;
			answer.far = true;
			return answer;
		}

		[[nodiscard]] bool reachable() const { return walk.has_value() || far; }
	};

} // namespace ecs
