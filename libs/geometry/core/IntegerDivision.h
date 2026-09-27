#pragma once

#include <cassert>
#include <cstdint>

// Integer division rounded toward negative infinity. C++ `/` truncates toward
// zero, which puts world-mm coordinates on either side of zero in the same cell;
// these give the lattice cell, tile, or chunk a coordinate falls in.

namespace geometry {

	// floor(a / b) for b > 0.
	inline std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
		assert(b > 0);
		const std::int64_t q = a / b;
		return (a % b != 0 && a < 0) ? q - 1 : q;
	}

	// ceil(a / b) for b > 0.
	inline std::int64_t ceilDiv(std::int64_t a, std::int64_t b) {
		assert(b > 0);
		const std::int64_t q = a / b;
		return (a % b != 0 && a > 0) ? q + 1 : q;
	}

} // namespace geometry
