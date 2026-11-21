#include "ForestShape.h"

#include "../polygon/Polygon.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

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

		// The kept sides, with room for the scan's closing repeat of its first side.
		using SideList = std::array<std::size_t, kSightSides + 1>;
		using VertexList = std::array<Vec2i64, kSightSides>;
		using HullList = std::array<Vec2i64, 2 * kSightSides>;

		std::int64_t floorDivide(std::int64_t numerator, std::int64_t denominator) {
			assert(denominator > 0);
			const std::int64_t quotient = numerator / denominator;
			return quotient * denominator > numerator ? quotient - 1 : quotient;
		}

		std::int64_t roundDivide(std::int64_t numerator, std::int64_t denominator) {
			return floorDivide(2 * numerator + denominator, 2 * denominator);
		}

		std::int64_t normalCross(std::size_t first, std::size_t second) {
			const auto& [firstX, firstY] = kSightSideNormals[first];
			const auto& [secondX, secondY] = kSightSideNormals[second];
			return firstX * secondY - firstY * secondX;
		}

		// The turn at the middle side's dual point n / h between the sides before and after it, times
		// the product of the three offsets h: positive when the middle side's edge lies on the shape
		// between the other two, zero or negative when their edges' crossing already lies in its
		// half-plane.
		Int128 dualTurn(const ForestHalfPlanes& halfPlanes, std::size_t before, std::size_t middle, std::size_t after) {
			const auto& offsets = halfPlanes.offsets;
			return Int128::product(offsets[before], normalCross(middle, after)) +
				   Int128::product(offsets[middle], normalCross(after, before)) +
				   Int128::product(offsets[after], normalCross(before, middle));
		}

		// The sides whose edges bound the shape, in order round it, into `kept`; returns how many. A
		// Graham scan of the dual points, which share the normals' angular order round the point,
		// started from the side with the least offset. That side's edge is always kept: any other two
		// edges cross at least 1 / cos(2 x pi / kSightSides) - 1 of its offset beyond it, against
		// rounding of a few parts in 2^22.
		std::size_t keptSides(const ForestHalfPlanes& halfPlanes, SideList& kept) {
			const auto&		  offsets = halfPlanes.offsets;
			const std::size_t start = static_cast<std::size_t>(std::min_element(offsets.begin(), offsets.end()) - offsets.begin());
			std::size_t		  count = 0;
			for (std::size_t step = 0; step <= kSightSides; ++step) {
				const std::size_t side = (start + step) % kSightSides;
				while (count >= 2 && dualTurn(halfPlanes, kept[count - 2], kept[count - 1], side) <= Int128(0)) {
					--count;
				}
				kept[count++] = side;
			}
			return count - 1;
		}

		// The greatest squared length of a side normal.
		constexpr std::int64_t kNormalMostSquared = [] {
			std::int64_t most = 0;
			for (const auto& [normalX, normalY] : kSightSideNormals) {
				most = std::max(most, normalX * normalX + normalY * normalY);
			}
			return most;
		}();

		// How far below kSightDirectionScale the window's cosine bound is pulled, for the normals'
		// length spread (under 2^-21 of their scale) with room to spare.
		constexpr std::int64_t kWindowCosineMargin = 1024;

		bool outsideSide(const ForestHalfPlanes& halfPlanes, const Vec2i64& offset, std::size_t side) {
			const auto& [normalX, normalY] = kSightSideNormals[side];
			return offset.x * normalX + offset.y * normalY > halfPlanes.offsets[side];
		}

		bool insideOffsets(const ForestHalfPlanes& halfPlanes, const Vec2i64& offset) {
			if (std::abs(offset.x) >= kOffsetLimitMm || std::abs(offset.y) >= kOffsetLimitMm) {
				return false;
			}
			const std::int64_t lengthSquared = offset.x * offset.x + offset.y * offset.y;
			if (Int128::product(lengthSquared, kNormalMostSquared) <= Int128::product(halfPlanes.leastOffset, halfPlanes.leastOffset)) {
				return true;
			}
			const std::size_t nearestSide = sightDirectionAtOrBefore(offset);
			if (outsideSide(halfPlanes, offset, nearestSide)) {
				return false;
			}
			for (std::size_t apart = 1; apart <= halfPlanes.window; ++apart) {
				if (outsideSide(halfPlanes, offset, (nearestSide + apart) % kSightSides) ||
					outsideSide(halfPlanes, offset, (nearestSide + kSightSides - apart) % kSightSides)) {
					return false;
				}
			}
			return true;
		}

		// The vertex for the crossing of the edges of sides `first` and `second`, relative to the
		// point. The crossing is (numeratorX, numeratorY) / determinant by Cramer's rule: numerators
		// under 2^61, the determinant under 2^45 and positive for edges less than half a turn apart.
		// It is the nearest of the crossing's four integer neighbours in every half-plane, or else the
		// first point in every half-plane on the line from the crossing rounded to nearest to the
		// point. Every half-plane holds the disc of radius r round the point, so a point a fraction t
		// of the way from the crossing to the point clears every edge by t x r; a step there is inside
		// once t x r passes the sqrt(2) mm its two roundings can take, and the line ends at the point,
		// which is inside.
		Vec2i64 placedVertex(const ForestHalfPlanes& halfPlanes, std::size_t first, std::size_t second) {
			const auto& [firstX, firstY] = kSightSideNormals[first];
			const auto& [secondX, secondY] = kSightSideNormals[second];
			const std::int64_t determinant = firstX * secondY - firstY * secondX;
			assert(determinant > 0);
			const std::int64_t numeratorX = halfPlanes.offsets[first] * secondY - halfPlanes.offsets[second] * firstY;
			const std::int64_t numeratorY = firstX * halfPlanes.offsets[second] - secondX * halfPlanes.offsets[first];
			const Vec2i64	   below{floorDivide(numeratorX, determinant), floorDivide(numeratorY, determinant)};
			const std::int64_t remainderX = numeratorX - below.x * determinant;
			const std::int64_t remainderY = numeratorY - below.y * determinant;

			bool	found = false;
			Vec2i64 nearest;
			Int128	nearestApart(0);
			for (std::int64_t stepX = 0; stepX <= 1; ++stepX) {
				for (std::int64_t stepY = 0; stepY <= 1; ++stepY) {
					const Vec2i64	   candidate{below.x + stepX, below.y + stepY};
					const std::int64_t apartX = stepX == 0 ? remainderX : determinant - remainderX;
					const std::int64_t apartY = stepY == 0 ? remainderY : determinant - remainderY;
					const Int128	   apart = Int128::product(apartX, apartX) + Int128::product(apartY, apartY);
					if ((!found || apart < nearestApart) && insideOffsets(halfPlanes, candidate)) {
						found = true;
						nearest = candidate;
						nearestApart = apart;
					}
				}
			}
			if (found) {
				return nearest;
			}

			const Vec2i64	   rounded{roundDivide(numeratorX, determinant), roundDivide(numeratorY, determinant)};
			const std::int64_t steps = std::max(std::abs(rounded.x), std::abs(rounded.y));
			assert(steps > 0);
			Vec2i64		 candidate;
			std::int64_t step = 0;
			do {
				++step;
				candidate = {roundDivide(rounded.x * (steps - step), steps), roundDivide(rounded.y * (steps - step), steps)};
			} while (!insideOffsets(halfPlanes, candidate));
			return candidate;
		}

	} // namespace

	ForestHalfPlanes forestHalfPlanes(const SightSideValues& ranges) {
		ForestHalfPlanes halfPlanes;
		for (std::size_t side = 0; side < kSightSides; ++side) {
			assert(ranges[side] >= kForestShapeLeastRangeMm && ranges[side] < kForestRadiusLimitMm);
			halfPlanes.offsets[side] = ranges[side] * kSightCosHalfSide / kOffsetDivisor;
		}
		halfPlanes.leastOffset = *std::min_element(halfPlanes.offsets.begin(), halfPlanes.offsets.end());
		halfPlanes.mostOffset  = *std::max_element(halfPlanes.offsets.begin(), halfPlanes.offsets.end());
		// Sides (apart - 1) steps of the table off the near side's direction can cut while cos of that
		// exceeds the bound; the last such gives the window.
		const Int128 bound = Int128::product(halfPlanes.leastOffset, kSightCosHalfSide - kWindowCosineMargin);
		std::size_t	 window = 1;
		while (window < kSightSides / 2 && Int128::product(kSightDirections[window][0], halfPlanes.mostOffset) > bound) {
			++window;
		}
		halfPlanes.window = window;
		return halfPlanes;
	}

	bool insideForestHalfPlanes(const Vec2i64& point, const ForestHalfPlanes& halfPlanes, const Vec2i64& query) {
		return insideOffsets(halfPlanes, query - point);
	}

	Ring forestShape(const Vec2i64& point, const SightSideValues& ranges) {
		const ForestHalfPlanes halfPlanes = forestHalfPlanes(ranges);
		SideList			   kept;
		const std::size_t	   keptCount = keptSides(halfPlanes, kept);
		VertexList			   vertices;
		for (std::size_t index = 0; index < keptCount; ++index) {
			vertices[index] = placedVertex(halfPlanes, kept[(index + keptCount - 1) % keptCount], kept[index]);
		}
		HullList		  hull;
		const std::size_t hullCount = convexHullInto(vertices.data(), keptCount, hull.data());
		assert(hullCount >= 3);
		std::size_t		  first = 0;
		for (std::size_t index = 0; index < hullCount; ++index) {
			if (inUpperHalfTurn(hull[index]) && !inUpperHalfTurn(hull[(index + hullCount - 1) % hullCount])) {
				first = index;
				break;
			}
		}
		Ring shape;
		shape.reserve(hullCount);
		for (std::size_t index = 0; index < hullCount; ++index) {
			shape.push_back(hull[(first + index) % hullCount] + point);
		}
		return shape;
	}

} // namespace geometry
