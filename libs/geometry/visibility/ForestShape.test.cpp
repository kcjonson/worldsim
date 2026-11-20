#include "visibility/ForestShape.h"

#include "predicates/Predicates.h"
#include "visibility/SightPolygon.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

	// Side `side`'s half-plane offset in doubles, from unit normals of the test's own making.
	double halfPlaneOffset(const SightSideValues& ranges, std::size_t side) {
		return static_cast<double>(ranges[side]) * std::cos(kPi / static_cast<double>(kSightSides));
	}

	// How far along the unit direction at `angle` from the point the exact intersection of the
	// half-planes reaches.
	double exactReach(const SightSideValues& ranges, double angle) {
		double least = std::numeric_limits<double>::max();
		for (std::size_t side = 0; side < kSightSides; ++side) {
			const double facing = std::cos(sideAngle(side) - angle);
			if (facing > 0.0) {
				least = std::min(least, halfPlaneOffset(ranges, side) / facing);
			}
		}
		return least;
	}

	struct PointDouble {
		double x;
		double y;
	};

	// The exact intersection of the half-planes, relative to the point, clipped from a square in
	// doubles.
	std::vector<PointDouble> exactShape(const SightSideValues& ranges) {
		std::vector<PointDouble> polygon{{-200000.0, -200000.0}, {200000.0, -200000.0}, {200000.0, 200000.0}, {-200000.0, 200000.0}};
		for (std::size_t side = 0; side < kSightSides; ++side) {
			const double			 normalX = std::cos(sideAngle(side));
			const double			 normalY = std::sin(sideAngle(side));
			const double			 offset = halfPlaneOffset(ranges, side);
			std::vector<PointDouble> clipped;
			for (std::size_t index = 0; index < polygon.size(); ++index) {
				const PointDouble from = polygon[index];
				const PointDouble to = polygon[(index + 1) % polygon.size()];
				const double	  fromOver = from.x * normalX + from.y * normalY - offset;
				const double	  toOver = to.x * normalX + to.y * normalY - offset;
				if (fromOver <= 0.0) {
					clipped.push_back(from);
				}
				if ((fromOver < 0.0 && toOver > 0.0) || (fromOver > 0.0 && toOver < 0.0)) {
					const double along = fromOver / (fromOver - toOver);
					clipped.push_back({from.x + (to.x - from.x) * along, from.y + (to.y - from.y) * along});
				}
			}
			polygon = clipped;
		}
		return polygon;
	}

	// The farthest any vertex of the forest shape of `ranges` round `eye` sits from the nearest
	// vertex of the exact intersection.
	double worstVertexDisplacement(const Vec2i64& eye, const SightSideValues& ranges) {
		const std::vector<PointDouble> exact = exactShape(ranges);
		double						   worst = 0.0;
		for (const Vec2i64& vertex : geometry::forestShape(eye, ranges)) {
			double nearest = std::numeric_limits<double>::max();
			for (const PointDouble& corner : exact) {
				nearest = std::min(
					nearest, std::hypot(static_cast<double>(vertex.x - eye.x) - corner.x, static_cast<double>(vertex.y - eye.y) - corner.y)
				);
			}
			worst = std::max(worst, nearest);
		}
		return worst;
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

	// Points 10 m from the eye at 16 even steps round it, from 0.05 radians, fixed so the bodies
	// built from them do not depend on the math library.
	constexpr std::array<Vec2i64, 16> kClumpMiddles{
		{{9988, 500},
		 {9036, 4284},
		 {6709, 7416},
		 {3360, 9419},
		 {-500, 9988},
		 {-4284, 9036},
		 {-7416, 6709},
		 {-9419, 3360},
		 {-9988, -500},
		 {-9036, -4284},
		 {-6709, -7416},
		 {-3360, -9419},
		 {500, -9988},
		 {4284, -9036},
		 {7416, -6709},
		 {9419, -3360}}
	};

	// 40 veteran trunks on a 0.5 m grid, the nearest to `middleOffset` from the eye.
	std::vector<ForestBody> clumpOf40(const Vec2i64& eye, const Vec2i64& middleOffset) {
		const Vec2i64		 middle = eye + middleOffset;
		std::vector<Vec2i64> grid;
		for (std::int64_t column = -8; column <= 8; ++column) {
			for (std::int64_t row = -8; row <= 8; ++row) {
				grid.push_back(middle + Vec2i64{column * 500, row * 500});
			}
		}
		std::sort(grid.begin(), grid.end(), [&](const Vec2i64& first, const Vec2i64& second) {
			const Vec2i64	   fromFirst = first - middle;
			const Vec2i64	   fromSecond = second - middle;
			const std::int64_t lengthFirst = fromFirst.x * fromFirst.x + fromFirst.y * fromFirst.y;
			const std::int64_t lengthSecond = fromSecond.x * fromSecond.x + fromSecond.y * fromSecond.y;
			return lengthFirst != lengthSecond ? lengthFirst < lengthSecond : first < second;
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
				worst = std::max(worst, halfPlaneOffset(ranges, side) - reachAlong(shape, eye, sideAngle(side)));
			}
		}
		return worst;
	}

	// The shape of `ranges` round `eye` is strictly convex, every vertex lies within 2 mm of every
	// half-plane by the test's own unit normals, and along 720 directions it reaches the exact
	// intersection's boundary to within `toleranceMm`.
	void expectShapeMatchesHalfPlanes(const Vec2i64& eye, const SightSideValues& ranges, double toleranceMm, const std::string& label) {
		const Ring shape = geometry::forestShape(eye, ranges);
		expectStrictlyConvex(shape);
		for (const Vec2i64& vertex : shape) {
			for (std::size_t side = 0; side < kSightSides; ++side) {
				const double along = static_cast<double>(vertex.x - eye.x) * std::cos(sideAngle(side)) +
									 static_cast<double>(vertex.y - eye.y) * std::sin(sideAngle(side));
				ASSERT_LE(along, halfPlaneOffset(ranges, side) + 2.0) << label << " side " << side;
			}
		}
		for (int step = 0; step < 720; ++step) {
			const double angle = 2.0 * kPi * step / 720.0;
			ASSERT_GE(reachAlong(shape, eye, angle), exactReach(ranges, angle) - toleranceMm) << label << " step " << step;
		}
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
// 2 mm; with no bodies the ranges are the radius on every side.
TEST(ForestShape, EqualRangesGiveTheSightPolygon) {
	std::mt19937			  rng(64);
	std::vector<std::int64_t> radii{1000, 1999, 20100, 29950, 30000, 32200, 38000, 65535};
	for (int index = 0; index < 60; ++index) {
		radii.push_back(1000 + static_cast<std::int64_t>(rng() % 64000));
	}
	for (const std::int64_t radius : radii) {
		for (const Vec2i64 centre : {Vec2i64{0, 0}, Vec2i64{-123457, 98765}, Vec2i64{37123456, -18456789}}) {
			if (radius > kSightMm - geometry::forestFloorMm(kSightMm, ForestTunables{})) {
				EXPECT_EQ(geometry::forestRanges(centre, kSightMm, radius, {}, ForestTunables{}), equalRanges(radius))
					<< "radius " << radius;
			}
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

// At the least range, alone or beside the greatest, the shape is a strictly convex polygon.
TEST(ForestShape, LeastRangeGivesAPolygon) {
	const Vec2i64 eye{777, -333};
	expectStrictlyConvex(geometry::forestShape(eye, equalRanges(geometry::kForestShapeLeastRangeMm)));
	SightSideValues mixed = equalRanges(geometry::kForestRadiusLimitMm - 1);
	for (std::size_t side = 0; side < kSightSides; side += 5) {
		mixed[side] = geometry::kForestShapeLeastRangeMm;
	}
	expectStrictlyConvex(geometry::forestShape(eye, mixed));
}

// Every vertex sits within a few millimetres of its exact crossing, worked out here by clipping in
// doubles: where two far-apart sides cross well outside the others (sides 11 and 33, near
// (-22088, 12766)), and through forestRanges with no slope limit and a low floor.
TEST(ForestShape, VerticesSitAtTheirCrossings) {
	SightSideValues ranges = equalRanges(30000);
	ranges[11] = 2099;
	ranges[33] = 20000;
	const Vec2i64 eye{40000, -70000};
	EXPECT_LE(worstVertexDisplacement(eye, ranges), 2.0);
	const Ring shape = geometry::forestShape(eye, ranges);
	double	   nearest = std::numeric_limits<double>::max();
	for (const Vec2i64& vertex : shape) {
		nearest =
			std::min(nearest, std::hypot(static_cast<double>(vertex.x - eye.x) + 22088.0, static_cast<double>(vertex.y - eye.y) - 12766.0));
	}
	EXPECT_LE(nearest, 2.0);

	std::mt19937   rng(11);
	ForestTunables steep;
	steep.forestSlopeMm = 10000;
	steep.forestFloorPercent = 10;
	steep.forestCapMm = 5000;
	double worst = 0.0;
	for (int stand = 0; stand < 30; ++stand) {
		const Vec2i64 standEye{static_cast<std::int64_t>(rng() % 200000) - 100000, static_cast<std::int64_t>(rng() % 200000) - 100000};
		std::vector<ForestBody>		  bodies = randomStand(rng, standEye);
		const std::vector<ForestBody> clump = clumpOf40(standEye, kClumpMiddles[static_cast<std::size_t>(stand) % kClumpMiddles.size()]);
		bodies.insert(bodies.end(), clump.begin(), clump.end());
		for (const std::int64_t radiusMm : {kSightMm, std::int64_t{32200}}) {
			worst = std::max(worst, worstVertexDisplacement(standEye, geometry::forestRanges(standEye, kSightMm, radiusMm, bodies, steep)));
		}
	}
	EXPECT_LE(worst, 4.0);
	RecordProperty("worstVertexDisplacementSteepMicrons", std::to_string(std::llround(worst * 1000.0)));
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
					EXPECT_NEAR(reachAlong(shape, eye, sideAngle(side)), halfPlaneOffset(ranges, side), 2.0)
						<< sideMm << " at " << distanceMm << " side " << side;
				}
			}
		}
	}
}

// Over seeded random stands, at the sight radius and at a polygon path's reach, the forest shape is
// strictly convex, every vertex lies in every half-plane, it matches the exact intersection of the
// half-planes worked out here, and it holds the floor's sight polygon to the 2 mm the two roundings
// can take.
TEST(ForestShape, ConvexAndHoldsTheFloor) {
	std::mt19937		 rng(20261116);
	const ForestTunables tunables;
	const std::int64_t	 floorMm = geometry::forestFloorMm(kSightMm, tunables);
	for (int stand = 0; stand < 40; ++stand) {
		const Vec2i64 eye{static_cast<std::int64_t>(rng() % 2000000) - 1000000, static_cast<std::int64_t>(rng() % 2000000) - 1000000};
		const std::vector<ForestBody> bodies = randomStand(rng, eye);
		for (const std::int64_t radiusMm : {kSightMm, std::int64_t{32200}}) {
			const SightSideValues			 ranges = geometry::forestRanges(eye, kSightMm, radiusMm, bodies, tunables);
			const geometry::ForestHalfPlanes halfPlanes = geometry::forestHalfPlanes(ranges);
			const Ring						 shape = geometry::forestShape(eye, ranges);
			expectShapeMatchesHalfPlanes(eye, ranges, 3.0, "stand " + std::to_string(stand));
			for (const Vec2i64& vertex : shape) {
				EXPECT_TRUE(geometry::insideForestHalfPlanes(eye, halfPlanes, vertex)) << "stand " << stand;
			}
			for (const Vec2i64& vertex : geometry::sightPolygon(eye, floorMm)) {
				EXPECT_GE(leastInsideBy(shape, vertex), -2.0) << "stand " << stand;
			}
			for (const Vec2i64& vertex : geometry::sightPolygon(eye, floorMm - 2)) {
				EXPECT_TRUE(geometry::insideForestHalfPlanes(eye, halfPlanes, vertex)) << "stand " << stand;
			}
		}
	}
}

// The point test (one squared-length accept, a binary search by angle, and the sides within the
// window) gives what testing all 64 half-planes gives: on a dense ring of points through each stand's
// rim band, and on the integer points either side of each side's edge and nearest it (exactly on it
// where one is an integer point), with equal ranges, random stands, and a lone deep dip beside full
// ranges.
TEST(ForestShape, PointTestAgreesWithEveryHalfPlane) {
	const auto everySide = [](const geometry::ForestHalfPlanes& halfPlanes, const Vec2i64& offset) {
		for (std::size_t side = 0; side < kSightSides; ++side) {
			const auto& [normalX, normalY] = geometry::kSightSideNormals[side];
			if (offset.x * normalX + offset.y * normalY > halfPlanes.offsets[side]) {
				return false;
			}
		}
		return true;
	};
	std::mt19937				 rng(20261117);
	std::vector<SightSideValues> rangeSets = {equalRanges(kSightMm), equalRanges(32200)};
	SightSideValues				 dip = equalRanges(kSightMm);
	dip[10]							 = geometry::kForestShapeLeastRangeMm;
	rangeSets.push_back(dip);
	for (int stand = 0; stand < 12; ++stand) {
		const Vec2i64 eye{static_cast<std::int64_t>(rng() % 200000), static_cast<std::int64_t>(rng() % 200000)};
		rangeSets.push_back(geometry::forestRanges(eye, kSightMm, kSightMm, randomStand(rng, eye), ForestTunables{}));
	}
	const Vec2i64 point{-31000, 47000};
	for (const SightSideValues& ranges : rangeSets) {
		const geometry::ForestHalfPlanes halfPlanes = geometry::forestHalfPlanes(ranges);
		const std::int64_t least = *std::min_element(ranges.begin(), ranges.end());
		const std::int64_t most	 = *std::max_element(ranges.begin(), ranges.end());
		for (int turn = 0; turn < 1024; ++turn) {
			const double angle = 2.0 * kPi * turn / 1024.0;
			for (std::int64_t radius = least - 400; radius <= most + 400; radius += 53) {
				const Vec2i64 offset{std::llround(radius * std::cos(angle)), std::llround(radius * std::sin(angle))};
				ASSERT_EQ(geometry::insideForestHalfPlanes(point, halfPlanes, point + offset), everySide(halfPlanes, offset))
					<< offset.x << ", " << offset.y;
			}
		}
		for (std::size_t side = 0; side < kSightSides; ++side) {
			const auto& [normalX, normalY] = geometry::kSightSideNormals[side];
			const std::int64_t offsetMm	   = halfPlanes.offsets[side];
			const double	   lengthSquared = static_cast<double>(normalX) * normalX + static_cast<double>(normalY) * normalY;
			const bool		   steep		 = std::abs(normalY) >= std::abs(normalX);
			// Along the edge near the normal's foot, the integer points nearest it on either side.
			const std::int64_t footAlong = std::llround(static_cast<double>(offsetMm) * (steep ? normalX : normalY) / lengthSquared);
			const std::int64_t across	 = steep ? normalY : normalX;
			const std::int64_t along	 = steep ? normalX : normalY;
			for (std::int64_t step = footAlong - 2000; step <= footAlong + 2000; ++step) {
				const std::int64_t rest	 = offsetMm - step * along;
				const std::int64_t below = rest / across - (rest % across != 0 && ((rest < 0) != (across < 0)) ? 1 : 0);
				for (std::int64_t other = below - 1; other <= below + 1; ++other) {
					const Vec2i64 offset = steep ? Vec2i64{step, other} : Vec2i64{other, step};
					ASSERT_EQ(geometry::insideForestHalfPlanes(point, halfPlanes, point + offset), everySide(halfPlanes, offset))
						<< "side " << side << " at " << offset.x << ", " << offset.y;
				}
			}
		}
	}
}

// At the edges of each tunable's range forest depth gives positive ranges at or above the floor and
// a shape that matches the exact intersection; a slope of 0 gives every side the least capped depth,
// round the wrap.
TEST(ForestShape, TunablesAtTheirRangeEdges) {
	std::vector<std::pair<std::string, ForestTunables>> cases;
	const auto											add = [&](const std::string& label, auto change) {
		 ForestTunables tunables;
		 change(tunables);
		 cases.emplace_back(label, tunables);
	};
	add("floor 1", [](ForestTunables& tunables) { tunables.forestFloorPercent = 1; });
	add("floor 100", [](ForestTunables& tunables) { tunables.forestFloorPercent = 100; });
	add("slope 0", [](ForestTunables& tunables) { tunables.forestSlopeMm = 0; });
	add("window 1", [](ForestTunables& tunables) { tunables.forestWindowSteps = 1; });
	add("window 32", [](ForestTunables& tunables) { tunables.forestWindowSteps = 32; });
	add("cap 0", [](ForestTunables& tunables) { tunables.forestCapMm = 0; });
	add("cap 5000", [](ForestTunables& tunables) { tunables.forestCapMm = 5000; });

	std::mt19937 rng(4242);
	for (int stand = 0; stand < 6; ++stand) {
		const Vec2i64			eye{static_cast<std::int64_t>(rng() % 200000) - 100000, static_cast<std::int64_t>(rng() % 200000) - 100000};
		std::vector<ForestBody> bodies = randomStand(rng, eye);
		const std::vector<ForestBody> clump = clumpOf40(eye, kClumpMiddles[static_cast<std::size_t>(stand) * 3 % kClumpMiddles.size()]);
		bodies.insert(bodies.end(), clump.begin(), clump.end());
		for (const auto& [label, tunables] : cases) {
			const std::int64_t	  floorMm = geometry::forestFloorMm(kSightMm, tunables);
			const SightSideValues raw = geometry::forestRawDepth(eye, kSightMm, bodies, tunables);
			const SightSideValues limited = geometry::forestLimitedDepth(raw, kSightMm, tunables);
			const SightSideValues ranges = geometry::forestRanges(eye, kSightMm, kSightMm, bodies, tunables);
			for (std::size_t side = 0; side < kSightSides; ++side) {
				ASSERT_GE(ranges[side], floorMm) << label << " side " << side;
				ASSERT_LE(ranges[side], kSightMm) << label << " side " << side;
			}
			if (tunables.forestSlopeMm == 0) {
				const std::int64_t least = std::min(*std::min_element(raw.begin(), raw.end()), kSightMm - floorMm);
				EXPECT_EQ(limited, equalRanges(least)) << label;
			}
			if (tunables.forestCapMm == 0 || tunables.forestFloorPercent == 100) {
				EXPECT_EQ(ranges, equalRanges(kSightMm)) << label;
			}
			expectShapeMatchesHalfPlanes(eye, ranges, 3.0, label + " stand " + std::to_string(stand));
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
	for (const Vec2i64& middle : kClumpMiddles) {
		const std::vector<ForestBody> bodies = clumpOf40(eye, middle);
		worstLimited = std::max(worstLimited, worstClearLoss(eye, bodies, ForestTunables{}));
		worstUnlimited = std::max(worstUnlimited, worstClearLoss(eye, bodies, unlimited));
	}
	EXPECT_LE(worstLimited, 2000.0);
	EXPECT_GE(worstUnlimited, 4000.0);
	RecordProperty("worstClearLossLimitedMm", std::to_string(std::llround(worstLimited)));
	RecordProperty("worstClearLossUnlimitedMm", std::to_string(std::llround(worstUnlimited)));
}

// Fixed input, fixed output, as ForestDepth.RangesArePinned, for the shape's vertices.
TEST(ForestShape, ShapeIsPinned) {
	std::mt19937			rng(424242);
	const Vec2i64			eye{100000, -50000};
	std::vector<ForestBody> bodies = clumpOf40(eye, {5403, 8415});
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
	const std::vector<std::int64_t> pinned{127913, -49455, 126843, -47193, 125607, -45131, 124233, -43278, 117256, -35580, 114147, -32762,
										   112389, -31459, 110512, -30334, 100906, -25790, 97119,  -24435, 94823,  -23860, 92383,  -23498,
										   89822,  -23373, 87163,  -23503, 84779,  -23857, 83594,  -24567, 81071,  -26438, 79049,  -28271,
										   77335,  -30162, 75808,  -32220, 75116,  -33375, 73214,  -37396, 71903,  -41062, 71111,  -44222,
										   70697,  -47012, 70552,  -49980, 70641,  -51797, 70945,  -53849, 72131,  -58582, 72899,  -60730,
										   74217,  -63516, 75308,  -65336, 76845,  -67409, 79050,  -69842, 82655,  -73109, 85909,  -75522,
										   87709,  -76601, 89685,  -77536, 91340,  -78128, 95030,  -79052, 97108,  -79360, 99135,  -79460,
										   100237, -79406, 101838, -79168, 104498, -78502, 107687, -77361, 110931, -75827, 112371, -74964,
										   115550, -72606, 118089, -70305, 120694, -67431, 123422, -63753, 124856, -61359, 125874, -59208,
										   126821, -56562, 127938, -52102, 128208, -50280};
	EXPECT_EQ(flat, pinned);
}
