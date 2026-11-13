#include "SightSkirt.h"

#include "Buildings.test.h"
#include "SightPolygon.h"
#include "Visibility.h"
#include "../predicates/Predicates.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

using namespace geometry;

namespace {

	constexpr std::int64_t kSightMm = 30000;

	double distanceToBoundary(const Vec2i64& point, const Ring& ring) {
		double nearest = 1e300;
		for (std::size_t i = 0; i < ring.size(); ++i) {
			nearest = std::min(nearest, distanceToSegment(point, ring[i], ring[(i + 1) % ring.size()]));
		}
		return nearest;
	}

	// Distance from a point to a ring's closed area, edges near the point only.
	double distanceToArea(const Vec2i64& point, const Ring& ring) {
		if (pointInPolygon(point, ring) != PointInPolygon::Outside) {
			return 0.0;
		}
		return distanceToBoundary(point, ring);
	}

	struct Properties {
		bool		holdsShape		= true; // every vertex and every deep point of the shape is in the skirted ring
		double		farthestReachMm = 0.0;	// the farthest sampled point of the skirted ring from the shape
		bool		rimKept			= true; // every rim vertex of the shape is one of the skirted ring's
		std::size_t shapeVertices	= 0;
		std::size_t skirtedVertices = 0;
	};

	// The four properties, sampled: the skirted ring holds the shape (to the millimetre of
	// snapping on the rim), every point of it sampled every `stepMm` along its edges lies within
	// reach of the shape, its rim vertices are the shape's, and it is simple round the centre.
	Properties propertiesOf(const Ring& shape, const Ring& skirted, const Vec2i64& centre, std::mt19937& rng, std::int64_t stepMm) {
		Properties out;
		out.shapeVertices	= shape.size();
		out.skirtedVertices = skirted.size();
		for (const Vec2i64& vertex : shape) {
			if (pointInPolygon(vertex, skirted) == PointInPolygon::Outside && distanceToBoundary(vertex, skirted) > 1.0) {
				out.holdsShape = false;
			}
		}
		Vec2i64 low	 = shape.front();
		Vec2i64 high = low;
		for (const Vec2i64& vertex : shape) {
			low	 = {std::min(low.x, vertex.x), std::min(low.y, vertex.y)};
			high = {std::max(high.x, vertex.x), std::max(high.y, vertex.y)};
		}
		for (int attempt = 0; attempt < 400; ++attempt) {
			const std::int64_t x = low.x + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(high.x - low.x + 1));
			const std::int64_t y = low.y + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(high.y - low.y + 1));
			const Vec2i64	   point{x, y};
			if (pointInPolygon(point, shape) == PointInPolygon::Inside && distanceToBoundary(point, shape) > 1.0 &&
				pointInPolygon(point, skirted) != PointInPolygon::Inside) {
				out.holdsShape = false;
			}
		}
		for (std::size_t i = 0; i < skirted.size(); ++i) {
			const Vec2i64&	   from	 = skirted[i];
			const Vec2i64&	   to	 = skirted[(i + 1) % skirted.size()];
			const double	   span	 = distanceToSegment(from, to, to);
			const std::int64_t steps = std::max<std::int64_t>(1, static_cast<std::int64_t>(span / static_cast<double>(stepMm)));
			for (std::int64_t step = 0; step < steps; ++step) {
				const double  along = static_cast<double>(step) / static_cast<double>(steps);
				const Vec2i64 point{from.x + std::llround(along * static_cast<double>(to.x - from.x)), from.y + std::llround(along * static_cast<double>(to.y - from.y))};
				out.farthestReachMm = std::max(out.farthestReachMm, distanceToArea(point, shape));
			}
		}
		const Ring rim = sightPolygon(centre, kSightMm);
		for (const Vec2i64& vertex : rim) {
			const bool inShape	 = std::find(shape.begin(), shape.end(), vertex) != shape.end();
			const bool inSkirted = std::find(skirted.begin(), skirted.end(), vertex) != skirted.end();
			out.rimKept			 = out.rimKept && (!inShape || inSkirted);
		}
		return out;
	}

	std::vector<OccluderSegment> wallPiece(const Vec2i64& from, const Vec2i64& to, std::int64_t thickness) {
		const Vec2i64 across = to.x == from.x ? Vec2i64{thickness, 0} : Vec2i64{0, thickness};
		return {{from, to}, {to, to + across}, {to + across, from + across}, {from + across, from}};
	}

} // namespace

TEST(SightSkirt, OpenGroundIsTheSightPolygon) {
	const Vec2i64 centre{1234567, -7654321};
	const Ring	  shape = computeVisibilityPolygon(centre, kSightMm, {});
	EXPECT_EQ(skirtedSightShape(shape, centre, kSightMm, kSeenSkirtMm), shape);
}

TEST(SightSkirt, WallGrowsByTheSkirtAndTheRimStays) {
	const Vec2i64					   centre{0, 0};
	const std::vector<OccluderSegment> walls = wallPiece({-4000, 5000}, {3000, 5000}, 200);
	const Ring						   shape = computeVisibilityPolygon(centre, kSightMm, walls);
	const Ring						   skirted = skirtedSightShape(shape, centre, kSightMm, kSeenSkirtMm);
	ASSERT_FALSE(skirted.empty());
	EXPECT_TRUE(simpleAround(skirted, centre));
	std::mt19937	 rng(7);
	const Properties properties = propertiesOf(shape, skirted, centre, rng, 5);
	EXPECT_TRUE(properties.holdsShape);
	EXPECT_TRUE(properties.rimKept);
	EXPECT_LE(properties.farthestReachMm, static_cast<double>(kSeenSkirtMm + 1));
	EXPECT_LE(properties.skirtedVertices, properties.shapeVertices + 12);
	// The wall's face moves out by just under the skirt; the shadow edges by as much, sideways.
	EXPECT_EQ(pointInPolygon({0, 5040}, skirted), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon({0, 5060}, skirted), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon({3000 * 4 - 30, 5000 * 4}, skirted), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon({3000 * 4 - 80, 5000 * 4}, skirted), PointInPolygon::Outside);
}

TEST(SightSkirt, NotchNarrowerThanTwiceTheSkirtCloses) {
	// A post 60 mm wide 3 m off casts a shadow that is 60 mm wide at the post and 600 mm at the
	// rim; grown by 50 mm a side, the shadow closes until it is 100 mm wide, 5 m out.
	const Vec2i64					   centre{0, 0};
	const std::vector<OccluderSegment> walls = wallPiece({-30, 3000}, {30, 3000}, 60);
	const Ring						   shape = computeVisibilityPolygon(centre, kSightMm, walls);
	const Ring						   skirted = skirtedSightShape(shape, centre, kSightMm, kSeenSkirtMm);
	ASSERT_FALSE(skirted.empty());
	EXPECT_EQ(pointInPolygon({0, 3070}, shape), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon({0, 4000}, skirted), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon({0, 7000}, skirted), PointInPolygon::Outside);
	std::mt19937	 rng(3);
	const Properties properties = propertiesOf(shape, skirted, centre, rng, 5);
	EXPECT_TRUE(properties.holdsShape);
	EXPECT_LE(properties.farthestReachMm, static_cast<double>(kSeenSkirtMm + 1));
}

TEST(SightSkirt, IsTheSameOnEveryToolchain) {
	std::mt19937					   rng(0x5C1u);
	const buildings::Building building = buildings::makeBuilding({5000, 8000, 4000, 6500}, {6000, 4500}, 0.349, {37123456, -18456789}, 0.4, rng);
	std::uint64_t					   hash	 = 0xCBF29CE484222325ULL;
	std::size_t						   total = 0;
	for (const Vec2i64& centre : building.roomCentres) {
		const Ring skirted = skirtedSightShape(computeVisibilityPolygon(centre, kSightMm, building.walls), centre, kSightMm, kSeenSkirtMm);
		total += skirted.size();
		for (const Vec2i64& vertex : skirted) {
			hash = (hash ^ static_cast<std::uint64_t>(vertex.x)) * 0x100000001B3ULL;
			hash = (hash ^ static_cast<std::uint64_t>(vertex.y)) * 0x100000001B3ULL;
		}
	}
	std::printf("[skirt pin] vertices %zu hash %llu\n", total, static_cast<unsigned long long>(hash));
	EXPECT_EQ(total, 2106U);
	EXPECT_EQ(hash, 397742317781407675ULL);
}

TEST(SightSkirt, HoldsItsPropertiesOnRandomBuildings) {
	std::mt19937 rng(0x5EEDu);
	int			 polygons = 0;
	int			 skipped  = 0;
	int			 failures = 0;
	double		 farthest = 0.0;
	std::size_t	 shapeVertices	 = 0;
	std::size_t	 skirtedVertices = 0;
	for (int scene = 0; scene < 12; ++scene) {
		const buildings::Building building = buildings::randomBuilding(rng);
		for (const Vec2i64& centre : buildings::observersFor(building, rng, 8)) {
			const Ring shape = computeVisibilityPolygon(centre, kSightMm, building.walls);
			if (!simpleAround(shape, centre)) {
				continue;
			}
			++polygons;
			const Ring skirted = skirtedSightShape(shape, centre, kSightMm, kSeenSkirtMm);
			if (skirted.empty()) {
				++skipped;
				continue;
			}
			const Properties properties = propertiesOf(shape, skirted, centre, rng, 20);
			const bool		 holds		= properties.holdsShape && properties.rimKept && simpleAround(skirted, centre) &&
								 properties.farthestReachMm <= static_cast<double>(kSeenSkirtMm + 1);
			failures += holds ? 0 : 1;
			if (!holds) {
				std::printf("  fail: holds %d rim %d reach %.2f at (%lld, %lld)\n", properties.holdsShape ? 1 : 0, properties.rimKept ? 1 : 0, properties.farthestReachMm,
							static_cast<long long>(centre.x), static_cast<long long>(centre.y));
			}
			farthest = std::max(farthest, properties.farthestReachMm);
			shapeVertices += properties.shapeVertices;
			skirtedVertices += properties.skirtedVertices;
		}
	}
	std::printf("[skirt fuzz] %d polygons, %d skipped, %d failed; farthest reach %.2f mm; vertices %zu true, %zu skirted\n", polygons, skipped, failures, farthest,
				shapeVertices, skirtedVertices);
	EXPECT_EQ(failures, 0);
	EXPECT_LE(skipped * 50, polygons);
}
