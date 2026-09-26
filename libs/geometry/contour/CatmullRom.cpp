#include "CatmullRom.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace geometry {

	namespace {

		std::size_t stepsForSpan(const Vec2d& a, const Vec2d& b, double maxSpacingM) {
			const double steps = std::ceil(length(b - a) / maxSpacingM);
			return std::max<std::size_t>(1, static_cast<std::size_t>(steps));
		}

		Vec2d blend(const Vec2d& a, const Vec2d& b, double ta, double tb, double u) {
			return a * ((tb - u) / (tb - ta)) + b * ((u - ta) / (tb - ta));
		}

		// One centripetal Catmull-Rom span from p1 to p2, evaluated with the
		// Barry-Goldman pyramid. Knot intervals are sqrt of chord length.
		struct Span {
			Vec2d  p0, p1, p2, p3;
			double t1 = 0.0;
			double t2 = 0.0;
			double t3 = 0.0;

			Span(const Vec2d& a, const Vec2d& b, const Vec2d& c, const Vec2d& d)
				: p0(a),
				  p1(b),
				  p2(c),
				  p3(d) {
				t1 = std::sqrt(length(p1 - p0));
				t2 = t1 + std::sqrt(length(p2 - p1));
				t3 = t2 + std::sqrt(length(p3 - p2));
			}

			// f in [0, 1] runs from p1 to p2.
			Vec2d at(double f) const {
				constexpr double t0 = 0.0;
				const double	 u	= t1 + f * (t2 - t1);
				const Vec2d		 a1 = blend(p0, p1, t0, t1, u);
				const Vec2d		 a2 = blend(p1, p2, t1, t2, u);
				const Vec2d		 a3 = blend(p2, p3, t2, t3, u);
				const Vec2d		 b1 = blend(a1, a2, t0, t2, u);
				const Vec2d		 b2 = blend(a2, a3, t1, t3, u);
				return blend(b1, b2, t1, t2, u);
			}
		};

	} // namespace

	std::vector<CenterlineSample>
	sampleCatmullRom(std::span<const Vec2d> points, std::span<const double> halfWidthsM, double maxSpacingM) {
		assert(points.size() >= 2 && halfWidthsM.size() == points.size() && maxSpacingM > 0.0);
		const std::size_t last = points.size() - 1;

		std::size_t total = 1; // the final point
		for (std::size_t i = 0; i < last; ++i) {
			total += stepsForSpan(points[i], points[i + 1], maxSpacingM);
		}
		// Sized up front and written by index: see contour_detail::dropConsecutiveDuplicates
		// on why contour loops avoid push_back.
		std::vector<CenterlineSample> out(total);

		std::size_t k = 0;
		for (std::size_t i = 0; i < last; ++i) {
			const Vec2d& p1 = points[i];
			const Vec2d& p2 = points[i + 1];
			const Vec2d	 p0 = i > 0 ? points[i - 1] : p1 * 2.0 - p2;
			const Vec2d	 p3 = i + 1 < last ? points[i + 2] : p2 * 2.0 - p1;
			const Span	 span(p0, p1, p2, p3);

			const std::size_t n = stepsForSpan(p1, p2, maxSpacingM);
			out[k++]			= {p1, halfWidthsM[i], i, 0.0};
			for (std::size_t s = 1; s < n; ++s) {
				const double f = static_cast<double>(s) / static_cast<double>(n);
				out[k++]	   = {span.at(f), std::lerp(halfWidthsM[i], halfWidthsM[i + 1], f), i, f};
			}
		}
		out[k] = {points[last], halfWidthsM[last], last - 1, 1.0};
		return out;
	}

} // namespace geometry
