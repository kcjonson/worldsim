#include "ForestShape.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <vector>

namespace geometry {

	namespace {

		// A range under 2^16 mm times kSightCosHalfSide (2^30) is under 2^46; over this it is a
		// half-plane offset at the normals' scale, under 2^38, against a dot product of an offset
		// under 2^17 and a normal under 2^22 per axis, under 2^40.
		constexpr std::int64_t kOffsetDivisor = kSightDirectionScale / kSightSideNormalScale;
		static_assert(kSightDirectionScale == std::int64_t{1} << 30 && kSightSideNormalScale == std::int64_t{1} << 22);
		static_assert(kForestRadiusLimitMm <= std::int64_t{1} << 16);

		// Every forest shape lies within range / cos(pi / kSightSides) of its point, under this.
		constexpr std::int64_t kOffsetLimitMm = std::int64_t{1} << 17;

		using HalfPlaneOffsets = std::array<std::int64_t, kSightSides>;

		HalfPlaneOffsets halfPlaneOffsets(const SightSideValues& ranges) {
			HalfPlaneOffsets offsets{};
			for (std::size_t side = 0; side < kSightSides; ++side) {
				assert(ranges[side] > 0 && ranges[side] < kForestRadiusLimitMm);
				offsets[side] = ranges[side] * kSightCosHalfSide / kOffsetDivisor;
			}
			return offsets;
		}

		std::int64_t normalCross(std::size_t a, std::size_t b) {
			const auto& [ax, ay] = kSightSideNormals[a];
			const auto& [bx, by] = kSightSideNormals[b];
			return ax * by - ay * bx;
		}

		// The turn at side j's dual point n_j / h_j between sides i and k, times h_i h_j h_k: positive
		// when j's edge lies on the shape between i's and k's, zero or negative when the crossing of
		// i's and k's edges already lies in j's half-plane.
		Int128 dualTurn(const HalfPlaneOffsets& offsets, std::size_t i, std::size_t j, std::size_t k) {
			return Int128::product(offsets[i], normalCross(j, k)) + Int128::product(offsets[j], normalCross(k, i)) +
				   Int128::product(offsets[k], normalCross(i, j));
		}

		// The sides whose edges bound the shape, in order round it: a Graham scan of the dual points,
		// which share the normals' angular order round the point, started from the side with the
		// least range. That side's edge is always kept: any other two edges cross at least
		// 1 / cos(2 x pi / kSightSides) - 1 of its offset beyond it, against rounding of a few parts
		// in 2^22.
		std::vector<std::size_t> keptSides(const HalfPlaneOffsets& offsets, const SightSideValues& ranges) {
			const std::size_t		 start = static_cast<std::size_t>(std::min_element(ranges.begin(), ranges.end()) - ranges.begin());
			std::vector<std::size_t> kept;
			kept.reserve(kSightSides + 1);
			for (std::size_t step = 0; step <= kSightSides; ++step) {
				const std::size_t side = (start + step) % kSightSides;
				while (kept.size() >= 2 && dualTurn(offsets, kept[kept.size() - 2], kept.back(), side) <= Int128(0)) {
					kept.pop_back();
				}
				kept.push_back(side);
			}
			kept.pop_back();
			return kept;
		}

		bool insideOffsets(const HalfPlaneOffsets& offsets, const Vec2i64& offset) {
			if (std::abs(offset.x) >= kOffsetLimitMm || std::abs(offset.y) >= kOffsetLimitMm) {
				return false;
			}
			for (std::size_t side = 0; side < kSightSides; ++side) {
				const auto& [normalX, normalY] = kSightSideNormals[side];
				if (offset.x * normalX + offset.y * normalY > offsets[side]) {
					return false;
				}
			}
			return true;
		}

		// Where the edges of sides a and b cross, relative to the point, by Cramer's rule: numerators
		// under 2^61, the determinant under 2^45 and positive for edges less than half a turn apart.
		// Truncation rounds each coordinate toward the point.
		Vec2i64 crossing(const HalfPlaneOffsets& offsets, std::size_t a, std::size_t b) {
			const auto& [ax, ay] = kSightSideNormals[a];
			const auto& [bx, by] = kSightSideNormals[b];
			const std::int64_t determinant = ax * by - ay * bx;
			assert(determinant > 0);
			return {(offsets[a] * by - offsets[b] * ay) / determinant, (ax * offsets[b] - bx * offsets[a]) / determinant};
		}

		bool inUpperHalf(const Vec2i64& offset) {
			return offset.y > 0 || (offset.y == 0 && offset.x > 0);
		}

		// The convex hull of `points`, counter-clockwise, with no collinear vertices.
		Ring convexHull(Ring points) {
			std::sort(points.begin(), points.end());
			points.erase(std::unique(points.begin(), points.end()), points.end());
			if (points.size() < 3) {
				return points;
			}
			Ring		hull(2 * points.size());
			std::size_t count = 0;
			for (const Vec2i64& point : points) {
				while (count >= 2 && orientation(hull[count - 2], hull[count - 1], point) != Orientation::CounterClockwise) {
					--count;
				}
				hull[count++] = point;
			}
			const std::size_t lowerCount = count + 1;
			for (std::size_t index = points.size() - 1; index-- > 0;) {
				while (count >= lowerCount &&
					   orientation(hull[count - 2], hull[count - 1], points[index]) != Orientation::CounterClockwise) {
					--count;
				}
				hull[count++] = points[index];
			}
			hull.resize(count - 1);
			return hull;
		}

	} // namespace

	bool insideForestHalfPlanes(const Vec2i64& point, const SightSideValues& ranges, const Vec2i64& query) {
		return insideOffsets(halfPlaneOffsets(ranges), query - point);
	}

	Ring forestShape(const Vec2i64& point, const SightSideValues& ranges) {
		const HalfPlaneOffsets		   offsets = halfPlaneOffsets(ranges);
		const std::vector<std::size_t> kept = keptSides(offsets, ranges);
		Ring						   vertices;
		vertices.reserve(kept.size());
		for (std::size_t index = 0; index < kept.size(); ++index) {
			Vec2i64 offset = crossing(offsets, kept[(index + kept.size() - 1) % kept.size()], kept[index]);
			while (!insideOffsets(offsets, offset)) {
				std::int64_t& larger = std::abs(offset.x) >= std::abs(offset.y) ? offset.x : offset.y;
				larger += larger > 0 ? -1 : 1;
			}
			vertices.push_back(offset);
		}
		Ring hull = convexHull(std::move(vertices));
		for (std::size_t index = 0; index < hull.size(); ++index) {
			if (inUpperHalf(hull[index]) && !inUpperHalf(hull[(index + hull.size() - 1) % hull.size()])) {
				std::rotate(hull.begin(), hull.begin() + static_cast<std::ptrdiff_t>(index), hull.end());
				break;
			}
		}
		for (Vec2i64& vertex : hull) {
			vertex = vertex + point;
		}
		return hull;
	}

} // namespace geometry
