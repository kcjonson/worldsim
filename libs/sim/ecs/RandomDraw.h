#pragma once

// Draws on raw mt19937 output. The standard distributions differ between standard libraries, these
// don't, so a seed gives the same colonist everywhere.

#include <cstddef>
#include <cstdint>
#include <random>

namespace ecs {

	/// Evenly in [0, 1).
	[[nodiscard]] inline double drawUnit(std::mt19937& rng) {
		constexpr double kEngineRange = 4294967296.0; // 2^32, the span of mt19937's output
		return static_cast<double>(rng()) / kEngineRange;
	}

	/// Evenly in [0, count), count at least 1 and at most 2^32 (multiply-shift, rejecting the biased low part).
	[[nodiscard]] inline std::size_t drawIndex(std::mt19937& rng, std::size_t count) {
		const auto	   span = static_cast<std::uint32_t>(count);
		std::uint64_t  product = static_cast<std::uint64_t>(rng()) * span;
		auto		   low = static_cast<std::uint32_t>(product);
		if (low < span) {
			const std::uint32_t threshold = (0U - span) % span;
			while (low < threshold) {
				product = static_cast<std::uint64_t>(rng()) * span;
				low = static_cast<std::uint32_t>(product);
			}
		}
		return static_cast<std::size_t>(product >> 32);
	}

} // namespace ecs
