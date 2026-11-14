#include "SightClip.h"

#include "Buildings.test.h"
#include "SightPolygon.h"
#include "SightSkirt.h"
#include "Visibility.h"
#include "../predicates/Predicates.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace geometry;

namespace {

	constexpr std::int64_t kSightMm = 30000;
	constexpr std::int64_t kStepMm	= 500;

	// The convex hull, counter-clockwise, no three vertices in line.
	Ring hullOf(std::vector<Vec2i64> points) {
		std::sort(points.begin(), points.end());
		points.erase(std::unique(points.begin(), points.end()), points.end());
		Ring hull(points.size() * 2);
		std::size_t size = 0;
		for (std::size_t pass = 0; pass < 2; ++pass) {
			const std::size_t floor = size;
			for (const Vec2i64& point : points) {
				while (size >= floor + 2 && cross(hull[size - 1] - hull[size - 2], point - hull[size - 2]).sign() <= 0) {
					--size;
				}
				hull[size++] = point;
			}
			--size;
			std::reverse(points.begin(), points.end());
		}
		hull.resize(size);
		return hull;
	}

	// The hull of the sight polygons round the two ends: the shape a stretch of path sweeps.
	Ring capsuleOf(const Vec2i64& start, const Vec2i64& end, std::int64_t radius) {
		std::vector<Vec2i64> points = sightPolygon(start, radius);
		const Ring			 atEnd	= sightPolygon(end, radius);
		points.insert(points.end(), atEnd.begin(), atEnd.end());
		return hullOf(points);
	}

	double distanceToBoundary(const Vec2i64& point, const Ring& ring) {
		double nearest = 1e300;
		for (std::size_t i = 0; i < ring.size(); ++i) {
			nearest = std::min(nearest, distanceToSegment(point, ring[i], ring[(i + 1) % ring.size()]));
		}
		return nearest;
	}

	struct Tally {
		int clipped		  = 0;
		int rejected	  = 0; // not simple round the centre
		int vertexOutside = 0; // a vertex strictly outside the star, or farther than 2 mm outside the convex ring
		int disagree	  = 0; // a point 3 mm or more from every boundary in the result but not in both, or in both but not the result
		int samples		  = 0;
	};

	void check(const Ring& star, const Vec2i64& centre, const Ring& convex, std::mt19937& rng, Tally& tally) {
		const Ring clipped = starClippedToConvex(star, centre, convex);
		++tally.clipped;
		if (!simpleAround(clipped, centre)) {
			++tally.rejected;
			return;
		}
		for (const Vec2i64& vertex : clipped) {
			const bool outsideStar	 = pointInPolygon(vertex, star) == PointInPolygon::Outside;
			const bool outsideConvex = pointInPolygon(vertex, convex) == PointInPolygon::Outside && distanceToBoundary(vertex, convex) > 2.0;
			tally.vertexOutside += outsideStar || outsideConvex ? 1 : 0;
		}
		Vec2i64 low	 = convex.front();
		Vec2i64 high = low;
		for (const Vec2i64& vertex : convex) {
			low	 = {std::min(low.x, vertex.x), std::min(low.y, vertex.y)};
			high = {std::max(high.x, vertex.x), std::max(high.y, vertex.y)};
		}
		for (int attempt = 0; attempt < 150; ++attempt) {
			const std::int64_t x = low.x + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(high.x - low.x + 1));
			const std::int64_t y = low.y + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(high.y - low.y + 1));
			const Vec2i64	   point{x, y};
			if (distanceToBoundary(point, star) < 3.0 || distanceToBoundary(point, convex) < 3.0 || distanceToBoundary(point, clipped) < 3.0) {
				continue;
			}
			++tally.samples;
			const bool inBoth = pointInPolygon(point, star) == PointInPolygon::Inside && pointInPolygon(point, convex) == PointInPolygon::Inside;
			const bool inClip = pointInPolygon(point, clipped) == PointInPolygon::Inside;
			tally.disagree += inBoth != inClip ? 1 : 0;
		}
	}

} // namespace

TEST(SightClip, StarInsideIsUnchanged) {
	const Vec2i64					   centre{5000, -3000};
	const std::vector<OccluderSegment> walls{{{0, 0}, {10000, 0}}, {{10000, 0}, {10000, -6000}}, {{10000, -6000}, {0, -6000}}, {{0, -6000}, {0, 0}}};
	const Ring						   room = computeVisibilityPolygon(centre, kSightMm + kStepMm, walls);
	EXPECT_EQ(starClippedToConvex(room, centre, sightPolygon(centre, kSightMm)), room);
}

TEST(SightClip, ConvexInsideIsTheConvex) {
	const Vec2i64 centre{-77000, 4000};
	const Ring	  open = computeVisibilityPolygon(centre, kSightMm + kStepMm, {});
	EXPECT_EQ(starClippedToConvex(open, centre, sightPolygon(centre, kSightMm)), sightPolygon(centre, kSightMm));
}

TEST(SightClip, LongPolygonCutToSightIsTheSightPolygon) {
	// Built a step long and cut to the sight polygon, a visibility polygon is the one built at sight:
	// the rim is the same 64-gon and the walls' hits are the same rays.
	std::mt19937					   rng(0xC11Bu);
	const buildings::Building building = buildings::makeBuilding({5000, 8000, 4000}, {6000, 4500}, 0.349, {1234567, -7654321}, 0.4, rng);
	int								   disagree = 0;
	int								   samples	= 0;
	for (const Vec2i64& centre : buildings::observersFor(building, rng, 10)) {
		const Ring atSight = computeVisibilityPolygon(centre, kSightMm, building.walls);
		const Ring clipped = starClippedToConvex(computeVisibilityPolygon(centre, kSightMm + kStepMm, building.walls), centre, sightPolygon(centre, kSightMm));
		ASSERT_FALSE(clipped.empty());
		for (int attempt = 0; attempt < 200; ++attempt) {
			const std::int64_t offsetX = static_cast<std::int64_t>(rng() % 60001) - 30000;
			const std::int64_t offsetY = static_cast<std::int64_t>(rng() % 60001) - 30000;
			const Vec2i64	   point{centre.x + offsetX, centre.y + offsetY};
			if (distanceToBoundary(point, atSight) < 3.0 || distanceToBoundary(point, clipped) < 3.0) {
				continue;
			}
			++samples;
			disagree += (pointInPolygon(point, atSight) == PointInPolygon::Inside) != (pointInPolygon(point, clipped) == PointInPolygon::Inside) ? 1 : 0;
		}
	}
	EXPECT_GT(samples, 1000);
	EXPECT_EQ(disagree, 0);
}

TEST(SightClip, CutToACapsuleItsOuterEdgeIsTheCapsules) {
	// Walking 0.5 m along a wall's face, the long polygon from the start cut to the capsule of the
	// stretch: every vertex off the wall and its shadow lies on the capsule's boundary.
	const Vec2i64					   start{0, 0};
	const Vec2i64					   end{500, 0};
	const std::vector<OccluderSegment> walls{{{-3000, 2000}, {4000, 2000}}, {{4000, 2000}, {4000, 2200}}, {{4000, 2200}, {-3000, 2200}}, {{-3000, 2200}, {-3000, 2000}}};
	const Ring						   capsule = capsuleOf(start, end, kSightMm - 50);
	const Ring						   clipped = starClippedToConvex(computeVisibilityPolygon(start, kSightMm + kStepMm, walls), start, capsule);
	ASSERT_TRUE(simpleAround(clipped, start));
	int offBoth = 0;
	for (const Vec2i64& vertex : clipped) {
		const bool onCapsule = distanceToBoundary(vertex, capsule) <= 2.0;
		bool	   onWall	 = false;
		for (const OccluderSegment& wall : walls) {
			onWall = onWall || withinDistanceOfSegment(vertex, wall.a, wall.b, 1);
		}
		offBoth += onCapsule || onWall ? 0 : 1;
	}
	EXPECT_EQ(offBoth, 0);
	const Ring skirted = skirtedSightShape(clipped, start, capsule, kSeenSkirtMm);
	ASSERT_FALSE(skirted.empty());
	for (const Vec2i64& vertex : skirted) {
		EXPECT_FALSE(pointInPolygon(vertex, capsule) == PointInPolygon::Outside && distanceToBoundary(vertex, capsule) > static_cast<double>(kSeenSkirtMm + 2));
	}
	EXPECT_EQ(pointInPolygon({250, 2040}, skirted), PointInPolygon::Inside) << "the wall's face is grown by the skirt";
	EXPECT_EQ(pointInPolygon({500 + kSightMm - 50 + 20, 0}, skirted), PointInPolygon::Outside) << "the capsule's end is not";
}

TEST(SightClip, RejectsAConvexRingThatMissesTheCentre) {
	const Vec2i64 centre{0, 0};
	const Ring	  star = sightPolygon(centre, kSightMm);
	EXPECT_TRUE(starClippedToConvex(star, centre, sightPolygon({40000, 0}, kSightMm)).empty());
	EXPECT_TRUE(starClippedToConvex(star, centre, sightPolygon({kSightMm, 0}, kSightMm)).empty()) << "on its boundary is not strictly inside";
}

TEST(SightClip, HoldsItsPropertiesOnRandomBuildings) {
	std::mt19937 rng(0xC1A5u);
	Tally		 tally;
	for (int scene = 0; scene < 10; ++scene) {
		const buildings::Building building = buildings::randomBuilding(rng);
		for (const Vec2i64& centre : buildings::observersFor(building, rng, 6)) {
			const Ring star = computeVisibilityPolygon(centre, kSightMm + kStepMm, building.walls);
			if (!simpleAround(star, centre)) {
				continue;
			}
			const std::int64_t heading = static_cast<std::int64_t>(rng() % 64);
			const Vec2i64	   back{-kSightDirections[static_cast<std::size_t>(heading)][0] * 600 / kSightDirectionScale,
									-kSightDirections[static_cast<std::size_t>(heading)][1] * 600 / kSightDirectionScale};
			check(star, centre, capsuleOf(centre + back, centre, kSightMm - 50), rng, tally);
			check(star, centre, sightPolygon(centre + Vec2i64{static_cast<std::int64_t>(rng() % 801) - 400, 0}, kSightMm - 160), rng, tally);
		}
	}
	std::printf("[sight clip fuzz] %d clipped, %d not simple, %d vertices outside, %d of %d samples disagree\n", tally.clipped, tally.rejected,
				tally.vertexOutside, tally.disagree, tally.samples);
	EXPECT_GT(tally.samples, 5000);
	EXPECT_EQ(tally.vertexOutside, 0);
	EXPECT_EQ(tally.disagree, 0);
	EXPECT_LE(tally.rejected * 50, tally.clipped);
}

TEST(SightClip, IsTheSameOnEveryToolchain) {
	std::mt19937					   rng(0xC11Du);
	const buildings::Building building = buildings::makeBuilding({5000, 8000, 4000, 6500}, {6000, 4500}, 0.349, {37123456, -18456789}, 0.4, rng);
	std::uint64_t					   hash	 = 0xCBF29CE484222325ULL;
	std::size_t						   total = 0;
	for (const Vec2i64& centre : building.roomCentres) {
		const Ring clipped =
			starClippedToConvex(computeVisibilityPolygon(centre, kSightMm + kStepMm, building.walls), centre, capsuleOf(centre - Vec2i64{431, 257}, centre, kSightMm - 50));
		total += clipped.size();
		for (const Vec2i64& vertex : clipped) {
			hash = (hash ^ static_cast<std::uint64_t>(vertex.x)) * 0x100000001B3ULL;
			hash = (hash ^ static_cast<std::uint64_t>(vertex.y)) * 0x100000001B3ULL;
		}
	}
	std::printf("[sight clip pin] vertices %zu hash %llu\n", total, static_cast<unsigned long long>(hash));
	EXPECT_EQ(total, 1642U);
	EXPECT_EQ(hash, 9961666663891061190ULL);
}
TEST(SightClip, ACrossingRoundedPastTheConvexSideStillSkirts) {
	// Walking east toward the end of a wall: the shadow edge from its north end meets the stretch's
	// capsule at a shallow angle, where the rounded crossing once landed a millimetre outside the
	// capsule and the skirt along it zigzagged.
	const Vec2i64					   centre{-16450, -5000};
	const std::vector<OccluderSegment> walls{{{0, 0}, {0, 10000}}};
	const Ring						   capsule = capsuleOf(centre, {-15900, -5000}, kSightMm - 50);
	const Ring						   clipped = starClippedToConvex(computeVisibilityPolygon(centre, kSightMm + kStepMm, walls), centre, capsule);
	ASSERT_TRUE(simpleAround(clipped, centre));
	for (const Vec2i64& vertex : clipped) {
		EXPECT_NE(pointInPolygon(vertex, capsule), PointInPolygon::Outside) << "(" << vertex.x << ", " << vertex.y << ")";
	}
	EXPECT_FALSE(skirtedSightShape(clipped, centre, capsule, kSeenSkirtMm).empty());
}
