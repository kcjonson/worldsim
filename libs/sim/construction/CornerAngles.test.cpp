#include "CornerAngles.h"

#include <cmath>
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
