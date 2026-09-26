#include "Stroke.h"
#include "../polygon/Polygon.h"
#include "CatmullRom.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	StrokeArgs strokeArgs(
		const std::vector<Vec2d>&  c,
		const std::vector<double>& left,
		const std::vector<double>& right,
		StrokeCap				   start,
		StrokeCap				   end
	) {
		return {c, left, right, start, end, 0.1};
	}

	// A meandering river centerline far from the origin, points ~20 m apart.
	std::vector<Vec2d> meander(int count) {
		std::vector<Vec2d> points;
		for (int i = 0; i < count; ++i) {
			const double s = 20.0 * i;
			points.push_back({731245.25 + s + 3.0 * std::sin(0.11 * s), -48211.5 + 25.0 * std::sin(0.037 * s + 0.4)});
		}
		return points;
	}

	std::vector<double> widths(int count, double phase) {
		std::vector<double> hw;
		for (int i = 0; i < count; ++i) {
			hw.push_back(2.0 + 0.05 * i + 0.3 * std::sin(0.7 * i + phase));
		}
		return hw;
	}

	std::int64_t minX(const Ring& r) {
		return std::min_element(r.begin(), r.end(), [](const Vec2i64& a, const Vec2i64& b) { return a.x < b.x; })->x;
	}

	std::int64_t maxX(const Ring& r) {
		return std::max_element(r.begin(), r.end(), [](const Vec2i64& a, const Vec2i64& b) { return a.x < b.x; })->x;
	}

	bool contains(const Ring& r, Vec2i64 v) { return std::find(r.begin(), r.end(), v) != r.end(); }

} // namespace

TEST(StrokePolyline, StraightButtStrokeIsARectangle) {
	const std::vector<Vec2d>  c		= {{0.0, 0.0}, {10.0, 0.0}};
	const std::vector<double> one	= {1.0, 1.0};
	const Ring				  r		= strokePolyline(strokeArgs(c, one, one, StrokeCap::Butt, StrokeCap::Butt));
	const Ring				  expected = {{0, -1000}, {10000, -1000}, {10000, 1000}, {0, 1000}};
	EXPECT_EQ(r, expected);
	EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
}

TEST(StrokePolyline, StraightRoundStrokeIsAStadium) {
	const std::vector<Vec2d>  c	  = {{0.0, 0.0}, {10.0, 0.0}};
	const std::vector<double> one = {1.0, 1.0};
	const Ring				  r	  = strokePolyline(strokeArgs(c, one, one, StrokeCap::Round, StrokeCap::Round));

	// pi * 1 m of cap arc at 0.1 m spacing is 32 steps: 31 cap vertices per end.
	ASSERT_EQ(r.size(), 4u + 2u * 31u);
	EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
	EXPECT_TRUE(isSimple(r).pass);
	EXPECT_EQ(r[0], (Vec2i64{0, -1000}));
	EXPECT_TRUE(contains(r, {11000, 0}));
	EXPECT_TRUE(contains(r, {-1000, 0}));
	EXPECT_EQ(minX(r), -1000);
	EXPECT_EQ(maxX(r), 11000);

	// Rectangle plus two 32-step half-disc fans of area (32 / 2) r^2 sin(pi / 32).
	const double expectedArea = 20.0 + 32.0 * std::sin(std::numbers::pi / 32.0);
	EXPECT_NEAR(signedAreaSquareMeters(r), expectedArea, 5e-3);
}

TEST(StrokePolyline, ButtAndRoundCapsMix) {
	const std::vector<Vec2d>  c	  = {{0.0, 0.0}, {10.0, 0.0}};
	const std::vector<double> one = {1.0, 1.0};

	const Ring roundEnd = strokePolyline(strokeArgs(c, one, one, StrokeCap::Butt, StrokeCap::Round));
	EXPECT_EQ(roundEnd.size(), 4u + 31u);
	EXPECT_EQ(minX(roundEnd), 0);
	EXPECT_EQ(maxX(roundEnd), 11000);
	EXPECT_EQ(roundEnd.front(), (Vec2i64{0, -1000}));
	EXPECT_EQ(roundEnd.back(), (Vec2i64{0, 1000})); // the butt cut is the closing edge
	EXPECT_EQ(windingOrder(roundEnd), Winding::CounterClockwise);

	const Ring roundStart = strokePolyline(strokeArgs(c, one, one, StrokeCap::Round, StrokeCap::Butt));
	EXPECT_EQ(roundStart.size(), 4u + 31u);
	EXPECT_EQ(minX(roundStart), -1000);
	EXPECT_EQ(maxX(roundStart), 10000);
	EXPECT_EQ(roundStart[1], (Vec2i64{10000, -1000})); // the butt cut is right bank end -> left bank end
	EXPECT_EQ(roundStart[2], (Vec2i64{10000, 1000}));
	EXPECT_EQ(windingOrder(roundStart), Winding::CounterClockwise);
}

TEST(StrokePolyline, VaryingOffsetsPerSide) {
	const std::vector<Vec2d>  c		= {{0.0, 0.0}, {5.0, 0.0}, {10.0, 0.0}};
	const std::vector<double> left	= {1.0, 2.0, 3.0};
	const std::vector<double> right = {0.5, 0.5, 0.5};
	const Ring				  r		= strokePolyline(strokeArgs(c, left, right, StrokeCap::Butt, StrokeCap::Butt));
	const Ring expected = {{0, -500}, {5000, -500}, {10000, -500}, {10000, 3000}, {5000, 2000}, {0, 1000}};
	EXPECT_EQ(r, expected);
	EXPECT_NEAR(signedAreaSquareMeters(r), 25.0, 1e-9); // trapezoid 10 m x (1.5 .. 3.5 m)
}

TEST(StrokePolyline, RoundCapMeetsUnequalBanks) {
	// Left 2 m, right 1 m: the cap reaches 1.5 m forward and ends on both banks.
	const std::vector<Vec2d>  c		= {{0.0, 0.0}, {10.0, 0.0}};
	const std::vector<double> left	= {2.0, 2.0};
	const std::vector<double> right = {1.0, 1.0};
	const Ring				  r		= strokePolyline(strokeArgs(c, left, right, StrokeCap::Butt, StrokeCap::Round));

	// pi * 1.5 m at 0.1 m spacing is 48 steps: 47 cap vertices after the right
	// bank's end at index 1, then the left bank's end.
	ASSERT_EQ(r.size(), 4u + 47u);
	EXPECT_EQ(r[1], (Vec2i64{10000, -1000}));
	EXPECT_EQ(r[1 + 24], (Vec2i64{11500, 0}));
	EXPECT_EQ(r[1 + 48], (Vec2i64{10000, 2000}));
	for (std::size_t i = 2; i < 1 + 48; ++i) {
		EXPECT_GT(r[i].x, 10000);
		EXPECT_GT(r[i].y, -1000);
		EXPECT_LT(r[i].y, 2000);
	}
	EXPECT_TRUE(isSimple(r).pass);

	// Two quarter-ellipse fans, 24 steps each: (24 / 2) * a * b * sin(pi / 48).
	const double capArea = 12.0 * 1.5 * (1.0 + 2.0) * std::sin(std::numbers::pi / 48.0);
	EXPECT_NEAR(signedAreaSquareMeters(r), 30.0 + capArea, 5e-3);
}

TEST(StrokePolyline, GentleArcUnderItsRadiusIsSimple) {
	// A quarter circle of radius 50 m, turning left, points 1 m apart.
	constexpr double   kRadius = 50.0;
	constexpr int	   kPoints = 79;
	std::vector<Vec2d> c;
	for (int i = 0; i < kPoints; ++i) {
		const double a = 0.5 * std::numbers::pi * i / (kPoints - 1);
		c.push_back({1000.0 + kRadius * std::cos(a), 2000.0 + kRadius * std::sin(a)});
	}
	for (int i = 1; i + 1 < kPoints; ++i) {
		EXPECT_NEAR(localRadiusOfCurvatureM(c[i - 1], c[i], c[i + 1]), kRadius, 1e-6);
	}

	// The left bank is the inner one; both stay well under the radius.
	const std::vector<double> left(kPoints, 6.0);
	const std::vector<double> right(kPoints, 4.0);
	const Ring				  r = strokePolyline(strokeArgs(c, left, right, StrokeCap::Round, StrokeCap::Round));
	EXPECT_TRUE(isSimple(r).pass);
	EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);

	// Annulus sector between radii 44 and 54, plus two end caps of ~pi * 5^2 / 2.
	const double sector = 0.25 * std::numbers::pi * (54.0 * 54.0 - 44.0 * 44.0);
	EXPECT_NEAR(signedAreaSquareMeters(r), sector + std::numbers::pi * 25.0, 0.5);
}

TEST(StrokePolyline, MeanderIsCounterClockwiseAndStartsAtRightBank) {
	const std::vector<Vec2d>  c		= meander(12);
	const std::vector<double> left	= widths(12, 0.0);
	const std::vector<double> right = widths(12, 1.3);
	for (const StrokeCap cap : {StrokeCap::Butt, StrokeCap::Round}) {
		const Ring r = strokePolyline(strokeArgs(c, left, right, cap, cap));
		EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
		EXPECT_TRUE(isSimple(r).pass);
		const Vec2d d = c[1] - c[0];
		const Vec2d n = Vec2d{-d.y, d.x} * (1.0 / length(d));
		const Vec2d p = c[0] - n * right[0];
		EXPECT_EQ(r[0], (Vec2i64{std::llround(p.x * 1000.0), std::llround(p.y * 1000.0)}));
	}
}

TEST(StrokePolyline, BankPointsDependOnlyOnNeighbors) {
	// The same centerline cut three points later: every bank point whose
	// centerline point has both neighbors in both inputs must match exactly.
	const std::vector<Vec2d>  c		= meander(30);
	const std::vector<double> left	= widths(30, 0.0);
	const std::vector<double> right = widths(30, 1.3);
	const std::size_t		  n		= c.size();
	const Ring				  full	= strokePolyline(strokeArgs(c, left, right, StrokeCap::Butt, StrokeCap::Butt));
	const Ring				  cut	= strokePolyline(
		  {std::span(c).subspan(3), std::span(left).subspan(3), std::span(right).subspan(3), StrokeCap::Butt, StrokeCap::Butt, 0.1}
	  );
	ASSERT_EQ(full.size(), 2 * n);
	ASSERT_EQ(cut.size(), 2 * (n - 3));
	for (std::size_t i = 4; i < n; ++i) {
		EXPECT_EQ(full[i], cut[i - 3]) << "right bank " << i;
		EXPECT_EQ(full[2 * n - 1 - i], cut[2 * (n - 3) - 1 - (i - 3)]) << "left bank " << i;
	}
	EXPECT_NE(full[3], cut[0]); // the cut input's first point sees only one segment
}

TEST(StrokePolyline, SampledRibbonsMatchAcrossACut) {
	// The seam property end to end: sample and stroke a river from two cut
	// copies of its polyline. Samples agree from span 4 on, so bank points agree
	// from the second sample of span 4 (the first still sees span 3's last sample).
	const std::vector<Vec2d>  points = meander(30);
	const std::vector<double> hw	 = widths(30, 0.0);
	const auto				  full	 = sampleCatmullRom(points, hw, 0.5);
	const auto				  cut	 = sampleCatmullRom(std::span(points).subspan(3), std::span(hw).subspan(3), 0.5);

	const auto ribbon = [](const std::vector<CenterlineSample>& samples) {
		std::vector<Vec2d>	c;
		std::vector<double> offsets;
		for (const CenterlineSample& s : samples) {
			c.push_back(s.position);
			offsets.push_back(s.halfWidthM);
		}
		return strokePolyline({c, offsets, offsets, StrokeCap::Round, StrokeCap::Round, 0.5});
	};
	const Ring fullRing = ribbon(full);
	const Ring cutRing	= ribbon(cut);
	EXPECT_TRUE(isSimple(fullRing).pass);

	const auto firstOfSegment = [](const std::vector<CenterlineSample>& samples, std::size_t segment) {
		return static_cast<std::size_t>(
			std::find_if(samples.begin(), samples.end(), [&](const CenterlineSample& s) { return s.segment == segment; }) -
			samples.begin()
		);
	};
	const std::size_t fullStart = firstOfSegment(full, 4);
	const std::size_t cutStart	= firstOfSegment(cut, 1);
	ASSERT_EQ(full.size() - fullStart, cut.size() - cutStart);

	// Right bank: ring index equals sample index (vertex 0 is the right bank's first point).
	std::size_t compared = 0;
	for (std::size_t j = 1; fullStart + j < full.size(); ++j) {
		EXPECT_EQ(fullRing[fullStart + j], cutRing[cutStart + j]);
		++compared;
	}
	EXPECT_GT(compared, 900u);
}

TEST(StrokePolyline, ButtCutsShareVerticesUnderASharedNormal) {
	// A centerline cut at a bend: each piece alone would offset the cut point
	// along its own single segment. Passing both the full centerline's normal
	// there makes the two butt edges one edge, vertex for vertex.
	const std::vector<Vec2d>  c		 = {{0.0, 0.0}, {5.0, 0.0}, {9.0, 3.0}, {12.0, 7.0}};
	const std::vector<double> left	 = {1.0, 1.2, 1.4, 1.6};
	const std::vector<double> right	 = {0.8, 0.9, 1.0, 1.1};
	const std::size_t		  cut	 = 1;
	const Vec2d				  normal = strokeNormal(c, cut);
	EXPECT_NEAR(length(normal), 1.0, 1e-12);

	auto piece = [&](std::size_t from, std::size_t to, bool shared) {
		StrokeArgs args{
			std::span(c).subspan(from, to - from + 1),
			std::span(left).subspan(from, to - from + 1),
			std::span(right).subspan(from, to - from + 1),
			from == cut ? StrokeCap::Butt : StrokeCap::Round,
			to == cut ? StrokeCap::Butt : StrokeCap::Round,
			0.5
		};
		if (shared) {
			(from == cut ? args.startNormal : args.endNormal) = normal;
		}
		return strokePolyline(args);
	};

	const Vec2i64 rightCut	 = strokeBankPoint(c[cut], normal, -right[cut]);
	const Vec2i64 leftCut	 = strokeBankPoint(c[cut], normal, left[cut]);
	const Ring	  upstream	 = piece(0, cut, true);
	const Ring	  downstream = piece(cut, c.size() - 1, true);
	for (const Ring* r : {&upstream, &downstream}) {
		EXPECT_TRUE(contains(*r, rightCut));
		EXPECT_TRUE(contains(*r, leftCut));
		EXPECT_TRUE(isSimple(*r).pass);
	}
	// The downstream piece starts on the cut: ring[0] is its right bank point.
	EXPECT_EQ(downstream.front(), rightCut);
	EXPECT_EQ(downstream.back(), leftCut);

	// Without the shared normal the two pieces disagree at the bend.
	EXPECT_FALSE(contains(piece(0, cut, false), rightCut));
	EXPECT_FALSE(contains(piece(cut, c.size() - 1, false), rightCut));
}

TEST(LocalRadiusOfCurvature, Values) {
	EXPECT_NEAR(localRadiusOfCurvatureM({5.0, 0.0}, {0.0, 5.0}, {-5.0, 0.0}), 5.0, 1e-12);
	EXPECT_NEAR(localRadiusOfCurvatureM({0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}), std::sqrt(0.5), 1e-12);
	EXPECT_EQ(localRadiusOfCurvatureM({0.0, 0.0}, {1.0, 0.0}, {2.0, 0.0}), std::numeric_limits<double>::infinity());
	EXPECT_EQ(localRadiusOfCurvatureM({0.0, 0.0}, {1.0, 0.0}, {0.5, 0.0}), 0.0);
}
