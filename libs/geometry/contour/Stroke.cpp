#include "Stroke.h"

#include "ContourDetail.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace geometry {

	namespace {

		Vec2d unit(const Vec2d& v) { return v * (1.0 / length(v)); }

		Vec2d leftNormal(const Vec2d& v) { return {-v.y, v.x}; }

		// Left unit normal of the centerline at point i, from p[i-1], p[i], p[i+1] only.
		Vec2d offsetNormal(std::span<const Vec2d> c, std::size_t i) {
			if (i == 0) {
				return leftNormal(unit(c[1] - c[0]));
			}
			const Vec2d incoming = unit(c[i] - c[i - 1]);
			if (i + 1 == c.size()) {
				return leftNormal(incoming);
			}
			const Vec2d	 bisector = incoming + unit(c[i + 1] - c[i]);
			const double len	  = length(bisector);
			if (len < 1e-12) {
				return leftNormal(incoming);
			}
			return leftNormal(bisector * (1.0 / len));
		}

		Vec2i64 quantizeMeters(const Vec2d& p) {
			constexpr auto kMm = static_cast<double>(kMillimetersPerMeter);
			return {std::llround(p.x * kMm), std::llround(p.y * kMm)};
		}

		double meanOffset(const StrokeArgs& args, std::size_t i) { return 0.5 * (args.leftOffsetM[i] + args.rightOffsetM[i]); }

		std::size_t capSteps(const StrokeArgs& args, StrokeCap cap, std::size_t i) {
			if (cap == StrokeCap::Butt) {
				return 0;
			}
			const double arcM = std::numbers::pi * meanOffset(args, i);
			return std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(arcM / args.capSpacingM)));
		}

		// Writes the interior vertices of a round cap around `center`, sweeping from
		// the bank at center + fromSide * fromOffset, through center + forward *
		// reach, to the bank at center - fromSide * toOffset. The two bank points
		// themselves are written by the bank loops.
		void writeRoundCap(
			Ring&		 out,
			std::size_t& k,
			std::size_t	 steps,
			const Vec2d& center,
			const Vec2d& forward,
			const Vec2d& fromSide,
			double		 fromOffset,
			double		 toOffset,
			double		 reach
		) {
			for (std::size_t s = 1; s < steps; ++s) {
				const double angle = std::numbers::pi * static_cast<double>(s) / static_cast<double>(steps);
				const double side  = std::cos(angle);
				const double sideR = side >= 0.0 ? fromOffset : toOffset;
				out[k++]		   = quantizeMeters(center + fromSide * (side * sideR) + forward * (std::sin(angle) * reach));
			}
		}

	} // namespace

	Ring strokePolyline(const StrokeArgs& args) {
		const std::span<const Vec2d> c = args.centerline;
		const std::size_t			 n = c.size();
		assert(n >= 2 && args.leftOffsetM.size() == n && args.rightOffsetM.size() == n);
		assert((args.startCap == StrokeCap::Butt && args.endCap == StrokeCap::Butt) || args.capSpacingM > 0.0);

		const std::size_t last		 = n - 1;
		const std::size_t startSteps = capSteps(args, args.startCap, 0);
		const std::size_t endSteps	 = capSteps(args, args.endCap, last);
		const std::size_t startCapN	 = startSteps > 0 ? startSteps - 1 : 0;
		const std::size_t endCapN	 = endSteps > 0 ? endSteps - 1 : 0;

		// Sized up front and written by index: see contour_detail::dropConsecutiveDuplicates
		// on why contour loops avoid push_back.
		Ring		out(2 * n + startCapN + endCapN);
		std::size_t k = 0;
		for (std::size_t i = 0; i < n; ++i) {
			out[k++] = quantizeMeters(c[i] - offsetNormal(c, i) * args.rightOffsetM[i]);
		}
		if (endSteps > 0) {
			const Vec2d normal = offsetNormal(c, last);
			const Vec2d forward{normal.y, -normal.x};
			writeRoundCap(
				out, k, endSteps, c[last], forward, -normal, args.rightOffsetM[last], args.leftOffsetM[last], meanOffset(args, last)
			);
		}
		for (std::size_t i = n; i-- > 0;) {
			out[k++] = quantizeMeters(c[i] + offsetNormal(c, i) * args.leftOffsetM[i]);
		}
		if (startSteps > 0) {
			const Vec2d normal = offsetNormal(c, 0);
			const Vec2d backward{-normal.y, normal.x};
			writeRoundCap(out, k, startSteps, c[0], backward, normal, args.leftOffsetM[0], args.rightOffsetM[0], meanOffset(args, 0));
		}
		assert(k == out.size());

		contour_detail::dropConsecutiveDuplicates(out);
		return out;
	}

	double localRadiusOfCurvatureM(const Vec2d& prev, const Vec2d& cur, const Vec2d& next) {
		const Vec2d	 a	   = cur - prev;
		const Vec2d	 b	   = next - cur;
		const double cross = a.x * b.y - a.y * b.x;
		if (cross == 0.0) {
			return a.x * b.x + a.y * b.y < 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
		}
		return length(a) * length(b) * length(next - prev) / (2.0 * std::abs(cross));
	}

} // namespace geometry
