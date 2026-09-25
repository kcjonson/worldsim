#include "WarpField.h"
#include "../polygon/Polygon.h"
#include "MarchingSquares.h"
#include "ScalarField.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	constexpr std::int64_t kMaxOffsetMm = 1450;

	// A smooth, bounded world-space offset (|x|, |y| <= 1450 mm), pure in its input.
	WarpOffsetMm wobble(Vec2i64 w) {
		const double x = static_cast<double>(w.x);
		const double y = static_cast<double>(w.y);
		return {
			1000.0 * std::sin(x * 0.00031 + y * 0.00017) + 450.0 * std::sin(y * 0.0011 + 1.3),
			1000.0 * std::cos(x * 0.00023 - y * 0.00029) + 450.0 * std::sin(x * 0.0013 + 0.4)
		};
	}

	// Tile-scale blobs: a few water "tiles" softened into 0/0.25/0.5/1 values,
	// surrounded by a wide land margin so the fine field's border stays dry.
	ScalarField blobField(Vec2i64 origin, int width, int height) {
		ScalarField f(origin, 1000, width, height);
		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				const bool lake	 = (x - 14) * (x - 14) + (y - 12) * (y - 12) < 30;
				const bool pond	 = x >= 24 && x <= 26 && y >= 8 && y <= 9;
				const bool pool	 = x == 9 && y == 22;
				f.at(x, y)		 = lake || pond ? 1.0F : (pool ? 0.8F : 0.0F);
			}
		}
		f.at(20, 20) = 0.3F;
		return f;
	}

	bool bitEqual(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

} // namespace

TEST(WarpField, ZeroOffsetOnCoarseLatticeReproducesSamples) {
	const ScalarField coarse = blobField({3000, -2000}, 36, 32);
	const ScalarField fine =
		warpField(coarse, coarse.originMm, coarse.cellMm, coarse.width, coarse.height, [](Vec2i64) { return WarpOffsetMm{}; }, 0.0F, {});
	for (int y = 0; y < coarse.height; ++y) {
		for (int x = 0; x < coarse.width; ++x) {
			EXPECT_EQ(fine.at(x, y), coarse.at(x, y));
		}
	}
}

TEST(WarpField, OffGridReadsOutsideValue) {
	const ScalarField coarse({0, 0}, 1000, 2, 2, 1.0F);
	const ScalarField fine = warpField(coarse, {-5000, -5000}, 1000, 2, 2, [](Vec2i64) { return WarpOffsetMm{}; }, 0.25F, {});
	EXPECT_EQ(fine.at(0, 0), 0.25F);
	EXPECT_EQ(fine.at(1, 1), 0.25F);
}

TEST(WarpField, OriginIndependent) {
	// Coarse B holds coarse A's samples at the same world positions inside a larger
	// grid with a different origin; the fine grids differ in origin too but share
	// sample positions. Over the overlap every fine sample must match bit for bit.
	const ScalarField a = blobField({-11000, 4000}, 36, 32);
	ScalarField		  b({a.originMm.x - 6000, a.originMm.y - 3000}, 1000, 36 + 10, 32 + 8);
	for (int y = 0; y < a.height; ++y) {
		for (int x = 0; x < a.width; ++x) {
			b.at(x + 6, y + 3) = a.at(x, y);
		}
	}
	const Vec2i64 fineOriginA{a.originMm.x + 2000, a.originMm.y + 2000};
	const Vec2i64 fineOriginB{fineOriginA.x - 250 * 7, fineOriginA.y + 250 * 5};
	const ScalarField fineA = warpField(a, fineOriginA, 250, 120, 100, wobble, 0.0F, {});
	const ScalarField fineB = warpField(b, fineOriginB, 250, 120, 100, wobble, 0.0F, {});
	int				  compared = 0;
	for (int y = 0; y < fineA.height; ++y) {
		for (int x = 0; x < fineA.width; ++x) {
			const int bx = x + 7;
			const int by = y - 5;
			if (bx < 0 || by < 0 || bx >= fineB.width || by >= fineB.height) {
				continue;
			}
			ASSERT_TRUE(bitEqual(fineA.at(x, y), fineB.at(bx, by))) << x << "," << y;
			++compared;
		}
	}
	EXPECT_GT(compared, 5000);
}

TEST(WarpField, SkipGivesIdenticalContoursAndSkipsMostCalls) {
	// Mostly dry, like most of a real chunk: the blobs sit in one corner.
	const ScalarField coarse = blobField({-4000, 2000}, 80, 72);
	// Fine lattice over the coarse grid minus a 3 m margin, so the fine border reads
	// only dry samples.
	const Vec2i64 fineOrigin{coarse.originMm.x + 3000, coarse.originMm.y + 3000};
	const int	  fineW = (80 - 7) * 4;
	const int	  fineH = (72 - 7) * 4;

	int				   callsFull = 0;
	int				   callsSkip = 0;
	const WarpFunction countFull = [&](Vec2i64 w) {
		++callsFull;
		return wobble(w);
	};
	const WarpFunction countSkip = [&](Vec2i64 w) {
		++callsSkip;
		return wobble(w);
	};
	const ScalarField full	  = warpField(coarse, fineOrigin, 250, fineW, fineH, countFull, 0.0F, {});
	const ScalarField skipped = warpField(coarse, fineOrigin, 250, fineW, fineH, countSkip, 0.0F, WarpSkip{0.5F, kMaxOffsetMm});

	EXPECT_EQ(callsFull, fineW * fineH);
	EXPECT_LT(callsSkip, callsFull / 4);
	EXPECT_GT(callsSkip, 0);

	const std::vector<Ring> ringsFull = marchingSquares(full, 0.5F);
	const std::vector<Ring> ringsSkip = marchingSquares(skipped, 0.5F);
	ASSERT_FALSE(ringsFull.empty());
	EXPECT_EQ(ringsFull, ringsSkip);
	for (const Ring& r : ringsFull) {
		EXPECT_TRUE(isSimple(r).pass);
	}
}
