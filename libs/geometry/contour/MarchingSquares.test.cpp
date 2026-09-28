#include "MarchingSquares.h"
#include "../polygon/Polygon.h"
#include "ScalarField.h"
#include "Smoothing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Field with every sample 0 except the listed inside samples set to `value`.
	ScalarField fieldWith(int width, int height, const std::vector<std::pair<int, int>>& inside, float value = 1.0F) {
		ScalarField f({0, 0}, 1000, width, height);
		for (const auto& [x, y] : inside) {
			f.at(x, y) = value;
		}
		return f;
	}

	Int128 area2(const Ring& r) { return signedAreaDoubled(r); }

	// A smooth random field: a few random sinusoids, with the border forced below
	// iso so every loop closes.
	ScalarField smoothRandomField(std::uint32_t seed, Vec2i64 origin, std::int64_t cellMm, int width, int height) {
		std::mt19937						   rng(seed);
		std::uniform_real_distribution<double> freq(0.0005, 0.004);
		std::uniform_real_distribution<double> phase(0.0, 6.283);
		std::uniform_real_distribution<double> amp(0.2, 0.6);
		struct Wave {
			double fx, fy, ph, a;
		};
		std::vector<Wave> waves;
		for (int i = 0; i < 5; ++i) {
			waves.push_back({freq(rng), freq(rng), phase(rng), amp(rng)});
		}
		ScalarField f(origin, cellMm, width, height);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
					continue;
				}
				const Vec2i64 p = f.samplePositionMm(x, y);
				double		  v = 0.5;
				for (const Wave& w : waves) {
					v += w.a * std::sin(static_cast<double>(p.x) * w.fx + static_cast<double>(p.y) * w.fy + w.ph);
				}
				f.at(x, y) = static_cast<float>(v);
			}
		}
		return f;
	}

} // namespace

TEST(MarchingSquares, SingleInsideSampleIsDiamond) {
	const ScalarField f = fieldWith(3, 3, {{1, 1}});
	const std::vector<Ring> rings = marchingSquares(f, 0.5F);
	ASSERT_EQ(rings.size(), 1u);
	// Crossings at the edge midpoints around the sample at (1000, 1000); the loop
	// starts at the first crossing in scan order (top edge of cell (0,0)).
	const Ring expected = {{500, 1000}, {1000, 500}, {1500, 1000}, {1000, 1500}};
	EXPECT_EQ(rings[0], expected);
	EXPECT_EQ(windingOrder(rings[0]), Winding::CounterClockwise);
}

TEST(MarchingSquares, InterpolatesCrossings) {
	// Sample at 0.75 against 0 neighbors: t = (0.5 - 0) / 0.75 from the lower
	// endpoint, i.e. 667 mm from the outer sample, 333 mm short of the center.
	const ScalarField f = fieldWith(3, 3, {{1, 1}}, 0.75F);
	const std::vector<Ring> rings = marchingSquares(f, 0.5F);
	ASSERT_EQ(rings.size(), 1u);
	const Ring expected = {{667, 1000}, {1000, 667}, {1333, 1000}, {1000, 1333}};
	EXPECT_EQ(rings[0], expected);
}

TEST(MarchingSquares, BlockIsChamferedSquareCcw) {
	// Inside samples 2..3 on both axes: straight sides at 1500 and 3500, each corner
	// cut by a 500 x 500 chamfer.
	const ScalarField f = fieldWith(6, 6, {{2, 2}, {3, 2}, {2, 3}, {3, 3}});
	const std::vector<Ring> rings = marchingSquares(f, 0.5F);
	ASSERT_EQ(rings.size(), 1u);
	EXPECT_EQ(rings[0].size(), 8u);
	EXPECT_EQ(windingOrder(rings[0]), Winding::CounterClockwise);
	EXPECT_TRUE(isSimple(rings[0]).pass);
	// 2000 x 2000 minus four 125 000 mm^2 chamfers, doubled.
	EXPECT_EQ(area2(rings[0]), Int128(2 * (4000000 - 4 * 125000)));
}

TEST(MarchingSquares, BlockWithHoleGivesCcwOuterAndCwHole) {
	std::vector<std::pair<int, int>> inside;
	for (int y = 1; y <= 5; ++y) {
		for (int x = 1; x <= 5; ++x) {
			if (x != 3 || y != 3) {
				inside.push_back({x, y});
			}
		}
	}
	const std::vector<Ring> rings = marchingSquares(fieldWith(7, 7, inside), 0.5F);
	ASSERT_EQ(rings.size(), 2u);
	// The outer loop's first crossing comes first in scan order.
	EXPECT_EQ(windingOrder(rings[0]), Winding::CounterClockwise);
	EXPECT_EQ(windingOrder(rings[1]), Winding::Clockwise);
	// The hole is the diamond around the outside sample at (3000, 3000).
	EXPECT_EQ(area2(rings[1]), Int128(-2 * 500000));
	for (const Ring& r : rings) {
		EXPECT_TRUE(isSimple(r).pass);
	}
}

TEST(MarchingSquares, SaddleConnectsWhenAverageAtOrAboveIso) {
	// Diagonal inside samples; the saddle cell's average is (1 + 1 + 0 + 0) / 4 = 0.5,
	// exactly iso, which connects.
	const std::vector<Ring> rings = marchingSquares(fieldWith(4, 4, {{1, 1}, {2, 2}}), 0.5F);
	ASSERT_EQ(rings.size(), 1u);
	EXPECT_EQ(windingOrder(rings[0]), Winding::CounterClockwise);
	EXPECT_TRUE(isSimple(rings[0]).pass);
}

TEST(MarchingSquares, SaddleSeparatesWhenAverageBelowIso) {
	// Inside samples at 0.6: average 0.3 < 0.5, the two corners stay apart.
	const std::vector<Ring> rings = marchingSquares(fieldWith(4, 4, {{1, 1}, {2, 2}}, 0.6F), 0.5F);
	ASSERT_EQ(rings.size(), 2u);
	for (const Ring& r : rings) {
		EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
		EXPECT_TRUE(isSimple(r).pass);
	}
}

TEST(MarchingSquares, OriginIndependent) {
	// Field B holds field A's samples at the same world positions, padded with extra
	// outside samples and so anchored at a different origin. Same world data, same
	// rings, bit for bit and in the same order.
	const Vec2i64	  originA{-7250, 13000};
	const ScalarField a = smoothRandomField(7, originA, 250, 40, 36);
	ScalarField		  b({originA.x - 3 * 250, originA.y - 5 * 250}, 250, 40 + 7, 36 + 9);
	for (int y = 0; y < a.height; ++y) {
		for (int x = 0; x < a.width; ++x) {
			b.at(x + 3, y + 5) = a.at(x, y);
		}
	}
	const std::vector<Ring> ringsA = marchingSquares(a, 0.5F);
	const std::vector<Ring> ringsB = marchingSquares(b, 0.5F);
	ASSERT_FALSE(ringsA.empty());
	EXPECT_EQ(ringsA, ringsB);
}

TEST(MarchingSquares, SampleAtIsoKeepsCrossingsOffTheSample) {
	// A sample exactly at iso would put its neighbor crossings on the sample
	// itself; they stay 1 mm inside their edges instead, so the ring stays simple.
	ScalarField f = fieldWith(4, 3, {{1, 1}, {2, 1}});
	f.at(2, 1)	  = 0.5F;
	const std::vector<Ring> rings = marchingSquares(f, 0.5F);
	ASSERT_EQ(rings.size(), 1u);
	EXPECT_TRUE(isSimple(rings[0]).pass);
	EXPECT_NE(std::find(rings[0].begin(), rings[0].end(), Vec2i64{2001, 1000}), rings[0].end());
	EXPECT_EQ(std::find(rings[0].begin(), rings[0].end(), Vec2i64{2000, 1000}), rings[0].end());
}

TEST(MarchingSquares, PinchThroughIsoSampleStaysSimple) {
	// A one-sample-wide vertical bar whose middle sample is exactly at iso. Both
	// strands of the loop pass that sample, one each side; rounded onto it they
	// would touch, kept 1 mm inside their edges they stay apart.
	ScalarField f = fieldWith(5, 5, {{2, 1}, {2, 2}, {2, 3}});
	f.at(2, 2)	  = 0.5F;
	const std::vector<Ring> rings = marchingSquares(f, 0.5F);
	ASSERT_EQ(rings.size(), 1u);
	EXPECT_TRUE(isSimple(rings[0]).pass);
	EXPECT_NE(std::find(rings[0].begin(), rings[0].end(), Vec2i64{1999, 2000}), rings[0].end());
	EXPECT_NE(std::find(rings[0].begin(), rings[0].end(), Vec2i64{2001, 2000}), rings[0].end());
}

TEST(MarchingSquares, BorderSampleAtIsoViolatesPrecondition) {
	const ScalarField f = fieldWith(3, 3, {{0, 1}});
#ifdef NDEBUG
	EXPECT_TRUE(marchingSquares(f, 0.5F).empty());
#else
	EXPECT_DEATH(marchingSquares(f, 0.5F), "border sample");
#endif
}

TEST(MarchingSquares, RandomSmoothFieldsGiveSimpleDisjointRings) {
	for (std::uint32_t seed = 1; seed <= 6; ++seed) {
		const ScalarField f = smoothRandomField(seed, {static_cast<std::int64_t>(seed) * 1237, -5000}, 250, 80, 64);
		std::vector<Ring> rings = marchingSquares(f, 0.5F);
		ASSERT_FALSE(rings.empty()) << "seed " << seed;
		// Every crossing sits on its own lattice edge: no vertex is shared.
		std::vector<Vec2i64> all;
		for (const Ring& r : rings) {
			all.insert(all.end(), r.begin(), r.end());
		}
		std::sort(all.begin(), all.end());
		EXPECT_EQ(std::adjacent_find(all.begin(), all.end()), all.end()) << "seed " << seed;
		for (Ring& r : rings) {
			EXPECT_TRUE(isSimple(r).pass) << "seed " << seed;
			chaikin(r, 1);
			EXPECT_TRUE(isSimple(r).pass) << "chaikin, seed " << seed;
		}
	}
}
