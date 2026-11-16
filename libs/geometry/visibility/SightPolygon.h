#pragma once

// An observer's sight disc as a ground outline: a regular polygon inscribed in the circle of his
// sight, so it never reaches past what he sees. Built in integers from a table made at compile
// time (core/DirectionTable.h), so every toolchain draws the same polygon from the same centre and
// radius (SightPolygon.test.cpp pins the table). computeVisibilityPolygon takes its rim from it, so
// an observer's outdoor and indoor shapes share their vertices on the circle.

#include "../core/DirectionTable.h"
#include "../core/Int128.h"
#include "../core/Vec2i64.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace geometry {

	constexpr std::size_t  kSightSides			= 64;
	constexpr std::int64_t kSightDirectionScale = std::int64_t{1} << 30;

	constexpr std::array<std::array<std::int64_t, 2>, kSightSides> kSightDirections = directionTable<kSightSides>(kSightDirectionScale);

	// cos(pi / kSightSides) times kSightDirectionScale: a side's distance from the centre over the
	// radius.
	constexpr std::int64_t kSightCosHalfSide = cosineOfHalfSide<kSightSides>(kSightDirectionScale);

	// The sight polygon of `radius` round `centre`, counter-clockwise from the vertex along +x. Vertex
	// k is direction k x radius / kSightDirectionScale, truncated toward the centre and then pulled
	// in a millimetre at a time while it lies outside the circle, so every vertex is within `radius`.
	// The products stay in int64 below a radius of 2^32 mm.
	inline std::vector<Vec2i64> sightPolygon(const Vec2i64& centre, std::int64_t radius) {
		assert(radius > 0 && radius < (std::int64_t{1} << 32));
		const Int128		 radiusSquared = Int128::product(radius, radius);
		std::vector<Vec2i64> out;
		out.reserve(kSightSides);
		for (const auto& [cosine, sine] : kSightDirections) {
			Vec2i64 offset{cosine * radius / kSightDirectionScale, sine * radius / kSightDirectionScale};
			while (dot(offset, offset) > radiusSquared) {
				std::int64_t& larger = (offset.x < 0 ? -offset.x : offset.x) >= (offset.y < 0 ? -offset.y : offset.y) ? offset.x : offset.y;
				larger += larger > 0 ? -1 : 1;
			}
			out.push_back(centre + offset);
		}
		return out;
	}

	// The distance in whole millimetres from the centre that every side of sightPolygon(centre,
	// radius) lies beyond: radius x cos(pi / kSightSides), less the 2 mm the vertices' truncation
	// can take off.
	inline std::int64_t sightInradius(std::int64_t radius) {
		return radius * kSightCosHalfSide / kSightDirectionScale - 2;
	}

} // namespace geometry
