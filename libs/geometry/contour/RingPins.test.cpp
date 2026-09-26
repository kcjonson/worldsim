#include "RingPins.h"
#include "../offset/WallOffset.h"
#include "../polygon/Polygon.h"
#include "Smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	const std::vector<std::int64_t> kNoLines;

	// Rotate so the ring starts at its lexicographically smallest vertex, mask in step.
	void canonicalize(Ring& ring, std::vector<std::uint8_t>& mask) {
		const auto first = std::min_element(ring.begin(), ring.end()) - ring.begin();
		std::rotate(ring.begin(), ring.begin() + first, ring.end());
		std::rotate(mask.begin(), mask.begin() + first, mask.end());
	}

	// An irregular closed shore: a wobbly circle of radius ~20 m with 400 vertices.
	Ring wobblyCircle() {
		Ring r;
		for (int i = 0; i < 400; ++i) {
			const double a	 = 6.283185307179586 * i / 400.0;
			const double rad = 20000.0 + 1500.0 * std::sin(5.0 * a) + 700.0 * std::cos(13.0 * a + 0.3);
			r.push_back({std::llround(rad * std::cos(a)), std::llround(rad * std::sin(a))});
		}
		return r;
	}

} // namespace

TEST(PinAxisLineCrossings, InsertsCrossingWithExactLineCoordinate) {
	// Edge (0,0)->(3000,1000) crosses x = 1000 at y = 333.33 -> 333.
	Ring						  r		 = {{0, 0}, {3000, 1000}, {0, 2000}};
	const std::array<std::int64_t, 1> xLines = {1000};
	const std::vector<std::uint8_t>		  mask	 = pinAxisLineCrossings(r, xLines, kNoLines);
	const Ring					  expected = {{0, 0}, {1000, 333}, {3000, 1000}, {1000, 1667}, {0, 2000}};
	EXPECT_EQ(r, expected);
	const std::vector<std::uint8_t> expectedMask = {0, 1, 0, 1, 0};
	EXPECT_EQ(mask, expectedMask);
}

TEST(PinAxisLineCrossings, RoundingIsSymmetricInEdgeDirection) {
	// Exact crossings land on .5 in the off-line coordinate; both walking
	// directions must round them to the same integer.
	Ring							  forward = {{0, 0}, {2000, 1}, {2000, 3001}, {0, 3000}};
	Ring							  reverse(forward.rbegin(), forward.rend());
	const std::array<std::int64_t, 1> xLines = {1000};
	const std::array<std::int64_t, 1> yLines = {1500};
	pinAxisLineCrossings(forward, xLines, yLines);
	pinAxisLineCrossings(reverse, xLines, yLines);
	EXPECT_NE(std::find(forward.begin(), forward.end(), Vec2i64{1000, 1}), forward.end()); // 0.5 rounds up
	std::sort(forward.begin(), forward.end());
	std::sort(reverse.begin(), reverse.end());
	EXPECT_EQ(forward, reverse);
}

TEST(PinAxisLineCrossings, FlagsPreexistingOnLineVertices) {
	Ring							  r		 = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	const std::array<std::int64_t, 1> xLines = {1000};
	const std::vector<std::uint8_t>			  mask	 = pinAxisLineCrossings(r, xLines, kNoLines);
	EXPECT_EQ(r.size(), 4u); // touching the line is not a proper crossing
	const std::vector<std::uint8_t> expectedMask = {0, 1, 1, 0};
	EXPECT_EQ(mask, expectedMask);
}

TEST(PinAxisLineCrossings, LinePairThroughItsIntersectionGivesOnePoint) {
	// The diagonal passes exactly through (1000, 1000), where both lines meet.
	Ring							  r		 = {{0, 0}, {2000, 2000}, {0, 2000}};
	const std::array<std::int64_t, 1> xLines = {1000};
	const std::array<std::int64_t, 1> yLines = {1000};
	const std::vector<std::uint8_t>			  mask	 = pinAxisLineCrossings(r, xLines, yLines);
	EXPECT_EQ(std::count(r.begin(), r.end(), Vec2i64{1000, 1000}), 1);
	EXPECT_EQ(r.size(), mask.size());
}

TEST(PinAxisLineCrossings, LatticePinsEveryMultipleOnBothAxes) {
	// (-5000, 1000) -> (37000, 1000) crosses x = 0, 16000, 32000; the vertical
	// back edges cross y = 16000 once each side; negative coordinates floor.
	Ring							r	 = {{-5000, 1000}, {37000, 1000}, {37000, 20000}, {-5000, 20000}};
	const std::vector<std::uint8_t> mask = pinAxisLineCrossings(r, kNoLines, kNoLines, 16000);
	const Ring expected = {{-5000, 1000},	{0, 1000},	   {16000, 1000},  {32000, 1000}, {37000, 1000}, {37000, 16000},
						   {37000, 20000}, {32000, 20000}, {16000, 20000}, {0, 20000},	   {-5000, 20000}, {-5000, 16000}};
	EXPECT_EQ(r, expected);
	const std::vector<std::uint8_t> expectedMask = {0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1};
	EXPECT_EQ(mask, expectedMask);
}

TEST(PinAxisLineCrossings, LatticeAndExplicitLinesShareACrossingOnce) {
	// x = 16000 is both an explicit line and a lattice line; x = 20000 only explicit.
	Ring							   r	  = {{1000, 1000}, {40000, 3000}, {1000, 6000}};
	const std::array<std::int64_t, 2> xLines = {16000, 20000};
	const std::vector<std::uint8_t>	   mask	  = pinAxisLineCrossings(r, xLines, kNoLines, 16000);
	EXPECT_EQ(std::count_if(r.begin(), r.end(), [](const Vec2i64& v) { return v.x == 16000; }), 2);
	EXPECT_EQ(std::count_if(r.begin(), r.end(), [](const Vec2i64& v) { return v.x == 20000; }), 2);
	EXPECT_EQ(std::count_if(r.begin(), r.end(), [](const Vec2i64& v) { return v.x == 32000; }), 2);
	EXPECT_EQ(r.size(), 9u);
	EXPECT_EQ(std::count(mask.begin(), mask.end(), std::uint8_t{1}), 6);
}

// The point of lattice pins: a resampled run depends only on the curve between
// two lattice crossings. Two copies of one shore (vertices on one 97 mm grid),
// cut at different places far from the lattice cell [0, 16 m], resample
// identically inside that cell.
TEST(PinAxisLineCrossings, LatticeRunsResampleIndependentlyOfWhereTheRingIsCut) {
	auto shore = [](double x) { return 3000.0 + 900.0 * std::sin(x / 2300.0) + 400.0 * std::cos(x / 700.0); };
	auto build = [&shore](std::int64_t x0, std::int64_t x1) {
		Ring r;
		for (std::int64_t x = x0; x <= x1; x += 97) {
			r.push_back({x, std::llround(shore(static_cast<double>(x)))});
		}
		r.push_back({x1, -20000});
		r.push_back({x0, -20000});
		return r;
	};
	auto cellVertices = [](Ring r) {
		std::vector<std::uint8_t> pinned = pinAxisLineCrossings(r, kNoLines, kNoLines, 16000);
		resampleRing(r, 250, pinned);
		Ring inside;
		for (const Vec2i64& v : r) {
			if (v.x >= 0 && v.x <= 16000 && v.y > 0) {
				inside.push_back(v);
			}
		}
		return inside;
	};
	const Ring a = cellVertices(build(-97 * 73, 97 * 258));
	const Ring b = cellVertices(build(-97 * 134, 97 * 423));
	ASSERT_GT(a.size(), 50u);
	EXPECT_EQ(a, b);
}

TEST(ResampleRing, UnpinnedRingStartsAtVertexZero) {
	Ring			  r = {{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}};
	std::vector<std::uint8_t> pinned(r.size(), 0);
	resampleRing(r, 1000, pinned);
	ASSERT_EQ(r.size(), 16u);
	EXPECT_EQ(r[0], (Vec2i64{0, 0}));
	EXPECT_EQ(r[1], (Vec2i64{1000, 0}));
	EXPECT_EQ(r[4], (Vec2i64{4000, 0}));
	EXPECT_EQ(r[15], (Vec2i64{0, 1000}));
	EXPECT_EQ(std::count(pinned.begin(), pinned.end(), std::uint8_t{1}), 0);
}

TEST(ResampleRing, KeepsPinsAndSpacing) {
	Ring							  r		 = wobblyCircle();
	const std::array<std::int64_t, 2> xLines = {-5000, 12000};
	const std::array<std::int64_t, 1> yLines = {3000};
	std::vector<std::uint8_t>				  pinned = pinAxisLineCrossings(r, xLines, yLines);
	Ring							  pins;
	for (std::size_t i = 0; i < r.size(); ++i) {
		if (pinned[i]) {
			pins.push_back(r[i]);
		}
	}
	ASSERT_EQ(pins.size(), 6u);

	resampleRing(r, 250, pinned);
	ASSERT_EQ(r.size(), pinned.size());
	Ring pinsAfter;
	for (std::size_t i = 0; i < r.size(); ++i) {
		if (pinned[i]) {
			pinsAfter.push_back(r[i]);
		}
	}
	EXPECT_EQ(pinsAfter, pins);
	for (std::size_t i = 0; i < r.size(); ++i) {
		const Vec2i64 d	  = r[(i + 1) % r.size()] - r[i];
		const double  len = std::sqrt(static_cast<double>(d.x) * d.x + static_cast<double>(d.y) * d.y);
		EXPECT_GT(len, 125.0);
		EXPECT_LT(len, 375.0);
	}
	EXPECT_TRUE(isSimple(r).pass);
}

TEST(ResampleRing, PinnedPipelineIsIndependentOfStartVertex) {
	// The seam guarantee: the same shoreline starting at a different vertex gives
	// the same vertices after pin + chaikin-free resample + simplify.
	const std::array<std::int64_t, 2> xLines = {-5000, 12000};
	const std::array<std::int64_t, 2> yLines = {-9000, 3000};
	auto run = [&](Ring r) {
		std::vector<std::uint8_t> pinned = pinAxisLineCrossings(r, xLines, yLines);
		resampleRing(r, 250, pinned);
		simplifyRing(r, 100, pinned);
		canonicalize(r, pinned);
		return std::make_pair(r, pinned);
	};
	const Ring base	   = wobblyCircle();
	Ring	   rotated = base;
	std::rotate(rotated.begin(), rotated.begin() + 137, rotated.end());
	EXPECT_EQ(run(base), run(rotated));
}
