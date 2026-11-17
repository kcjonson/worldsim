#include "visibility/ForestShape.h"

#include "predicates/Predicates.h"
#include "visibility/SightPolygon.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <vector>

using geometry::ForestBody;
using geometry::ForestTunables;
using geometry::kFractionOne;
using geometry::kSightSides;
using geometry::Ring;
using geometry::SightSideValues;
using geometry::Vec2i64;

namespace {

	constexpr std::int64_t kSightMm = 30000;
	constexpr double	   kPi = 3.14159265358979323846;

	SightSideValues equalRanges(std::int64_t rangeMm) {
		SightSideValues ranges{};
		ranges.fill(rangeMm);
		return ranges;
	}

	void expectStrictlyConvex(const Ring& ring) {
		ASSERT_GE(ring.size(), 3U);
		for (std::size_t index = 0; index < ring.size(); ++index) {
			EXPECT_EQ(
				geometry::orientation(ring[index], ring[(index + 1) % ring.size()], ring[(index + 2) % ring.size()]),
				geometry::Orientation::CounterClockwise
			) << "vertex "
			  << index;
		}
	}

	// Signed distance from `query` in to the edge from `from` to `to` of a counter-clockwise ring.
	double insideBy(const Vec2i64& from, const Vec2i64& to, const Vec2i64& query) {
		const double edgeX = static_cast<double>(to.x - from.x);
		const double edgeY = static_cast<double>(to.y - from.y);
		return (edgeX * static_cast<double>(query.y - from.y) - edgeY * static_cast<double>(query.x - from.x)) /
			   std::sqrt(edgeX * edgeX + edgeY * edgeY);
	}

	double leastInsideBy(const Ring& ring, const Vec2i64& query) {
		double least = std::numeric_limits<double>::max();
		for (std::size_t index = 0; index < ring.size(); ++index) {
			least = std::min(least, insideBy(ring[index], ring[(index + 1) % ring.size()], query));
		}
		return least;
	}

	// How far from `point` along the unit direction at `angle` the boundary of a convex
	// counter-clockwise ring round it lies.
	double reachAlong(const Ring& ring, const Vec2i64& point, double angle) {
		const double directionX = std::cos(angle);
		const double directionY = std::sin(angle);
		double		 least = std::numeric_limits<double>::max();
		for (std::size_t index = 0; index < ring.size(); ++index) {
			const Vec2i64& from = ring[index];
			const Vec2i64& to = ring[(index + 1) % ring.size()];
			const double   normalX = static_cast<double>(to.y - from.y);
			const double   normalY = -static_cast<double>(to.x - from.x);
			const double   facing = normalX * directionX + normalY * directionY;
			if (facing > 0.0) {
				least = std::min(
					least, (normalX * static_cast<double>(from.x - point.x) + normalY * static_cast<double>(from.y - point.y)) / facing
				);
			}
		}
		return least;
	}

	double sideAngle(std::size_t side) {
		return (static_cast<double>(side) + 0.5) * 2.0 * kPi / static_cast<double>(kSightSides);
	}

	std::int64_t squareWidthFactor(std::int64_t sideMm) {
		return geometry::forestWidthFactor(4 * sideMm, ForestTunables{});
	}

	std::vector<ForestBody> randomStand(std::mt19937& rng, const Vec2i64& eye) {
		std::vector<ForestBody> bodies;
		const std::int64_t		spacingMm = 2000 + static_cast<std::int64_t>(rng() % 6000);
		const std::int64_t		count = 80 * 80 * 1000000 / (spacingMm * spacingMm);
		const bool				clumped = rng() % 3 == 0;
		const Vec2i64			clump{static_cast<std::int64_t>(rng() % 30000) - 15000, static_cast<std::int64_t>(rng() % 30000) - 15000};
		for (std::int64_t index = 0; index < count; ++index) {
			Vec2i64 offset{static_cast<std::int64_t>(rng() % 80000) - 40000, static_cast<std::int64_t>(rng() % 80000) - 40000};
			if (clumped && index % 2 == 0) {
				offset = clump + Vec2i64{static_cast<std::int64_t>(rng() % 6000) - 3000, static_cast<std::int64_t>(rng() % 6000) - 3000};
			}
			if (offset.x * offset.x + offset.y * offset.y < 600 * 600) {
				continue;
			}
			bodies.push_back({eye + offset, squareWidthFactor(280 + static_cast<std::int64_t>(rng() % 800)), kFractionOne});
		}
		return bodies;
	}

	// 40 veteran trunks on a 0.5 m grid, the nearest to a point 10 m from the eye.
	std::vector<ForestBody> clumpOf40(const Vec2i64& eye, double angle) {
		const Vec2i64		 middle = eye + Vec2i64{std::llround(10000.0 * std::cos(angle)), std::llround(10000.0 * std::sin(angle))};
		std::vector<Vec2i64> grid;
		for (std::int64_t i = -8; i <= 8; ++i) {
			for (std::int64_t j = -8; j <= 8; ++j) {
				grid.push_back(middle + Vec2i64{i * 500, j * 500});
			}
		}
		std::sort(grid.begin(), grid.end(), [&](const Vec2i64& a, const Vec2i64& b) {
			const Vec2i64	   fromA = a - middle;
			const Vec2i64	   fromB = b - middle;
			const std::int64_t lengthA = fromA.x * fromA.x + fromA.y * fromA.y;
			const std::int64_t lengthB = fromB.x * fromB.x + fromB.y * fromB.y;
			return lengthA != lengthB ? lengthA < lengthB : a < b;
		});
		std::vector<ForestBody> bodies;
		for (std::size_t index = 0; index < 40; ++index) {
			bodies.push_back({grid[index], kFractionOne, kFractionOne});
		}
		return bodies;
	}

	// The most the forest shape takes from the slope-limited ranges in a clear direction: a side
	// whose own capped depth is 0, measured along the side's normal against range x cos(pi / 64).
	double worstClearLoss(const Vec2i64& eye, const std::vector<ForestBody>& bodies, const ForestTunables& tunables) {
		const SightSideValues raw = geometry::forestRawDepth(eye, kSightMm, bodies, tunables);
		const SightSideValues limited = geometry::forestLimitedDepth(raw, kSightMm, tunables);
		SightSideValues		  ranges{};
		for (std::size_t side = 0; side < kSightSides; ++side) {
			ranges[side] = kSightMm - limited[side];
		}
		const Ring shape = geometry::forestShape(eye, ranges);
		double	   worst = 0.0;
		for (std::size_t side = 0; side < kSightSides; ++side) {
			if (raw[side] == 0) {
				const double star = static_cast<double>(ranges[side]) * std::cos(kPi / static_cast<double>(kSightSides));
				worst = std::max(worst, star - reachAlong(shape, eye, sideAngle(side)));
			}
		}
		return worst;
	}

} // namespace

// The side normals, made at compile time, are the same on every toolchain.
TEST(ForestShape, SideNormalsArePinned) {
	static_assert(geometry::kSightSides == 64, "the pinned table is for kSightSides; pin the new table when it changes");
	constexpr std::array<std::array<std::int64_t, 2>, 64> kPinned{
		{{4189252, 205805},	   {4148907, 615432},	 {4068606, 1019133},   {3949122, 1413018},	 {3791606, 1793296},   {3597575, 2156303},
		 {3368897, 2498544},   {3107774, 2816722},	 {2816722, 3107774},   {2498544, 3368897},	 {2156303, 3597575},   {1793296, 3791606},
		 {1413018, 3949122},   {1019133, 4068606},	 {615432, 4148907},	   {205805, 4189252},	 {-205805, 4189252},   {-615432, 4148907},
		 {-1019133, 4068606},  {-1413018, 3949122},	 {-1793296, 3791606},  {-2156303, 3597575},	 {-2498544, 3368897},  {-2816722, 3107774},
		 {-3107774, 2816722},  {-3368897, 2498544},	 {-3597575, 2156303},  {-3791606, 1793296},	 {-3949122, 1413018},  {-4068606, 1019133},
		 {-4148907, 615432},   {-4189252, 205805},	 {-4189252, -205805},  {-4148907, -615432},	 {-4068606, -1019133}, {-3949122, -1413018},
		 {-3791606, -1793296}, {-3597575, -2156303}, {-3368897, -2498544}, {-3107774, -2816722}, {-2816722, -3107774}, {-2498544, -3368897},
		 {-2156303, -3597575}, {-1793296, -3791606}, {-1413018, -3949122}, {-1019133, -4068606}, {-615432, -4148907},  {-205805, -4189252},
		 {205805, -4189252},   {615432, -4148907},	 {1019133, -4068606},  {1413018, -3949122},	 {1793296, -3791606},  {2156303, -3597575},
		 {2498544, -3368897},  {2816722, -3107774},	 {3107774, -2816722},  {3368897, -2498544},	 {3597575, -2156303},  {3791606, -1793296},
		 {3949122, -1413018},  {4068606, -1019133},	 {4148907, -615432},   {4189252, -205805}}
	};
	EXPECT_EQ(geometry::kSightSideNormals, kPinned);
}

// With every range equal the forest shape is the sight polygon of that radius, vertex for vertex, to
// 2 mm.
TEST(ForestShape, EqualRangesGiveTheSightPolygon) {
	std::mt19937			  rng(64);
	std::vector<std::int64_t> radii{1000, 1999, 20100, 29950, 30000, 32200, 38000, 65535};
	for (int i = 0; i < 60; ++i) {
		radii.push_back(1000 + static_cast<std::int64_t>(rng() % 64000));
	}
	for (const std::int64_t radius : radii) {
		for (const Vec2i64 centre : {Vec2i64{0, 0}, Vec2i64{-123457, 98765}, Vec2i64{37123456, -18456789}}) {
			const Ring shape = geometry::forestShape(centre, equalRanges(radius));
			const Ring polygon = geometry::sightPolygon(centre, radius);
			ASSERT_EQ(shape.size(), polygon.size()) << "radius " << radius;
			for (std::size_t index = 0; index < shape.size(); ++index) {
				const Vec2i64 apart = shape[index] - polygon[index];
				EXPECT_LE(apart.x * apart.x + apart.y * apart.y, 4) << "radius " << radius << " vertex " << index;
			}
		}
	}
}

// A lone ring's tent is convex already: every side keeps its edge, and the shape reaches each side's
// half-plane.
TEST(ForestShape, LoneBodyLosesNothingToConvexity) {
	const Vec2i64 eye{-4000, 9000};
	for (const std::int64_t sideMm : {280, 550, 820, 1070}) {
		for (const std::int64_t distanceMm : {1000, 3000, 10000, 20000}) {
			for (int angleStep = 0; angleStep < 11; ++angleStep) {
				const double				  angle = 2.0 * kPi * angleStep / 11.0;
				const std::vector<ForestBody> bodies{
					{eye + Vec2i64{std::llround(distanceMm * std::cos(angle)), std::llround(distanceMm * std::sin(angle))},
					 squareWidthFactor(sideMm),
					 kFractionOne}
				};
				const SightSideValues ranges = geometry::forestRanges(eye, kSightMm, kSightMm, bodies, ForestTunables{});
				const Ring			  shape = geometry::forestShape(eye, ranges);
				EXPECT_EQ(shape.size(), kSightSides) << sideMm << " at " << distanceMm;
				for (std::size_t side = 0; side < kSightSides; ++side) {
					const double star = static_cast<double>(ranges[side]) * std::cos(kPi / static_cast<double>(kSightSides));
					EXPECT_NEAR(reachAlong(shape, eye, sideAngle(side)), star, 2.0) << sideMm << " at " << distanceMm << " side " << side;
				}
			}
		}
	}
}

// Over seeded random stands, at the sight radius and at a polygon path's reach, the forest shape is
// strictly convex, every vertex lies in every half-plane, and it holds the floor's sight polygon to
// the 2 mm the two roundings can take.
TEST(ForestShape, ConvexAndHoldsTheFloor) {
	std::mt19937		 rng(20261116);
	const ForestTunables tunables;
	const std::int64_t	 floorMm = geometry::forestFloorMm(kSightMm, tunables);
	for (int stand = 0; stand < 40; ++stand) {
		const Vec2i64 eye{static_cast<std::int64_t>(rng() % 2000000) - 1000000, static_cast<std::int64_t>(rng() % 2000000) - 1000000};
		const std::vector<ForestBody> bodies = randomStand(rng, eye);
		for (const std::int64_t radiusMm : {kSightMm, std::int64_t{32200}}) {
			const SightSideValues ranges = geometry::forestRanges(eye, kSightMm, radiusMm, bodies, tunables);
			const Ring			  shape = geometry::forestShape(eye, ranges);
			expectStrictlyConvex(shape);
			for (const Vec2i64& vertex : shape) {
				EXPECT_TRUE(geometry::insideForestHalfPlanes(eye, ranges, vertex)) << "stand " << stand;
			}
			for (const Vec2i64& vertex : geometry::sightPolygon(eye, floorMm)) {
				EXPECT_GE(leastInsideBy(shape, vertex), -2.0) << "stand " << stand;
			}
			for (const Vec2i64& vertex : geometry::sightPolygon(eye, floorMm - 2)) {
				EXPECT_TRUE(geometry::insideForestHalfPlanes(eye, ranges, vertex)) << "stand " << stand;
			}
		}
	}
}

// A clump of 40 veteran trunks 10 m off takes at most 2 m from any clear direction with the slope
// limit (4.2's 1.89 m); without it, convexity alone takes several metres.
TEST(ForestShape, SlopeLimitBoundsAClumpsLossInClearDirections) {
	const Vec2i64  eye{250, 130};
	ForestTunables unlimited;
	unlimited.forestSlopeMm = 10000;
	double worstLimited = 0.0;
	double worstUnlimited = 0.0;
	for (int angleStep = 0; angleStep < 16; ++angleStep) {
		const std::vector<ForestBody> bodies = clumpOf40(eye, 2.0 * kPi * angleStep / 16.0 + 0.05);
		worstLimited = std::max(worstLimited, worstClearLoss(eye, bodies, ForestTunables{}));
		worstUnlimited = std::max(worstUnlimited, worstClearLoss(eye, bodies, unlimited));
	}
	EXPECT_LE(worstLimited, 2000.0);
	EXPECT_GE(worstUnlimited, 4000.0);
	RecordProperty("worstClearLossLimitedMm", std::to_string(std::llround(worstLimited)));
	RecordProperty("worstClearLossUnlimitedMm", std::to_string(std::llround(worstUnlimited)));
	std::printf("clump of 40: worst clear loss %.0f mm with the slope limit, %.0f mm without\n", worstLimited, worstUnlimited);
}

// Fixed input, fixed output, as ForestDepth.RangesArePinned, for the shape's vertices.
TEST(ForestShape, ShapeIsPinned) {
	std::mt19937			rng(424242);
	const Vec2i64			eye{100000, -50000};
	std::vector<ForestBody> bodies = clumpOf40(eye, 1.0);
	for (int index = 0; index < 120; ++index) {
		const Vec2i64 centre{static_cast<std::int64_t>(rng() % 64000) - 32000, static_cast<std::int64_t>(rng() % 64000) - 32000};
		bodies.push_back({eye + centre, squareWidthFactor(280 + static_cast<std::int64_t>(rng() % 900)), kFractionOne});
	}
	const Ring				  shape = geometry::forestShape(eye, geometry::forestRanges(eye, kSightMm, 32200, bodies, ForestTunables{}));
	std::vector<std::int64_t> flat;
	for (const Vec2i64& vertex : shape) {
		flat.push_back(vertex.x);
		flat.push_back(vertex.y);
	}
	const std::vector<std::int64_t> pinned{127912, -49455, 126843, -47193, 125607, -45131, 124233, -43279, 117255, -35580, 114147, -32762,
										   112389, -31459, 110512, -30334, 100906, -25791, 97120,  -24436, 94823,  -23860, 92384,  -23498,
										   89823,  -23373, 87164,  -23503, 84779,  -23857, 83594,  -24567, 81071,  -26438, 79049,  -28271,
										   77335,  -30162, 75809,  -32220, 75116,  -33376, 73215,  -37396, 71903,  -41062, 71111,  -44223,
										   70697,  -47012, 70552,  -49980, 70641,  -51797, 70945,  -53849, 72131,  -58581, 72899,  -60729,
										   74217,  -63516, 75308,  -65336, 76846,  -67409, 79050,  -69841, 82655,  -73109, 85909,  -75522,
										   87709,  -76601, 89685,  -77536, 91340,  -78128, 95031,  -79052, 97109,  -79360, 99135,  -79460,
										   100237, -79406, 101837, -79168, 104498, -78502, 107687, -77361, 110931, -75827, 112371, -74963,
										   115550, -72606, 118088, -70305, 120694, -67431, 123422, -63752, 124856, -61359, 125874, -59208,
										   126821, -56562, 127938, -52101, 128208, -50280};
	EXPECT_EQ(flat, pinned);
}
