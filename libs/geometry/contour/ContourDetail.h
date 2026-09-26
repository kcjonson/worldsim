#pragma once

#include "../core/Int128.h"
#include "../polygon/Polygon.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// Internal helpers shared by the contour sources. Every rounding rule here is a
// function of the exact rational value alone, which is what makes contour
// vertices independent of edge direction and field origin.

namespace geometry::contour_detail {

	// floor(a / b) for b > 0.
	inline std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
		assert(b > 0);
		const std::int64_t q = a / b;
		return (a % b != 0 && a < 0) ? q - 1 : q;
	}

	// num / den rounded to the nearest integer, halves toward +infinity:
	// floor((2 * num + den) / (2 * den)), den > 0. Exact for any 128-bit numerator
	// whose quotient fits int64: a double estimate is corrected with exact
	// comparisons.
	inline std::int64_t roundDivHalfUp(const Int128& num, std::int64_t den) {
		assert(den > 0 && den <= INT64_MAX / 2);
		const std::int64_t twoDen = 2 * den;
		const Int128	   target = num + num + Int128(den); // q is the largest integer with twoDen * q <= target
		auto			   q	  = static_cast<std::int64_t>(std::floor(num.toDouble() / static_cast<double>(den) + 0.5));
		while (Int128::product(twoDen, q) > target) {
			--q;
		}
		while (Int128::product(twoDen, q + 1) <= target) {
			++q;
		}
		return q;
	}

	// Collapse runs of equal consecutive vertices (cyclically) to one. With a mask,
	// a collapsed vertex is flagged if any member of its run was. Compacts in
	// place: no per-vertex push_back, each of which takes MSVC's process-wide
	// debug-iterator lock in debug builds and serializes concurrent chunk workers.
	inline void dropConsecutiveDuplicates(Ring& ring, std::vector<std::uint8_t>* mask = nullptr) {
		assert(mask == nullptr || mask->size() == ring.size());
		std::size_t kept = 0;
		for (std::size_t i = 0; i < ring.size(); ++i) {
			const std::uint8_t flag = mask != nullptr ? (*mask)[i] : std::uint8_t{0};
			if (kept > 0 && ring[kept - 1] == ring[i]) {
				if (mask != nullptr) {
					(*mask)[kept - 1] = static_cast<std::uint8_t>((*mask)[kept - 1] | flag);
				}
				continue;
			}
			ring[kept] = ring[i];
			if (mask != nullptr) {
				(*mask)[kept] = flag;
			}
			++kept;
		}
		while (kept > 1 && ring[kept - 1] == ring[0]) {
			if (mask != nullptr) {
				(*mask)[0] = static_cast<std::uint8_t>((*mask)[0] | (*mask)[kept - 1]);
			}
			--kept;
		}
		ring.resize(kept);
		if (mask != nullptr) {
			mask->resize(kept);
		}
	}

} // namespace geometry::contour_detail
