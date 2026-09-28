// TerrainPolygonQuery and the shore points it filters
// (terrain-polygons-architecture.md D3, D11): distance, nearest shore point, and
// containment against brute force over every ring, the edges that are not
// shore (synthetic closures, fordable cuts), and where shore points stand.

#include "world/chunk/TerrainPolygonQuery.h"

#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TerrainPolygonTestSupport.h"

#include <predicates/Predicates.h>
#include <random/HashNoise.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

using namespace engine::world;
using namespace engine::world::terrain_test;

namespace {

	using Builder = TerrainPolygonBuilder;

	constexpr uint64_t kSeed = 0x0E7A1C5ULL;

	// Chunk (0, 0): a lake 120 m across around a 30 m island, and ocean from
	// x = 480 m out past the extended region's east edge.
	Biome queryWorld(int64_t tx, int64_t ty) {
		const double lx = static_cast<double>(tx) + 0.5 - 150.0;
		const double ly = static_cast<double>(ty) + 0.5 - 150.0;
		const double r2 = lx * lx + ly * ly;
		if (r2 < 60.0 * 60.0 && r2 >= 15.0 * 15.0) {
			return Biome::Lake;
		}
		return tx >= 480 ? Biome::Ocean : Biome::TemperateGrassland;
	}

	// A river into the ocean, a creek tapering through the fordable width, and a pond.
	std::vector<Builder::RiverSegment> queryRivers() {
		std::vector<Builder::RiverSegment> out = riverSegmentsThrough({{250.0, 350.0}, {330.0, 356.0}, {410.0, 346.0}, {470.0, 352.0}, {500.0, 350.0}}, 3.0F);
		std::vector<geometry::Vec2d> creek;
		for (int i = 0; i <= 30; ++i) {
			creek.push_back({250.0 + 5.0 * i, 420.0 + 3.0 * std::sin(0.3 * i)});
		}
		for (Builder::RiverSegment s : riverSegmentsThrough(creek, 1.0F)) {
			// Half-width falls from 1 m to 0.4 m, through the 0.6 m fordable threshold.
			s.halfWidth0 = static_cast<float>(1.0 - 0.6 * (s.x0 - 250.0) / 150.0);
			s.halfWidth1 = static_cast<float>(1.0 - 0.6 * (s.x1 - 250.0) / 150.0);
			out.push_back(s);
		}
		return out;
	}

	const std::vector<Builder::Pond> kPonds = {{350.0, 150.0, 10.0F, 0.7F, 1.9F, 100}};

	struct Built {
		HandTiles			 tiles;
		ChunkTerrainPolygons polys;
	};

	const Built& queryBuild() {
		static const Built built = [] {
			HandTiles				 tiles({0, 0}, queryWorld);
			const auto				 rivers = queryRivers();
			ChunkTerrainPolygons	 polys	= buildHand(tiles, kSeed, rivers, kPonds);
			return Built{std::move(tiles), std::move(polys)};
		}();
		return built;
	}

	// D9's classification with fordable channels in, by pointInPolygon over every ring.
	bool bruteWater(const ChunkTerrainPolygons& polys, const Vec2i64& p, bool& onBoundary) {
		int	 parity = 0;
		bool solid	= false;
		for (const TerrainRing& ring : polys.rings) {
			const geometry::PointInPolygon where = geometry::pointInPolygon(p, ring.ring);
			onBoundary							 = onBoundary || where == geometry::PointInPolygon::OnBoundary;
			if (where == geometry::PointInPolygon::Inside) {
				parity += ring.holeCapable ? 1 : 0;
				solid = solid || !ring.holeCapable;
			}
		}
		return solid || parity % 2 == 1;
	}

	double bruteShoreDistance(const ChunkTerrainPolygons& polys, const Vec2i64& p) {
		double best = std::numeric_limits<double>::infinity();
		for (const TerrainRing& ring : polys.rings) {
			for (size_t i = 0; i < ring.ring.size(); ++i) {
				if (ring.isShoreEdge(i)) {
					best = std::min(best, geometry::closestOnSegment(p, ring.ring[i], ring.ring[(i + 1) % ring.ring.size()]).distanceMm);
				}
			}
		}
		return best;
	}

	// Deterministic points over the chunk square plus 10 m.
	std::vector<Vec2i64> samplePoints(int count) {
		std::vector<Vec2i64> out;
		for (int i = 0; i < count; ++i) {
			const uint32_t hx = foundation::hash3(i, 1, 0, 77U);
			const uint32_t hy = foundation::hash3(i, 2, 0, 77U);
			out.push_back({static_cast<int64_t>(hx % 532000U) - 10000, static_cast<int64_t>(hy % 532000U) - 10000});
		}
		return out;
	}

	double length(const Vec2i64& a, const Vec2i64& b) {
		const auto dx = static_cast<double>(a.x - b.x);
		const auto dy = static_cast<double>(a.y - b.y);
		return std::sqrt(dx * dx + dy * dy);
	}

} // namespace

// Every answer against brute force over every ring and edge: distance (0 in
// water, else to the nearest shore edge, unbounded and capped), the nearest
// shore point, and containment.
TEST(TerrainPolygonQueryTest, MatchesBruteForceEverywhere) {
	const ChunkTerrainPolygons& polys = queryBuild().polys;
	const TerrainPolygonQuery	query(polys);
	int							water	= 0;
	int							checked = 0;
	for (const Vec2i64& p : samplePoints(4000)) {
		bool	   onBoundary = false;
		const bool inWater	  = bruteWater(polys, p, onBoundary);
		if (onBoundary) {
			continue;
		}
		++checked;
		water += inWater ? 1 : 0;
		ASSERT_EQ(query.isInsideWater(p), inWater) << p.x << ", " << p.y;
		const double shore = bruteShoreDistance(polys, p);
		const double truth = inWater ? 0.0 : shore;
		ASSERT_EQ(query.distanceToWaterMm(p), truth) << p.x << ", " << p.y;
		ASSERT_EQ(query.distanceToWaterMm(p, 3000.0), truth <= 3000.0 ? truth : TerrainPolygonQuery::kUnbounded) << p.x << ", " << p.y;
		const std::optional<Vec2i64> nearest = query.nearestShorePoint(p);
		ASSERT_TRUE(nearest.has_value());
		EXPECT_NEAR(length(*nearest, p), shore, 1.0) << p.x << ", " << p.y;
	}
	EXPECT_GT(checked, 3900);
	EXPECT_GT(water, 300) << "enough points land in the lake, the ocean, and the rivers";
}

// The lake's island is a hole: land, with water all around it.
TEST(TerrainPolygonQueryTest, AnIslandIsLand) {
	const TerrainPolygonQuery query(queryBuild().polys);
	EXPECT_FALSE(query.isInsideWater({150000, 150000}));
	EXPECT_GT(query.distanceToWaterMm({150000, 150000}), 10000.0);
	EXPECT_TRUE(query.isInsideWater({150000, 190000}));
	EXPECT_EQ(query.distanceToWaterMm({190000, 150000}), 0.0);
	EXPECT_FALSE(query.isInsideWater({150000, 250000}));
}

// The ocean closes along the extended region's east edge (D4). From the water
// just inside that edge, the nearest shore is the real coast 50 m west, not
// the synthetic closure half a meter away.
TEST(TerrainPolygonQueryTest, SyntheticEdgesAreNotShore) {
	const ChunkTerrainPolygons& polys = queryBuild().polys;
	const TerrainPolygonQuery	query(polys);
	int							synthetic = 0;
	for (const TerrainRing& ring : polys.rings) {
		for (size_t i = 0; i < ring.ring.size(); ++i) {
			synthetic += (ring.profiles[i].flags & ShoreProfile::kFlagSynthetic) != 0 ? 1 : 0;
		}
	}
	ASSERT_GT(synthetic, 0);
	const Vec2i64 nearClosure{531500, 200000};
	ASSERT_TRUE(query.isInsideWater(nearClosure));
	const std::optional<Vec2i64> nearest = query.nearestShorePoint(nearClosure);
	ASSERT_TRUE(nearest.has_value());
	EXPECT_LT(nearest->x, 490000);
}

// The creek splits where it narrows through the fordable width, and the butt
// edge the two pieces share is inside the river: from its middle the nearest
// shore is a bank, not the cut. The fordable piece is water.
TEST(TerrainPolygonQueryTest, FordableCutsAreNotShoreAndFordableWaterIsWater) {
	const ChunkTerrainPolygons& polys = queryBuild().polys;
	const TerrainPolygonQuery	query(polys);
	std::optional<Vec2i64>		cutMiddle;
	for (const TerrainRing& ring : polys.rings) {
		for (size_t i = 0; i < ring.ring.size() && !cutMiddle; ++i) {
			if (!ring.isShoreEdge(i) && (ring.profiles[i].flags & ShoreProfile::kFlagSynthetic) == 0) {
				const Vec2i64& a = ring.ring[i];
				const Vec2i64& b = ring.ring[(i + 1) % ring.ring.size()];
				cutMiddle		 = Vec2i64{(a.x + b.x) / 2, (a.y + b.y) / 2};
			}
		}
	}
	ASSERT_TRUE(cutMiddle.has_value()) << "the creek must cross the fordable width";
	EXPECT_EQ(query.distanceToWaterMm(*cutMiddle), 0.0);
	const std::optional<Vec2i64> nearest = query.nearestShorePoint(*cutMiddle);
	ASSERT_TRUE(nearest.has_value());
	EXPECT_GT(length(*nearest, *cutMiddle), 100.0) << "on a bank, not on the cut";

	bool sawFordable = false;
	for (const TerrainRing& ring : polys.rings) {
		sawFordable = sawFordable || (ring.kind == TerrainRingKind::Channel && !ring.blocksMovement);
	}
	EXPECT_TRUE(sawFordable);
	// The creek's thin end, 0.45 m half-width, on its centerline.
	EXPECT_TRUE(query.isInsideWater({375000, static_cast<int64_t>(std::llround((420.0 + 3.0 * std::sin(0.3 * 25.0)) * 1000.0))}));
}

// Shore points (D11): in the chunk square, on land, kShoreOffsetMm off the
// shore, about kShorePointSpacingMm apart along it, and along every kind of
// shore including the fordable creek.
TEST(TerrainPolygonQueryTest, ShorePointsStandOnLandBesideTheShore) {
	const ChunkTerrainPolygons& polys = queryBuild().polys;
	const TerrainPolygonQuery	query(polys);
	ASSERT_GT(polys.shorePoints.size(), 800U);

	int creek	 = 0;
	int atOffset = 0;
	for (const Vec2i64& p : polys.shorePoints) {
		EXPECT_TRUE(p.x >= 0 && p.x < kChunkMm && p.y >= 0 && p.y < kChunkMm) << p.x << ", " << p.y;
		EXPECT_FALSE(query.isInsideWater(p)) << p.x << ", " << p.y;
		const double d = query.distanceToWaterMm(p);
		EXPECT_LE(d, static_cast<double>(Builder::kShoreOffsetMm) + 1.0) << p.x << ", " << p.y;
		atOffset += d >= static_cast<double>(Builder::kShoreOffsetMm) - 10.0 ? 1 : 0;
		creek += p.x > 260000 && p.x < 390000 && std::abs(p.y - 420000) < 5000 ? 1 : 0;
	}
	EXPECT_GT(atOffset, static_cast<int>(polys.shorePoints.size()) * 9 / 10) << "off a straight or convex shore, exactly the offset";
	EXPECT_GT(creek, 200) << "both banks of the creek, fordable piece included";

	// Consecutive points along one run of shore: a meter apart, a lattice anchor
	// between them making it anywhere from half a meter to a meter and a half.
	// Where one ring's points end and the next ring's start is anything, so the
	// tails are percentiles.
	std::vector<double> gaps;
	for (size_t i = 1; i < polys.shorePoints.size(); ++i) {
		const double gap = length(polys.shorePoints[i], polys.shorePoints[i - 1]);
		if (gap < 3000.0) {
			gaps.push_back(gap);
		}
	}
	ASSERT_GT(gaps.size(), 700U);
	std::sort(gaps.begin(), gaps.end());
	EXPECT_NEAR(gaps[gaps.size() / 2], static_cast<double>(Builder::kShorePointSpacingMm), 30.0);
	EXPECT_GE(gaps[gaps.size() / 20], 400.0);
	EXPECT_LE(gaps[gaps.size() * 19 / 20], 1600.0);
}

// The river's run into the ocean: its banks past the coast are submerged, so no
// shore point stands in the ocean, while the coast itself carries them.
TEST(TerrainPolygonQueryTest, NoShorePointOnABankSubmergedInTheSea) {
	const ChunkTerrainPolygons& polys = queryBuild().polys;
	int							coast = 0;
	for (const Vec2i64& p : polys.shorePoints) {
		EXPECT_LT(p.x, 482000) << "no shore point out in the ocean";
		coast += p.x > 476000 ? 1 : 0;
	}
	EXPECT_GT(coast, 400);
}
