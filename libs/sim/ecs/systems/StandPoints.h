#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <glm/vec2.hpp>
#include <optional>

namespace ecs {

	/// The spots a mover may stand on to work one target, nearest the mover first
	/// (NavigationSystem::standPointsBeside).
	struct StandPoints {
		static constexpr std::size_t	 kCapacity = 2;
		std::array<glm::vec2, kCapacity> points{};
		std::size_t						 count = 0;

		void push(glm::vec2 point) {
			assert(count < kCapacity);
			points[count++] = point;
		}
		[[nodiscard]] bool		  empty() const { return count == 0; }
		[[nodiscard]] std::size_t size() const { return count; }
		[[nodiscard]] glm::vec2	  operator[](std::size_t index) const {
			  assert(index < count);
			  return points[index];
		}
		[[nodiscard]] std::optional<glm::vec2> nearest() const { return count > 0 ? std::optional<glm::vec2>(points[0]) : std::nullopt; }
	};

} // namespace ecs
