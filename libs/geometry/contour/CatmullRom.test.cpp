#include "CatmullRom.h"

#include <cmath>
#include <cstddef>
#include <span>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// A meandering river centerline far from the origin, points ~20 m apart.
	std::vector<Vec2d> meander(int count) {
		std::vector<Vec2d> points;
		for (int i = 0; i < count; ++i) {
			const double s = 20.0 * i;
			points.push_back({731245.25 + s + 3.0 * std::sin(0.11 * s), -48211.5 + 25.0 * std::sin(0.037 * s + 0.4)});
		}
		return points;
	}

	std::vector<double> widths(int count) {
		std::vector<double> hw;
		for (int i = 0; i < count; ++i) {
			hw.push_back(2.0 + 0.05 * i + 0.3 * std::sin(0.7 * i));
		}
		return hw;
	}

	std::vector<CenterlineSample> samplesOfSegment(const std::vector<CenterlineSample>& all, std::size_t segment) {
		std::vector<CenterlineSample> out;
		for (const CenterlineSample& s : all) {
			if (s.segment == segment) {
				out.push_back(s);
			}
		}
		return out;
	}

} // namespace

TEST(SampleCatmullRom, TwoPointsGiveEvenlySpacedStraightLine) {
	const std::vector<Vec2d>  points = {{10.0, 5.0}, {12.0, 5.0}};
	const std::vector<double> hw	 = {1.0, 3.0};
	const auto				  out	 = sampleCatmullRom(points, hw, 0.5);
	ASSERT_EQ(out.size(), 5u);
	for (std::size_t i = 0; i < out.size(); ++i) {
		EXPECT_NEAR(out[i].position.x, 10.0 + 0.5 * static_cast<double>(i), 1e-12);
		EXPECT_NEAR(out[i].position.y, 5.0, 1e-12);
		EXPECT_NEAR(out[i].halfWidthM, 1.0 + 0.5 * static_cast<double>(i), 1e-12);
		EXPECT_EQ(out[i].segment, 0u);
	}
	EXPECT_EQ(out.back().t, 1.0);
}

TEST(SampleCatmullRom, StepCountPerSpanIsCeilOfChordOverSpacing) {
	// Chords 1.0, 1.2, 0.3 at spacing 0.5: 2, 3, and 1 steps, plus the last point.
	const std::vector<Vec2d>  points = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.2}, {1.3, 1.2}};
	const std::vector<double> hw	 = {1.0, 1.0, 1.0, 1.0};
	const auto				  out	 = sampleCatmullRom(points, hw, 0.5);
	ASSERT_EQ(out.size(), 7u);
	EXPECT_EQ(samplesOfSegment(out, 0).size(), 2u);
	EXPECT_EQ(samplesOfSegment(out, 1).size(), 3u);
	EXPECT_EQ(samplesOfSegment(out, 2).size(), 2u); // one step plus the final point
}

TEST(SampleCatmullRom, EveryInputPointAppearsExactlyOnce) {
	const std::vector<Vec2d>  points = meander(12);
	const std::vector<double> hw	 = widths(12);
	const auto				  out	 = sampleCatmullRom(points, hw, 0.5);
	std::size_t				  next	 = 0;
	for (const CenterlineSample& s : out) {
		const bool isInputPoint = s.t == 0.0 || (s.t == 1.0 && s.segment == points.size() - 2);
		if (isInputPoint) {
			ASSERT_LT(next, points.size());
			EXPECT_EQ(s.position, points[next]);
			EXPECT_EQ(s.halfWidthM, hw[next]);
			++next;
		}
	}
	EXPECT_EQ(next, points.size());
}

TEST(SampleCatmullRom, HalfWidthIsLinearWithinASpan) {
	const std::vector<Vec2d>  points = meander(6);
	const std::vector<double> hw	 = widths(6);
	for (const CenterlineSample& s : sampleCatmullRom(points, hw, 0.5)) {
		EXPECT_NEAR(s.halfWidthM, hw[s.segment] + s.t * (hw[s.segment + 1] - hw[s.segment]), 1e-12);
	}
}

TEST(SampleCatmullRom, SpacingStaysNearTheLimit) {
	const std::vector<Vec2d>  points = meander(20);
	const std::vector<double> hw	 = widths(20);
	const auto				  out	 = sampleCatmullRom(points, hw, 0.5);
	// Steps are equal in the centripetal parameter, whose speed varies along a
	// span, so gaps wander around the chord step rather than sitting under it.
	for (std::size_t i = 1; i < out.size(); ++i) {
		const double gap = length(out[i].position - out[i - 1].position);
		EXPECT_GT(gap, 0.2);
		EXPECT_LT(gap, 0.6);
	}
}

TEST(SampleCatmullRom, SamplesDependOnlyOnNearbyPoints) {
	// Two chunks gather the same river cut at different places. Dropping the
	// first three points changes spans 0..3 (span 3 loses its real p[i-1]) and
	// nothing from span 4 on: those samples must be bit-identical.
	const std::vector<Vec2d>  points = meander(30);
	const std::vector<double> hw	 = widths(30);
	const auto				  full	 = sampleCatmullRom(points, hw, 0.5);
	const auto				  cut	 = sampleCatmullRom(std::span(points).subspan(3), std::span(hw).subspan(3), 0.5);

	std::size_t compared = 0;
	for (std::size_t segment = 4; segment + 1 < points.size(); ++segment) {
		const auto a = samplesOfSegment(full, segment);
		const auto b = samplesOfSegment(cut, segment - 3);
		ASSERT_EQ(a.size(), b.size());
		for (std::size_t i = 0; i < a.size(); ++i) {
			EXPECT_EQ(a[i].position, b[i].position);
			EXPECT_EQ(a[i].halfWidthM, b[i].halfWidthM);
			EXPECT_EQ(a[i].t, b[i].t);
			++compared;
		}
	}
	EXPECT_GT(compared, 1000u);

	// Span 3 is the cut input's first span, which runs off a phantom point.
	const auto a = samplesOfSegment(full, 3);
	const auto b = samplesOfSegment(cut, 0);
	ASSERT_EQ(a.size(), b.size());
	EXPECT_NE(a[a.size() / 2].position, b[b.size() / 2].position);
}
