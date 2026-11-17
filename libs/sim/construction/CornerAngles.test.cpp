#include "CornerAngles.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <gtest/gtest.h>
#include <numbers>
#include <vector>

using namespace construction;
using ::Foundation::Vec2;

namespace {

	constexpr float kDegreeTolerance = 1e-3F;

	Vec2 polar(Vec2 origin, float degrees, float length) {
		const float radians = degrees * std::numbers::pi_v<float> / 180.0F;
		return {origin.x + std::cos(radians) * length, origin.y + std::sin(radians) * length};
	}

	CornerAngles read(
		const std::vector<Vec2>& points,
		Vec2					 cursor,
		bool					 closes = false,
		AngleReadoutTool		 tool = AngleReadoutTool::Foundation,
		float					 closingReach = 0.0F
	) {
		return cornerAngles({.points = points, .cursor = cursor, .closes = closes, .tool = tool, .closingReachMeters = closingReach});
	}

} // namespace

TEST(CornerAngles, AngleReadout) {
	const Vec2		   start{0.0F, 0.0F};
	const Vec2		   second{5.0F, 0.0F};
	const CornerAngles at75 = read({start, second}, polar(second, 105.0F, 3.0F));
	ASSERT_EQ(at75.count, 1U);
	EXPECT_EQ(at75.corners[0].role, CornerRole::Previous);
	EXPECT_NEAR(at75.corners[0].degrees, 75.0F, kDegreeTolerance);
	EXPECT_FALSE(at75.corners[0].square);

	const CornerAngles at90 = read({start, second}, polar(second, 90.0F, 3.0F));
	ASSERT_EQ(at90.count, 1U);
	EXPECT_TRUE(at90.corners[0].square);

	EXPECT_EQ(read({start}, polar(start, 30.0F, 3.0F)).count, 0U);
}

TEST(CornerAngles, AngleReadoutWallChainStaysOpen) {
	const std::vector<Vec2> points = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}};
	// An open wall chain reads only the corner at its last point, however near the start.
	const CornerAngles open = read(points, {2.0F, 2.0F}, false, AngleReadoutTool::Wall, 5.0F);
	ASSERT_EQ(open.count, 1U);
	EXPECT_NEAR(open.corners[0].degrees, 45.0F, kDegreeTolerance);
	// The foundation tool reads the same path as a polygon and adds the closing corner.
	const CornerAngles polygon = read(points, {2.0F, 2.0F}, false, AngleReadoutTool::Foundation, 5.0F);
	ASSERT_EQ(polygon.count, 2U);
	EXPECT_EQ(polygon.corners[1].role, CornerRole::Closing);
}

TEST(CornerAngles, AngleReadoutSkipsShortEdgesAndShortPaths) {
	const Vec2 start{0.0F, 0.0F};
	const Vec2 second{5.0F, 0.0F};
	EXPECT_EQ(read({start, second}, {5.0F, 0.0005F}).count, 0U);
	EXPECT_EQ(read({}, {1.0F, 1.0F}).count, 0U);
}

TEST(CornerAngles, AngleReadoutSquareToleranceIsStrict) {
	const std::vector<Vec2> points = {{0.0F, 0.0F}, {6.0F, 0.0F}};
	EXPECT_TRUE(read(points, polar(points[1], 90.0F, 4.0F)).corners[0].square);
	EXPECT_FALSE(read(points, polar(points[1], 90.2F, 4.0F)).corners[0].square);
}

TEST(CornerAngles, AngleReadoutMatchesPrototype) {
	struct Expected {
		CornerRole role;
		float	   degrees;
		bool	   square;
	};
	struct Case {
		std::vector<Vec2>	  points;
		Vec2				  cursor;
		bool				  closes;
		AngleReadoutTool	  tool;
		float				  closingReach;
		std::vector<Expected> corners;
	};
	// The prototype's cornerAngles, 21 inputs.
	const std::vector<Case> cases = {
		{{{0.0F, 0.0F}, {5.0F, 0.0F}}, {5.0F, 3.0F}, false, AngleReadoutTool::Foundation, 0.0F, {{CornerRole::Previous, 90.0F, true}}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 3.0F}}, {0.3F, 3.2F}, false, AngleReadoutTool::Foundation, 1.0F, {{CornerRole::Previous, 92.436648F, false}}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 3.0F}}, {0.3F, 0.4F}, false, AngleReadoutTool::Foundation, 1.0F, {{CornerRole::Previous, 61.049005F, false}, {CornerRole::Closing, 53.130102F, false}}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 3.0F}}, {0.3F, 0.4F}, false, AngleReadoutTool::Foundation, 0.3F, {{CornerRole::Previous, 61.049005F, false}}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 3.0F}, {0.0F, 3.0F}}, {0.0F, 0.0F}, true, AngleReadoutTool::Foundation, 0.0F, {{CornerRole::Previous, 90.0F, true}, {CornerRole::Closing, 90.0F, true}}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 3.0F}}, {0.0F, 3.0F}, false, AngleReadoutTool::Wall, 0.0F, {{CornerRole::Previous, 90.0F, true}}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 3.0F}, {0.0F, 3.0F}}, {0.0F, 0.0F}, true, AngleReadoutTool::Wall, 0.0F, {{CornerRole::Previous, 90.0F, true}, {CornerRole::Closing, 90.0F, true}}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 3.0F}}, {0.3F, 0.4F}, false, AngleReadoutTool::Wall, 1.0F, {{CornerRole::Previous, 61.049005F, false}}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}, {5.0F, 3.0F}}, {5.0F, 3.0F}, false, AngleReadoutTool::Foundation, 0.0F, {}},
		{{{0.0F, 0.0F}, {5.0F, 0.0F}}, {5.0F, 0.0005F}, false, AngleReadoutTool::Foundation, 0.0F, {}},
		{{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 4.0F}}, {0.0F, 4.0F}, false, AngleReadoutTool::Foundation, 0.0F, {{CornerRole::Previous, 90.0F, true}}},
		{{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 4.0F}}, {0.0F, 4.04F}, false, AngleReadoutTool::Foundation, 0.0F, {{CornerRole::Previous, 90.381966F, false}}},
		{{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 4.0F}}, {0.0F, 4.2F}, false, AngleReadoutTool::Foundation, 0.0F, {{CornerRole::Previous, 91.909152F, false}}},
		{{{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}, {2.0F, 2.0F}}, {0.0F, 4.0F}, false, AngleReadoutTool::Foundation, 0.0F, {{CornerRole::Previous, 270.0F, false}}},
		{{{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 4.0F}, {2.0F, 2.0F}}, {1.0F, 1.0F}, false, AngleReadoutTool::Foundation, 2.0F, {{CornerRole::Previous, 180.0F, false}, {CornerRole::Closing, 45.0F, false}}},
		{{{0.0F, 0.0F}, {4.0F, 0.0F}, {2.0F, 2.0F}}, {4.0F, -3.0F}, false, AngleReadoutTool::Foundation, 0.0F, {{CornerRole::Previous, 23.198591F, false}}},
		{{{0.0F, 0.0F}, {4.0F, 0.0F}, {2.0F, -2.0F}}, {6.0F, -3.0F}, false, AngleReadoutTool::Wall, 0.0F, {{CornerRole::Previous, 59.036243F, false}}},
		{{{0.0F, 0.0F}, {4.0F, 0.0F}, {2.0F, -2.0F}, {1.0F, -1.0F}}, {0.0F, 0.0F}, true, AngleReadoutTool::Foundation, 0.0F, {{CornerRole::Previous, 180.0F, false}, {CornerRole::Closing, 45.0F, false}}},
		{{{-3.0F, -3.0F}, {2.0F, -1.0F}, {5.0F, 4.0F}, {-1.0F, 3.0F}, {-4.0F, 1.0F}}, {-3.0F, -2.6F}, false, AngleReadoutTool::Foundation, 1.0F, {{CornerRole::Previous, 108.165957F, false}, {CornerRole::Closing, 68.198591F, false}}},
		{{{0.0F, 0.0F}}, {3.0F, 3.0F}, false, AngleReadoutTool::Foundation, 0.0F, {}},
		{{{1.0F, 1.0F}, {1.0F, 1.0F}}, {2.0F, 3.0F}, false, AngleReadoutTool::Foundation, 0.0F, {}},
	};
	for (const Case& c : cases) {
		SCOPED_TRACE(testing::Message() << "cursor (" << c.cursor.x << ", " << c.cursor.y << ") points " << c.points.size());
		const CornerAngles got = read(c.points, c.cursor, c.closes, c.tool, c.closingReach);
		ASSERT_EQ(got.count, c.corners.size());
		for (std::size_t index = 0; index < got.count; ++index) {
			EXPECT_EQ(got.corners[index].role, c.corners[index].role);
			EXPECT_NEAR(got.corners[index].degrees, c.corners[index].degrees, kDegreeTolerance);
			EXPECT_EQ(got.corners[index].square, c.corners[index].square);
		}
	}
}

// A nearly straight corner of a thin triangle reads its interior angle in either winding: the
// inside is taken right at the corner, so nothing lands past the far side.
TEST(CornerAngles, ANearlyStraightCornerOfAThinTriangleReadsItsInteriorAngle) {
	for (const float bendDegrees : {0.2F, 0.5F, 1.0F, 1.5F, 3.0F}) {
		for (const float winding : {1.0F, -1.0F}) {
			SCOPED_TRACE(testing::Message() << "bend " << bendDegrees << ", winding " << winding);
			const Vec2		   first{0.0F, 0.0F};
			const Vec2		   second{4.0F, 0.0F};
			const CornerAngles thin = read({first, second}, polar(second, winding * bendDegrees, 4.0F));
			ASSERT_EQ(thin.count, 1U);
			EXPECT_NEAR(thin.corners[0].degrees, 180.0F - bendDegrees, kDegreeTolerance);
			EXPECT_LT(thin.corners[0].sweepRadians * winding, 0.0F) << "the sweep turns through the inside";
		}
	}
}

// Every corner of a closed square and of a closed regular hexagon, in either winding, reads its
// interior angle. Each corner's bisector runs through the opposite corner, so the ray that finds
// the inside passes through a vertex of the path.
TEST(CornerAngles, EveryCornerOfASquareAndARegularHexagonReadsItsInteriorAngleInEitherWinding) {
	constexpr float kSideMeters = 4.0F;
	constexpr int	kHexagonCorners = 6;
	struct Shape {
		std::vector<Vec2> ring;
		float			  interiorDegrees;
		bool			  square;
	};
	Shape hexagon{.ring = {}, .interiorDegrees = 120.0F, .square = false};
	Vec2  corner{0.0F, 0.0F};
	for (int index = 0; index < kHexagonCorners; ++index) {
		hexagon.ring.push_back(corner);
		corner = polar(corner, 360.0F * static_cast<float>(index) / static_cast<float>(kHexagonCorners), kSideMeters);
	}
	const Shape square{
		.ring = {{0.0F, 0.0F}, {kSideMeters, 0.0F}, {kSideMeters, kSideMeters}, {0.0F, kSideMeters}},
		.interiorDegrees = 90.0F,
		.square = true,
	};
	for (const Shape& shape : {square, hexagon}) {
		for (const bool reversed : {false, true}) {
			std::vector<Vec2> wound = shape.ring;
			if (reversed) {
				std::reverse(wound.begin(), wound.end());
			}
			for (std::size_t rotation = 0; rotation < wound.size(); ++rotation) {
				SCOPED_TRACE(testing::Message() << wound.size() << " corners, reversed " << reversed << ", rotation " << rotation);
				std::vector<Vec2> points = wound;
				std::rotate(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(rotation), points.end());
				const CornerAngles closed = read(points, points.front(), true);
				ASSERT_EQ(closed.count, 2U);
				for (std::size_t index = 0; index < closed.count; ++index) {
					EXPECT_NEAR(closed.corners[index].degrees, shape.interiorDegrees, kDegreeTolerance);
					EXPECT_EQ(closed.corners[index].square, shape.square);
				}
			}
		}
	}
}

// A reflex corner reads over 180 degrees in either winding.
TEST(CornerAngles, AReflexCornerReadsOverAHundredAndEightyInEitherWinding) {
	for (const float winding : {1.0F, -1.0F}) {
		SCOPED_TRACE(winding);
		const std::vector<Vec2> points = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F * winding}};
		const CornerAngles		reflex = read(points, {6.0F, 5.0F * winding});
		ASSERT_EQ(reflex.count, 1U);
		EXPECT_NEAR(reflex.corners[0].degrees, 225.0F, kDegreeTolerance);
		EXPECT_NEAR(reflex.corners[0].sweepRadians, -winding * 225.0F * std::numbers::pi_v<float> / 180.0F, 1e-5F);
	}
}

// A collinear path has no inside: its corner reads the plain angle between its edges, 180
// degrees straight on and 0 folded back.
TEST(CornerAngles, ACollinearPathReadsThePlainAngleBetweenItsEdges) {
	const std::vector<Vec2> points = {{0.0F, 0.0F}, {4.0F, 0.0F}};
	const CornerAngles		straight = read(points, {8.0F, 0.0F});
	ASSERT_EQ(straight.count, 1U);
	EXPECT_NEAR(straight.corners[0].degrees, 180.0F, kDegreeTolerance);
	const CornerAngles folded = read(points, {2.0F, 0.0F});
	ASSERT_EQ(folded.count, 1U);
	EXPECT_NEAR(folded.corners[0].degrees, 0.0F, kDegreeTolerance);
}
