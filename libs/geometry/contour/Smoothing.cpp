#include "Smoothing.h"

#include "ContourDetail.h"

#include <cstddef>
#include <utility>

namespace geometry {

	namespace {

		using contour_detail::floorDiv;

		// (3a + b) / 4, nearest mm, halves up.
		Vec2i64 quarterPoint(const Vec2i64& a, const Vec2i64& b) {
			return {floorDiv(3 * a.x + b.x + 2, 4), floorDiv(3 * a.y + b.y + 2, 4)};
		}

	} // namespace

	void chaikin(Ring& ring, int iterations) {
		if (ring.size() < 3) {
			return;
		}
		for (int pass = 0; pass < iterations; ++pass) {
			const std::size_t n = ring.size();
			Ring			  out;
			out.reserve(n * 2);
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64& a = ring[i];
				const Vec2i64& b = ring[(i + 1) % n];
				out.push_back(quarterPoint(a, b));
				out.push_back(quarterPoint(b, a));
			}
			// Edges under 2 mm can round both cut points onto one integer.
			contour_detail::dropConsecutiveDuplicates(out);
			ring = std::move(out);
		}
	}

} // namespace geometry
