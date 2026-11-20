// The snap guides (building-construction D12, Guides), checked against the tuned
// prototype's cases in its legacy mode (common lengths and rounding off). Expected points
// are the prototype's outputs for the same inputs.

#include "SnapEngine.h"
#include "SnapGuidesDetail.h"
#include "SnapTestFixtures.h"

#include <assets/ConstructionRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace construction;
using namespace construction::snap_test;
using assets::SnappingConfig;
using snap_guides::CrossingStatus;
using snap_guides::GuideLineStatus;
using snap_guides::GuideNodeKind;
using snap_guides::GuideStudy;
using snap_guides::NearbyEdgeStatus;
using snap_guides::SnapTool;

namespace {

	constexpr float kOracleTolerance = 1e-4F;
	constexpr float kNearTolerance = 1e-3F;

	void expectPoint(Vec2 actual, double x, double y, float tolerance = kOracleTolerance) {
		EXPECT_NEAR(actual.x, x, tolerance);
		EXPECT_NEAR(actual.y, y, tolerance);
	}

	std::size_t countLines(const GuideStudy& study, GuideSource source) {
		return static_cast<std::size_t>(std::count_if(study.lines.begin(), study.lines.end(), [&](const snap_guides::GuideLine& line) {
			return line.source == source;
		}));
	}

	std::size_t countCrossings(const GuideStudy& study, CrossingStatus status) {
		return static_cast<std::size_t>(
			std::count_if(study.crossings.begin(), study.crossings.end(), [&](const snap_guides::GuideCrossingRecord& crossing) {
				return crossing.status == status;
			})
		);
	}

	// The angle of a unit direction, in degrees from +x.
	double degreesOf(Vec2 direction) {
		return std::atan2(static_cast<double>(direction.y), static_cast<double>(direction.x)) / kRadiansPerDegree;
	}

	class SnapGuides : public ::testing::Test {
	  protected:
		SnappingConfig snapping;
		SnapWorld	   built;

		[[nodiscard]] SnapEngine engine() const { return SnapEngine(snapping, built.world); }
		[[nodiscard]] SnapResult snap(const std::vector<Vec2>& points, Vec2 cursor, const SnapInput& input = foundationInput()) const {
			return engine().snap(points, cursor, input);
		}
		[[nodiscard]] SnapResult snapWall(const std::vector<Vec2>& points, Vec2 cursor, const SnapInput& input = wallInput()) const {
			return engine().snapWall(points, cursor, input);
		}
		[[nodiscard]] SnapResult snapWith(bool wallTool, const std::vector<Vec2>& points, Vec2 cursor, const DrawingAids& aids) const {
			return wallTool ? snapWall(points, cursor, wallInput(aids)) : snap(points, cursor, foundationInput(aids));
		}
		[[nodiscard]] GuideStudy study(
			const std::vector<Vec2>& points,
			Vec2					 cursor,
			const DrawingAids&		 aids = guideAidsOn(),
			SnapTool				 tool = SnapTool::Foundation
		) const {
			return studyGuides(snapping, built.world, points, cursor, aids, tool);
		}
	};

} // namespace

// --- Worked example -------------------------------------------------------------

TEST_F(SnapGuides, WorkedExampleRectangleAt30Degrees) {
	std::vector<Vec2> points;
	SnapResult		  result = snap(points, {0.02F, -0.01F});
	EXPECT_EQ(result.kind, SnapKind::None);
	points.push_back(kWorkedPoint0);

	result = snap(points, polar(kWorkedPoint0, 28.5, 6.0));
	EXPECT_EQ(result.kind, SnapKind::Angle);
	expectPoint(result.point, 5.196, 3.0, kNearTolerance);
	points.push_back(placed(result.point));

	const Vec2 offCorner = polar(kWorkedPoint2, 30.0, 0.5);
	result = snap(points, offCorner);
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 3.196003, 6.463995);
	ASSERT_EQ(result.guideCount, 1U);
	EXPECT_TRUE(result.guides[0].rightAngle);
	for (int angle = 0; angle < 360; angle += 30) {
		const SnapResult each = snap(points, polar(kWorkedPoint2, angle, 0.5));
		EXPECT_EQ(each.kind, SnapKind::GuideLine) << angle;
		EXPECT_LT(distanceFromLine(points[1], 120.0, each.point), 1e-3) << angle;
		EXPECT_LT(distanceBetween(each.guides[0].from, points[1]), 1e-6) << angle;
	}
	points.push_back(placed(result.point));

	result = snap(points, {-1.6F, 3.8F});
	EXPECT_EQ(result.kind, SnapKind::GuideCrossing);
	expectPoint(result.point, -2.0, 3.464);
	ASSERT_EQ(result.guideCount, 2U);
	expectPoint(result.guides[0].from, 0.0, 0.0);
	EXPECT_EQ(result.guides[0].relation, GuideRelation::Perpendicular);
	expectPoint(result.guides[1].from, 3.196, 6.464);
	EXPECT_EQ(result.guides[1].relation, GuideRelation::Parallel);
	EXPECT_TRUE(result.guides[0].rightAngle);
	EXPECT_TRUE(result.guides[1].rightAngle);
	EXPECT_FALSE(result.guides[0].throughPreviousPoint);
	EXPECT_TRUE(result.guides[1].throughPreviousPoint);
	points.push_back(placed(result.point));

	result = snap(points, {0.2F, -0.15F});
	EXPECT_EQ(result.kind, SnapKind::Origin);
	EXPECT_TRUE(result.closesShape());

	const std::size_t count = points.size();
	for (std::size_t index = 0; index < count; ++index) {
		const Vec2 previous = points[(index + count - 1) % count];
		const Vec2 next = points[(index + 1) % count];
		EXPECT_NEAR(cornerDegrees(previous, points[index], next), 90.0, 0.005) << index;
	}
	const double edges[] = {6.0, 4.0, 6.0, 4.0};
	for (std::size_t index = 0; index < count; ++index) {
		EXPECT_NEAR(distanceBetween(points[index], points[(index + 1) % count]), edges[index], 0.0005) << index;
	}
}

// --- Freeform tool ----------------------------------------------------------------

TEST_F(SnapGuides, GuidePerpendicularOffFirstEdgeAt30Degrees) {
	const SnapResult result = snap({kWorkedPoint0, kWorkedPoint1}, polar(kWorkedPoint1, 118.0, 4.0));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 3.197175, 6.461964);
	ASSERT_EQ(result.guideCount, 1U);
	const SnapGuide& guide = result.guides[0];
	EXPECT_EQ(guide.source, GuideSource::DrawnShape);
	EXPECT_EQ(guide.relation, GuideRelation::Perpendicular);
	EXPECT_TRUE(guide.throughPreviousPoint);
	expectPoint(guide.from, 5.196, 3.0);
	EXPECT_TRUE(guide.rightAngle);
	EXPECT_NEAR(degreesOf(guide.rightAngleEdgeDirection), -150.0, 1.0);
	EXPECT_NEAR(guide.distanceFromNodeMeters, distanceBetween(guide.from, guide.to), 1e-5);
}

TEST_F(SnapGuides, GuidePerpendicularCapturedByAngleFarOut) {
	const Vec2 cursor = polar(kWorkedPoint1, 115.0, 10.0);
	EXPECT_NEAR(distanceFromLine(kWorkedPoint1, 120.0, cursor), 0.872, 1e-3);
	const SnapResult result = snap({kWorkedPoint0, kWorkedPoint1}, cursor);
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 0.214922, 11.627226);
	EXPECT_TRUE(result.guides[0].rightAngle);
}

TEST_F(SnapGuides, GuideTwoLinesThroughOneNodeDoNotCross) {
	const std::vector<Vec2> points{kWorkedPoint0, kWorkedPoint1};
	const Vec2				cursor = polar(kWorkedPoint1, 112.0, 0.5);
	const SnapResult		result = snap(points, cursor);
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 4.948428, 3.428795);
	// The 120-degree line and the world's north-south line both run through point 2.
	EXPECT_GE(countCrossings(study(points, cursor), CrossingStatus::SharedGeneratingNode), 1U);
}

TEST_F(SnapGuides, GuideClosingCornerLandsOnExistingCorner) {
	built.addFoundation({{-2.0, 3.464}, {-2.0, 0.464}, {-5.0, 0.464}, {-5.0, 3.464}});
	const double	 towardX = -1.6 - -2.0;
	const double	 towardY = 3.8 - 3.464;
	const double	 size = std::hypot(towardX, towardY);
	const Vec2		 cursor{static_cast<float>(-2.0 + towardX / size * 0.45), static_cast<float>(3.464 + towardY / size * 0.45)};
	const SnapResult result = snap({kWorkedPoint0, kWorkedPoint1, kWorkedPoint2}, cursor);
	EXPECT_EQ(result.kind, SnapKind::GuideCrossing);
	expectPoint(result.point, -2.0, 3.464);
	ASSERT_EQ(result.guideCount, 2U);
	EXPECT_EQ(result.guides[0].source, GuideSource::DrawnShape);
	EXPECT_EQ(result.guides[1].source, GuideSource::DrawnShape);
}

TEST_F(SnapGuides, GuideCrossingOutsideRadiusIgnored) {
	built.addFoundation({{1.1, 15.0}, {3.0, 15.0}, {3.0, 17.0}, {1.1, 17.0}});
	const std::vector<Vec2> points{kWorkedPoint0, kWorkedPoint1};
	const Vec2				cursor = polar(kWorkedPoint1, 115.0, 10.0);
	EXPECT_EQ(snap(points, cursor).kind, SnapKind::GuideLine);
	const GuideStudy guides = study(points, cursor);
	const auto		 tooFar =
		std::find_if(guides.crossings.begin(), guides.crossings.end(), [](const snap_guides::GuideCrossingRecord& crossing) {
			return crossing.status == CrossingStatus::TooFar;
		});
	ASSERT_NE(tooFar, guides.crossings.end());
	EXPECT_NEAR(tooFar->distanceMeters, 1.973, 1e-3);
}

TEST_F(SnapGuides, GuideNearParallelLinesNeverCross) {
	built.addFoundation(rectangle(8.0, 9.0, 31.5, 4.0, 3.0));
	const std::vector<Vec2> points{kWorkedPoint0, kWorkedPoint1};
	const Vec2				cursor = polar(kWorkedPoint1, 30.75, 10.0);
	const SnapResult		result = snap(points, cursor);
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_EQ(result.guides[0].source, GuideSource::DrawnShape);
	expectPoint(result.point, 13.855450, 7.999682);
	const GuideStudy guides = study(points, cursor);
	const bool		 nearbyCaught = std::any_of(guides.lines.begin(), guides.lines.end(), [](const snap_guides::GuideLine& line) {
		  return line.source == GuideSource::NearbyStructure && line.captured();
	  });
	EXPECT_TRUE(nearbyCaught);
	EXPECT_EQ(countCrossings(guides, CrossingStatus::TooShallow), 1U);
}

TEST_F(SnapGuides, GuideParallelMergeKeepsShapePoint) {
	built.addFoundation({{0.25, 10.0}, {3.25, 10.0}, {3.25, 13.0}, {0.25, 13.0}});
	const std::vector<Vec2> points{kWorkedPoint0};
	const Vec2				cursor{0.2F, 5.0F};
	const SnapResult		result = snap(points, cursor);
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 0.0, 5.0);
	expectPoint(result.guides[0].from, 0.0, 0.0);
	const GuideStudy guides = study(points, cursor);
	const auto		 merged = std::find_if(guides.lines.begin(), guides.lines.end(), [](const snap_guides::GuideLine& line) {
		  return line.status == GuideLineStatus::Merged;
	  });
	ASSERT_NE(merged, guides.lines.end());
	EXPECT_EQ(merged->node.kind, GuideNodeKind::CommittedCorner);
	EXPECT_NEAR(merged->node.point.x, 0.25, 1e-9);
	EXPECT_NEAR(merged->node.point.y, 10.0, 1e-9);
	EXPECT_EQ(guides.lines[merged->mergedInto].node.kind, GuideNodeKind::ShapePoint);
}

TEST_F(SnapGuides, GuideParallelToNearbyRotatedFoundation) {
	built.addFoundation(rectangle(6.0, -2.0, 37.0, 4.0, 3.0));
	const SnapResult result = snap({kWorkedPoint0}, polar(kWorkedPoint0, 39.0, 5.0));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_EQ(result.guides[0].source, GuideSource::NearbyStructure);
	EXPECT_LT(distanceFromLine(kWorkedPoint0, 37.0, result.point), 1e-3);
	expectPoint(result.point, 3.990660, 3.007363);
}

TEST_F(SnapGuides, GuideDrawnShapeBeatsCloserWorldAxis) {
	const Vec2				onLine = polar(kWorkedPoint1, 120.0, 0.533);
	const Vec2				cursor = polar(onLine, 30.0, 0.25);
	const std::vector<Vec2> points{kWorkedPoint0, kWorkedPoint1};
	const SnapResult		result = snap(points, cursor);
	EXPECT_EQ(result.guides[0].source, GuideSource::DrawnShape);
	expectPoint(result.point, 4.929496, 3.461585);

	snapping.priorityRule = assets::GuidePriorityRule::Closest;
	const SnapResult closest = snap(points, cursor);
	EXPECT_EQ(closest.guides[0].source, GuideSource::WorldAxes);
	expectPoint(closest.point, 5.196, 3.586591);
}

TEST_F(SnapGuides, GuideNearbyStructureBeatsWorldAxes) {
	built.addFoundation(rectangle(5.0, -4.0, 37.0, 4.0, 3.0));
	const Vec2	 cursor = polar(polar(kWorkedPoint0, 37.0, 5.0), 127.0, 0.1);
	const double cursorY = static_cast<double>(cursor.y);
	built.addFoundation({{9.0, cursorY + 0.12}, {11.0, cursorY + 0.12}, {11.0, cursorY + 2.0}, {9.0, cursorY + 2.0}});
	const SnapResult result = snap({kWorkedPoint0}, cursor);
	EXPECT_EQ(result.kind, SnapKind::GuideCrossing);
	EXPECT_EQ(result.guides[0].source, GuideSource::NearbyStructure);
	EXPECT_LT(distanceFromLine(kWorkedPoint0, 37.0, result.point), 1e-3);
	EXPECT_EQ(result.guides[1].source, GuideSource::WorldAxes);
	expectPoint(result.point, 4.258224, 3.209);
}

TEST_F(SnapGuides, GuideParallelNeighboursBothGetLines) {
	const geometry::Ring shedA = rectangle(-10.0, 3.0, 37.0, 4.0, 3.0);
	const geometry::Ring shedB = rectangle(-1.5, -0.5, 37.0, 4.0, 3.0);
	const FoundationId	 idA = built.addFoundation(shedA);
	const FoundationId	 idB = built.addFoundation(shedB);
	const Vec2			 cornerA = meters(shedA[1]);
	const Vec2			 cursor{
		  -2.6F,
		  static_cast<float>(
			  static_cast<double>(cornerA.y) + std::tan(37.0 * kRadiansPerDegree) * (-2.6 - static_cast<double>(cornerA.x)) + 0.1
		  )
	  };

	const GuideStudy guides = study({}, cursor);
	ASSERT_FALSE(guides.directions.empty());
	EXPECT_EQ(guides.directions[0].supplier.id, idB);
	const auto throughA = std::count_if(guides.lines.begin(), guides.lines.end(), [&](const snap_guides::GuideLine& line) {
		return line.source == GuideSource::NearbyStructure && line.node.kind == GuideNodeKind::CommittedCorner &&
			   guides.cornerStructures[guides.corners[line.node.index].firstStructure].id == idA;
	});
	EXPECT_EQ(throughA, 4);

	const SnapResult result = snap({}, cursor);
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_EQ(result.guides[0].source, GuideSource::NearbyStructure);
	expectPoint(result.guides[0].from, -6.805, 5.407);
	expectPoint(result.point, -2.552342, 8.612467);
}

TEST_F(SnapGuides, GuideNearbyDirectionLimit) {
	const double angles[] = {10.0, 22.0, 35.0, 50.0, 65.0, 78.0};
	for (int index = 0; index < 6; ++index) {
		const double around = index * 60.0 * kRadiansPerDegree;
		built.addFoundation(rectangle(std::cos(around) * 4.0, std::sin(around) * 4.0, angles[index], 2.0, 1.5));
	}
	const GuideStudy guides = study({}, {0.0F, 0.0F});
	ASSERT_EQ(guides.directions.size(), 4U);
	const double expectedModQuarter[] = {78.0, 65.0, 50.0, 10.0};
	for (std::size_t index = 0; index < 4; ++index) {
		EXPECT_NEAR(std::fmod(guides.directions[index].angle / kRadiansPerDegree, 90.0), expectedModQuarter[index], 0.05) << index;
	}
	const auto overLimit = std::count_if(guides.walk.begin(), guides.walk.end(), [](const snap_guides::NearbyWalkEntry& entry) {
		return entry.status == NearbyEdgeStatus::OverLimit;
	});
	EXPECT_EQ(overLimit, 8);
}

TEST_F(SnapGuides, GuideCornerLimit) {
	for (int index = 0; index < 5; ++index) {
		const double left = index * 2.0 - 4.0;
		const double top = 3.0 + index * 0.1;
		built.addFoundation({{left, 2.0}, {left + 1.0, 2.0}, {left + 1.0, top}, {left, top}});
	}
	const GuideStudy guides = study({}, {0.0F, 0.0F});
	EXPECT_EQ(guides.corners.size(), 20U);
	EXPECT_EQ(guides.cornersUsed, 16U);
}

TEST_F(SnapGuides, GuideAlignToExistingOffDropsBuildingGuides) {
	built.addFoundation(rectangle(6.0, -2.0, 37.0, 4.0, 3.0));
	DrawingAids aids = guideAidsOn();
	aids.alignToExistingBuildings = false;
	const std::vector<Vec2> points{kWorkedPoint0};
	const Vec2				cursor = polar(kWorkedPoint0, 39.0, 5.0);
	const SnapResult		result = snap(points, cursor, foundationInput(aids));
	EXPECT_EQ(result.kind, SnapKind::Angle);
	expectPoint(result.point, 3.535534, 3.535534);
	const GuideStudy guides = study(points, cursor, aids);
	EXPECT_EQ(countLines(guides, GuideSource::NearbyStructure), 0U);
	EXPECT_TRUE(std::none_of(guides.lines.begin(), guides.lines.end(), [](const snap_guides::GuideLine& line) {
		return line.node.kind == GuideNodeKind::CommittedCorner;
	}));
}

TEST_F(SnapGuides, GuideSquaresPointOnExistingEdge) {
	built.addFoundation({{-2.0, 6.0}, {10.0, 6.0}, {10.0, 10.0}, {-2.0, 10.0}});
	const SnapResult result = snap({kWorkedPoint0, kWorkedPoint1}, {3.580F, 5.8F});
	EXPECT_EQ(result.kind, SnapKind::Edge);
	expectPoint(result.point, 3.463898, 6.0);
	ASSERT_EQ(result.guideCount, 1U);
	EXPECT_TRUE(result.guides[0].rightAngle);
	EXPECT_NEAR(cornerDegrees(kWorkedPoint0, kWorkedPoint1, result.point), 90.0, 0.005);
}

TEST_F(SnapGuides, GuideOnFirstPointReportsOrigin) {
	const std::vector<Vec2> points{kWorkedPoint0, kWorkedPoint1, kWorkedPoint2, kWorkedPoint3};
	const Vec2				cursor{0.214F, 0.47F};
	ASSERT_GT(distanceBetween(cursor, kWorkedPoint0), snapping.originCloseRadiusMeters);
	for (const bool wallTool : {false, true}) {
		const SnapResult result = wallTool ? snapWall(points, cursor) : snap(points, cursor);
		EXPECT_EQ(result.kind, SnapKind::Origin) << wallTool;
		EXPECT_EQ(result.point.x, kWorkedPoint0.x) << wallTool;
		EXPECT_EQ(result.point.y, kWorkedPoint0.y) << wallTool;
		ASSERT_EQ(result.guideCount, 1U) << wallTool;
		EXPECT_EQ(result.guides[0].relation, GuideRelation::Perpendicular) << wallTool;
		EXPECT_TRUE(result.guides[0].rightAngle) << wallTool;
		EXPECT_EQ(result.guides[0].to.x, kWorkedPoint0.x) << wallTool;
		EXPECT_EQ(result.guides[0].to.y, kWorkedPoint0.y) << wallTool;
	}
	EXPECT_TRUE(snap(points, cursor).closesShape());
}

TEST_F(SnapGuides, GuideNoMarkerOnParallel) {
	const SnapResult result = snap({kWorkedPoint0, kWorkedPoint1}, {7.796F, 4.6F});
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 7.838802, 4.525867);
	EXPECT_EQ(result.guides[0].relation, GuideRelation::Parallel);
	expectPoint(result.guides[0].from, 0.0, 0.0);
	EXPECT_FALSE(result.guides[0].rightAngle);
	EXPECT_FALSE(result.guides[0].throughPreviousPoint);
}

TEST_F(SnapGuides, GuideDeterministicTieBreak) {
	// The cursor is midway between y = 0 and y = 0.5 and as far from (2, 0) as from (5, 0.5).
	// Those numbers are exact in float; D12's 0.2 and 0.4 are not, so they would not tie.
	const FoundationId first = built.addFoundation({{0.0, 0.0}, {2.0, 0.0}, {2.0, -2.0}, {0.0, -2.0}});
	built.addFoundation({{5.0, 0.5}, {7.0, 0.5}, {7.0, 2.5}, {5.0, 2.5}});
	const SnapResult result = snap({}, {3.5F, 0.25F});
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_NEAR(result.point.x, 3.5F, kOracleTolerance);
	EXPECT_NEAR(result.point.y, 0.0F, 1e-6F);
	expectPoint(result.guides[0].from, 2.0, 0.0);
	const GuideStudy guides = study({}, {3.5F, 0.25F});
	EXPECT_EQ(guides.cornerStructures[guides.corners[0].firstStructure].id, first);
}

// --- Wall tool ----------------------------------------------------------------------

TEST_F(SnapGuides, WallGuideClosesRotatedRectangleNotInset) {
	const SnapResult result = snapWall({kWorkedPoint0, kWorkedPoint1, kWorkedPoint2}, {-1.6F, 3.8F});
	EXPECT_EQ(result.kind, SnapKind::GuideCrossing);
	expectPoint(result.point, -2.0, 3.464);
	const SnapResult closing = snapWall({kWorkedPoint0, kWorkedPoint1, kWorkedPoint2, kWorkedPoint3}, {0.2F, 0.1F});
	EXPECT_EQ(closing.kind, SnapKind::Origin);
}

TEST_F(SnapGuides, WallGuideParallelToHostFoundationEdge) {
	built.addFoundation(rectangle(0.0, 0.0, 25.0, 10.0, 6.0));
	const Vec2		 start{3.0F, 3.5F};
	const SnapResult result = snapWall({start}, polar(start, 27.0, 4.0));
	EXPECT_EQ(result.guides[0].source, GuideSource::NearbyStructure);
	EXPECT_LT(distanceFromLine(start, 25.0, result.point), 1e-3);
	EXPECT_EQ(result.kind, SnapKind::GuideCrossing);
	expectPoint(result.point, 6.477666, 5.121802);
}

TEST_F(SnapGuides, WallGuidePerpendicularToNearbyWallSegment) {
	built.addWall({millimeters(0.0, 0.0), millimeters(6.0 * std::cos(20.0 * kRadiansPerDegree), 6.0 * std::sin(20.0 * kRadiansPerDegree))});
	const Vec2		 start{1.0F, 2.5F};
	const SnapResult result = snapWall({start}, polar(start, 112.0, 4.0));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_EQ(result.guides[0].source, GuideSource::NearbyStructure);
	EXPECT_LT(distanceFromLine(start, 110.0, result.point), 1e-3);
	expectPoint(result.point, -0.367209, 6.256493);
}

TEST_F(SnapGuides, WallGuideUsesInsetCorners) {
	const geometry::Ring ring = rectangle(0.0, 0.0, 20.0, 12.0, 8.0);
	built.addFoundation(ring);
	const Vec2				corner = placed(outerFaceFlushCorner(ring, 1, kWallHalfThicknessMm));
	const geometry::Vec2i64 cornerMillimeters = geometry::quantize(corner);
	built.addWall({cornerMillimeters, {cornerMillimeters.x - 3000, cornerMillimeters.y - 5000}});
	const Vec2 cursor{corner.x + 3.0F, corner.y + 0.05F};

	const GuideStudy guides = study({}, cursor, guideAidsOn(), SnapTool::Wall);
	std::size_t		 kept = 0;
	std::size_t		 merged = 0;
	for (const snap_guides::GuideLine& line : guides.lines) {
		if (line.source == GuideSource::WorldAxes && line.relation == GuideRelation::Parallel && line.captured()) {
			kept += line.status == GuideLineStatus::Kept ? 1 : 0;
			merged += line.status == GuideLineStatus::Merged ? 1 : 0;
		}
	}
	EXPECT_EQ(kept, 1U);
	EXPECT_EQ(merged, 1U);

	const SnapResult result = snapWall({}, cursor);
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.guides[0].from, 11.145269, 4.164965);
}

TEST_F(SnapGuides, WallGuideSquaresTJunction) {
	built.addWall({millimeters(-2.0, 6.0), millimeters(10.0, 6.0)});
	const SnapResult result = snapWall({kWorkedPoint0, kWorkedPoint1}, {3.580F, 5.8F});
	EXPECT_EQ(result.kind, SnapKind::WallSegment);
	EXPECT_NE(result.hitSegment, kInvalidSegment);
	expectPoint(result.point, 3.463898, 6.0);
	ASSERT_EQ(result.guideCount, 1U);
	EXPECT_TRUE(result.guides[0].rightAngle);
}

TEST_F(SnapGuides, WallGuideSquaresInsetEdge) {
	built.addFoundation({{-2.0, 6.0}, {10.0, 6.0}, {10.0, 10.0}, {-2.0, 10.0}});
	const SnapResult result = snapWall({kWorkedPoint0, kWorkedPoint1}, {3.580F, 5.95F});
	EXPECT_EQ(result.kind, SnapKind::Edge);
	expectPoint(result.point, 3.405007, 6.102);
}

TEST_F(SnapGuides, WallEndpointBeatsGuideCrossing) {
	built.addWall({millimeters(-1.7, 3.564), millimeters(-6.0, 8.0)});
	const SnapResult result = snapWall({kWorkedPoint0, kWorkedPoint1, kWorkedPoint2}, {-1.75F, 3.584F});
	EXPECT_EQ(result.kind, SnapKind::WallEndpoint);
	expectPoint(result.point, -1.7, 3.564);
}

TEST_F(SnapGuides, WallFoundationCornerSnapsToInset) {
	const geometry::Ring ring = rectangle(0.0, 0.0, 20.0, 12.0, 8.0);
	built.addFoundation(ring);
	const Vec2		 corner = meters(ring[1]);
	const SnapResult result = snapWall({}, {corner.x - 0.1F, corner.y + 0.15F});
	EXPECT_EQ(result.kind, SnapKind::Vertex);
	expectPoint(result.point, 11.145269, 4.164965, 1e-5F);
}

// --- Helper switches (the prototype's levels cases; rounding is SnapLengths.test.cpp's) ---

TEST_F(SnapGuides, HelpersOffKeepAdjacency) {
	built.addFoundation({{9.0, 4.0}, {12.0, 4.0}, {12.0, 7.0}, {9.0, 7.0}});
	const SnapResult result = snap({{0.0F, 0.0F}, {6.0F, 0.0F}}, {9.2F, 4.15F}, foundationInput(allAidsOff()));
	EXPECT_EQ(result.kind, SnapKind::Vertex);
	expectPoint(result.point, 9.0, 4.0);
}

TEST_F(SnapGuides, EachHelperAloneTurnsItsCandidatesOn) {
	built.addFoundation(rectangle(9.0, 6.0, 37.0, 3.0, 2.0));
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.0F, 0.0F}};
	const Vec2				cursor{3.3F, 2.9F};

	const GuideStudy none = study(points, cursor, allAidsOff());
	EXPECT_TRUE(none.lines.empty());
	EXPECT_EQ(none.cornersUsed, 0U);
	const SnapResult bare = snap(points, cursor, foundationInput(allAidsOff()));
	EXPECT_EQ(bare.kind, SnapKind::None);
	EXPECT_EQ(bare.commonLengthCount, 0U);

	DrawingAids shape = allAidsOff();
	shape.guidesFromShape = true;
	EXPECT_EQ(countLines(study(points, cursor, shape), GuideSource::DrawnShape), 4U);

	DrawingAids align = allAidsOff();
	align.alignToExistingBuildings = true;
	const GuideStudy aligned = study(points, cursor, align);
	EXPECT_EQ(countLines(aligned, GuideSource::NearbyStructure), 6U);
	EXPECT_EQ(aligned.cornersUsed, 2U);
	EXPECT_EQ(countLines(aligned, GuideSource::WorldAxes), 0U);

	DrawingAids world = allAidsOff();
	world.guidesFromWorldAxes = true;
	const GuideStudy axes = study(points, cursor, world);
	EXPECT_EQ(countLines(axes, GuideSource::WorldAxes), 4U);
	EXPECT_EQ(axes.cornersUsed, 0U);

	DrawingAids angle = allAidsOff();
	angle.angleSteps = true;
	const SnapResult stepped = snap(points, cursor, foundationInput(angle));
	EXPECT_EQ(stepped.kind, SnapKind::Angle);
	expectPoint(stepped.point, 3.198215, 2.801785);

	// The shape's 6 m edge, then the shed's 2 m and 3 m edges.
	DrawingAids lengths = allAidsOff();
	lengths.commonLengths = true;
	const SnapResult measured = snap(points, cursor, foundationInput(lengths));
	ASSERT_EQ(measured.commonLengthCount, 3U);
	EXPECT_NEAR(measured.commonLengths[0], 6.0F, kNearTolerance);
	EXPECT_NEAR(measured.commonLengths[1], 2.0F, kNearTolerance);
	EXPECT_NEAR(measured.commonLengths[2], 3.0F, kNearTolerance);
}

TEST_F(SnapGuides, CrossingsSwitch) {
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 4.0F}};
	DrawingAids				aids = allAidsOff();
	aids.guidesFromShape = true;
	const SnapResult single = snap(points, {0.15F, 3.95F}, foundationInput(aids));
	EXPECT_EQ(single.kind, SnapKind::GuideLine);
	expectPoint(single.point, 0.15, 4.0);
	aids.guideCrossings = true;
	const SnapResult crossing = snap(points, {0.15F, 3.95F}, foundationInput(aids));
	EXPECT_EQ(crossing.kind, SnapKind::GuideCrossing);
	expectPoint(crossing.point, 0.0, 4.0);
}

// --- One rule each ------------------------------------------------------------------

TEST_F(SnapGuides, GuideThroughShapePointRanksBeforeCloserCorner) {
	// Parallel world guides 0.35 m apart (past the merge distance): y = 0 through the
	// placed point, y = 0.35 through a committed corner, nearer the cursor.
	built.addFoundation({{3.0, 0.35}, {5.0, 0.35}, {5.0, 2.0}, {3.0, 2.0}});
	const SnapResult result = snap({{0.0F, 0.0F}}, {1.5F, 0.2F});
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 1.5, 0.0);
	expectPoint(result.guides[0].from, 0.0, 0.0);

	snapping.priorityRule = assets::GuidePriorityRule::Closest;
	const SnapResult closest = snap({{0.0F, 0.0F}}, {1.5F, 0.2F});
	expectPoint(closest.point, 1.5, 0.35);
	expectPoint(closest.guides[0].from, 3.0, 0.35);
}

TEST_F(SnapGuides, GuideCrossingLedByItsShapePointLine) {
	// Two nearby directions: 20 and 110 degrees from the nearer building, 50 and 140 from the
	// farther. The 20-degree line through the nearer building's corner (1.038, 3.57) and the
	// 50-degree line through the placed point cross near the cursor. The corner line is
	// generated first and is the closer of the two, but the shape point line leads the
	// crossing: it is reported first.
	built.addFoundation({{1.038, 3.57}, {-1.781, 2.544}, {-1.097, 0.665}, {1.722, 1.691}});
	built.addFoundation({{-2.0, 3.0}, {-0.714, 4.532}, {-1.863, 5.496}, {-3.149, 3.964}});
	DrawingAids aids = allAidsOff();
	aids.alignToExistingBuildings = true;
	aids.guideCrossings = true;
	const SnapResult result = snap({{0.0F, 0.0F}}, {3.96F, 4.55F}, foundationInput(aids));
	EXPECT_EQ(result.kind, SnapKind::GuideCrossing);
	expectPoint(result.point, 3.858660, 4.596786);
	ASSERT_EQ(result.guideCount, 2U);
	EXPECT_EQ(result.guides[0].source, GuideSource::NearbyStructure);
	EXPECT_EQ(result.guides[0].relation, GuideRelation::Parallel);
	EXPECT_FALSE(result.guides[0].rightAngle);
	expectPoint(result.guides[0].from, 0.0, 0.0);
	EXPECT_EQ(result.guides[1].source, GuideSource::NearbyStructure);
	EXPECT_EQ(result.guides[1].relation, GuideRelation::Perpendicular);
	EXPECT_TRUE(result.guides[1].rightAngle);
	expectPoint(result.guides[1].from, 1.038, 3.57);
}

TEST_F(SnapGuides, GuideMergeDistance) {
	const auto keptEastWest = [](const GuideStudy& guides) {
		return std::count_if(guides.lines.begin(), guides.lines.end(), [](const snap_guides::GuideLine& line) {
			return line.source == GuideSource::WorldAxes && line.relation == GuideRelation::Parallel &&
				   line.status == GuideLineStatus::Kept;
		});
	};
	const std::vector<Vec2> points{{0.0F, 0.0F}};
	const Vec2				cursor{1.5F, 0.15F};
	ASSERT_FLOAT_EQ(snapping.effectiveGuideMergeDistanceMeters(), 0.3F);

	SnapWorld apart;
	apart.addFoundation({{3.0, 0.31}, {5.0, 0.31}, {5.0, 2.0}, {3.0, 2.0}});
	EXPECT_EQ(keptEastWest(studyGuides(snapping, apart.world, points, cursor, guideAidsOn())), 2);

	SnapWorld close;
	close.addFoundation({{3.0, 0.29}, {5.0, 0.29}, {5.0, 2.0}, {3.0, 2.0}});
	const GuideStudy merged = studyGuides(snapping, close.world, points, cursor, guideAidsOn());
	EXPECT_EQ(keptEastWest(merged), 1);
	const auto throughCorner = std::find_if(merged.lines.begin(), merged.lines.end(), [](const snap_guides::GuideLine& line) {
		return line.source == GuideSource::WorldAxes && line.relation == GuideRelation::Parallel &&
			   line.node.kind == GuideNodeKind::CommittedCorner && line.captured();
	});
	ASSERT_NE(throughCorner, merged.lines.end());
	EXPECT_EQ(throughCorner->status, GuideLineStatus::Merged);
}

TEST_F(SnapGuides, GuideCrossingOnNodeOfMergedLineIgnored) {
	// The kept lines are x = 3 through (3, 5) and y = 0 through (0, 0). They cross at the
	// corner (3, 0), whose own two world guides merged into them; neither kept line was
	// generated through it.
	built.addFoundation({{3.0, 0.0}, {5.0, 0.0}, {5.0, 2.0}, {3.0, 2.0}});
	DrawingAids aids = allAidsOff();
	aids.alignToExistingBuildings = true;
	aids.guidesFromWorldAxes = true;
	aids.guideCrossings = true;
	const std::vector<Vec2> points{{3.0F, 5.0F}, {0.0F, 0.0F}};
	const Vec2				cursor{2.72F, -0.29F};
	const SnapResult		result = snap(points, cursor, foundationInput(aids));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 3.0, -0.29);
	ASSERT_EQ(result.guideCount, 1U);
	expectPoint(result.guides[0].from, 3.0, 5.0);
	EXPECT_EQ(countCrossings(study(points, cursor, aids), CrossingStatus::SharedGeneratingNode), 1U);
}

TEST_F(SnapGuides, WallGuideSquaringOnTJunctionIsStrictlyInside) {
	// The chain's guide y = 2 meets the wall exactly at its end (2, 6): a T-junction there
	// would be an end join, so the point stays the foot. The vertex radius is cut so the
	// end is not caught first.
	snapping.vertexSnapRadiusMeters = 0.1F;
	built.addWall({millimeters(6.0, 2.0), millimeters(6.0, 10.0)});
	const SnapResult result = snapWall({{0.0F, 2.0F}, {3.0F, 2.0F}}, {5.85F, 2.2F});
	EXPECT_EQ(result.kind, SnapKind::WallSegment);
	expectPoint(result.point, 6.0, 2.2);
	EXPECT_EQ(result.guideCount, 0U);
}

TEST_F(SnapGuides, GuideSquaringMinimumAngle) {
	// The shape's guide y = 0 crosses an edge through the origin at 14 degrees (under the
	// 15 degree minimum: the foot) and at 16 degrees (squared onto the crossing).
	const std::vector<Vec2> points{{-8.0F, 0.0F}, {-6.0F, 0.0F}};
	const Vec2				cursor{0.1F, -0.1F};
	const auto				onEdgeAt = [&](double angleDegrees) {
		 const double radians = angleDegrees * kRadiansPerDegree;
		 SnapWorld	  edge;
		 edge.addFoundation(rectangle(-3.0 * std::cos(radians), -3.0 * std::sin(radians), angleDegrees, 6.0, 3.0));
		 return SnapEngine(snapping, edge.world).snap(points, cursor, foundationInput());
	};

	const SnapResult shallow = onEdgeAt(14.0);
	EXPECT_EQ(shallow.kind, SnapKind::Edge);
	expectPoint(shallow.point, 0.070665, 0.017624);
	EXPECT_EQ(shallow.guideCount, 0U);

	const SnapResult steep = onEdgeAt(16.0);
	EXPECT_EQ(steep.kind, SnapKind::Edge);
	expectPoint(steep.point, 0.0, 0.0);
	ASSERT_EQ(steep.guideCount, 1U);
	EXPECT_EQ(steep.guides[0].source, GuideSource::DrawnShape);
}

TEST_F(SnapGuides, WallVerticesOfNearbyStructuresMakeGuides) {
	// The wall tool reads a built wall's vertices as corners: the guide runs square to the
	// wall through its far end. Without them the cursor is left to the angle step.
	built.addWall({millimeters(0.0, 0.0), millimeters(4.0, 3.0)});
	const SnapResult result = snapWall({{30.0F, -20.0F}}, {1.1F, 7.0F});
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, 1.036, 6.952, kNearTolerance);
	ASSERT_EQ(result.guideCount, 1U);
	EXPECT_EQ(result.guides[0].source, GuideSource::NearbyStructure);
	EXPECT_EQ(result.guides[0].relation, GuideRelation::Perpendicular);
	EXPECT_TRUE(result.guides[0].rightAngle);
	expectPoint(result.guides[0].from, 4.0, 3.0);
}

TEST_F(SnapGuides, GuideSquaringReachesOnlyTheEdgeSnapRadius) {
	// The shape's guide through (5.15, 1.71) at 20 degrees crosses the committed foundation's
	// south edge (y = 0) at x = 0.468. Squaring onto it moves the point only when that is
	// within the edge snap radius (0.3 m) of the cursor.
	built.addFoundation({{-10.0, -5.0}, {10.0, -5.0}, {10.0, 0.0}, {-10.0, 0.0}});
	const std::vector<Vec2> points{{5.15F, 1.71F}, {7.97F, 2.74F}};
	ASSERT_FLOAT_EQ(snapping.edgeSnapRadiusMeters, 0.3F);

	const SnapResult beyond = snap(points, {0.0F, 0.1F});
	EXPECT_EQ(beyond.kind, SnapKind::Edge);
	expectPoint(beyond.point, 0.0, 0.0);
	EXPECT_EQ(beyond.guideCount, 0U);

	const SnapResult within = snap(points, {0.3F, 0.1F});
	EXPECT_EQ(within.kind, SnapKind::Edge);
	expectPoint(within.point, 0.468253, 0.0);
	ASSERT_EQ(within.guideCount, 1U);
	EXPECT_EQ(within.guides[0].source, GuideSource::DrawnShape);
	expectPoint(within.guides[0].from, 5.15, 1.71);
}

TEST_F(SnapGuides, GuideResultClosesWithinInputRadius) {
	// The guide x = 0 lands 0.97 m from the first point, the cursor 1.01 m from it: inside a
	// 1 m close radius only the guide result is, and outside the configured 0.5 m neither is.
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 4.0F}};
	const Vec2				cursor{0.28F, 0.97F};
	ASSERT_FLOAT_EQ(snapping.originCloseRadiusMeters, 0.5F);

	const SnapResult configured = snap(points, cursor);
	EXPECT_EQ(configured.kind, SnapKind::GuideLine);
	expectPoint(configured.point, 0.0, 0.97);

	SnapInput widened = foundationInput();
	widened.originCloseRadiusMeters = 1.0F;
	const SnapResult closed = snap(points, cursor, widened);
	EXPECT_EQ(closed.kind, SnapKind::Origin);
	EXPECT_TRUE(closed.closesShape());
	ASSERT_EQ(closed.guideCount, 1U);
	EXPECT_EQ(closed.guides[0].relation, GuideRelation::Perpendicular);

	SnapInput widenedWall = wallInput();
	widenedWall.originCloseRadiusMeters = 1.0F;
	EXPECT_EQ(snapWall(points, cursor, widenedWall).kind, SnapKind::Origin);
}

TEST_F(SnapGuides, WallChainPointWindow) {
	// 34 points against a window of 32: the first point and the last 31 (indices 3 to 33)
	// make guides, so indices 1 and 2 do not; each is alone on its north-south line
	// (x = -5 and x = 2). The window's far edge is pinned by index 2 (out) and 3 (in), its
	// near edge by the first point, which stays in though it is long out of the last 31.
	std::vector<Vec2> chain;
	for (int index = 0; index < 34; ++index) {
		chain.push_back(index == 1 ? Vec2{-5.0F, 10.0F} : Vec2{static_cast<float>(index), static_cast<float>((index % 2) * 3)});
	}
	DrawingAids aids = allAidsOff();
	aids.guidesFromWorldAxes = true;
	SnapInput input = wallInput(aids);
	input.maxPoints = 32;

	const SnapResult outside = snapWall(chain, {-4.9F, 6.0F}, input);
	EXPECT_EQ(outside.kind, SnapKind::None);
	expectPoint(outside.point, -4.9, 6.0);

	const SnapResult justOutside = snapWall(chain, {2.1F, 6.0F}, input);
	EXPECT_EQ(justOutside.kind, SnapKind::None);
	expectPoint(justOutside.point, 2.1, 6.0);

	const SnapResult first = snapWall(chain, {0.1F, 6.0F}, input);
	EXPECT_EQ(first.kind, SnapKind::GuideLine);
	expectPoint(first.point, 0.0, 6.0);
	ASSERT_EQ(first.guideCount, 1U);
	expectPoint(first.guides[0].from, 0.0, 0.0);

	const SnapResult inside = snapWall(chain, {3.1F, 6.0F}, input);
	EXPECT_EQ(inside.kind, SnapKind::GuideLine);
	expectPoint(inside.point, 3.0, 6.0);
	expectPoint(inside.guides[0].from, 3.0, 3.0);

	const std::vector<Vec2> shortChain(chain.begin(), chain.begin() + 32);
	const SnapResult		whole = snapWall(shortChain, {-4.9F, 6.0F}, input);
	EXPECT_EQ(whole.kind, SnapKind::GuideLine);
	expectPoint(whole.guides[0].from, -5.0, 10.0);

	// The window is the input's limit: one as long as the chain takes index 1 back in.
	SnapInput wider = input;
	wider.maxPoints = static_cast<int>(chain.size());
	const SnapResult widened = snapWall(chain, {-4.9F, 6.0F}, wider);
	EXPECT_EQ(widened.kind, SnapKind::GuideLine);
	expectPoint(widened.guides[0].from, -5.0, 10.0);
	EXPECT_NE(snapWall(chain, {-4.9F, 6.0F}, input).kind, SnapKind::GuideLine);
}

// --- The world axes, and what ships ----------------------------------------------------
// The fourth corner of an axis-aligned rectangle locks to both axes once world-axis guides
// and crossings are on; the player's starting aids (shippedAids, from DrawingSettingsDefaults)
// have every helper off and leave that corner to the pointer, the length rounded.

namespace {
	DrawingAids worldAxesAndCrossings() {
		DrawingAids aids;
		aids.guidesFromWorldAxes = true;
		aids.guideCrossings = true;
		return aids;
	}
} // namespace

TEST_F(SnapGuides, WorldAxesAndCrossingsLandFourthCornerOnBothAxes) {
	// The lock is a crossing of two world guides: the north-south line through (0, 0) and the
	// east-west line through (6, 6). Each is square to the shape's edge at its node, so each
	// carries a marker.
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 6.0F}};
	const Vec2				cursor{0.2F, 5.85F};
	for (const bool wallTool : {false, true}) {
		const SnapResult result = snapWith(wallTool, points, cursor, worldAxesAndCrossings());
		EXPECT_EQ(result.kind, SnapKind::GuideCrossing) << wallTool;
		expectPoint(result.point, 0.0, 6.0);
		ASSERT_EQ(result.guideCount, 2U) << wallTool;
		const SnapGuide& vertical = result.guides[0];
		EXPECT_EQ(vertical.source, GuideSource::WorldAxes);
		EXPECT_EQ(vertical.relation, GuideRelation::Perpendicular);
		expectPoint(vertical.from, 0.0, 0.0);
		EXPECT_EQ(vertical.to.x, result.point.x);
		EXPECT_EQ(vertical.to.y, result.point.y);
		EXPECT_NEAR(vertical.distanceFromNodeMeters, 6.0F, kOracleTolerance);
		EXPECT_FALSE(vertical.throughPreviousPoint);
		EXPECT_TRUE(vertical.rightAngle);
		expectPoint(vertical.rightAngleEdgeDirection, 1.0, 0.0, 1e-6F);
		const SnapGuide& horizontal = result.guides[1];
		EXPECT_EQ(horizontal.source, GuideSource::WorldAxes);
		EXPECT_EQ(horizontal.relation, GuideRelation::Parallel);
		expectPoint(horizontal.from, 6.0, 6.0);
		EXPECT_EQ(horizontal.to.x, result.point.x);
		EXPECT_EQ(horizontal.to.y, result.point.y);
		EXPECT_NEAR(horizontal.distanceFromNodeMeters, 6.0F, kOracleTolerance);
		EXPECT_TRUE(horizontal.throughPreviousPoint);
		EXPECT_TRUE(horizontal.rightAngle);
		expectPoint(horizontal.rightAngleEdgeDirection, 0.0, -1.0, 1e-6F);
	}
}

TEST_F(SnapGuides, WorldAxesLockEitherAxis) {
	// Near one world guide and far from the other: the east-west line through (6, 6), then
	// the north-south line through (0, 0).
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 6.0F}};
	for (const bool wallTool : {false, true}) {
		const SnapResult eastWest = snapWith(wallTool, points, {3.0F, 5.85F}, worldAxesAndCrossings());
		EXPECT_EQ(eastWest.kind, SnapKind::GuideLine) << wallTool;
		expectPoint(eastWest.point, 3.0, 6.0);
		ASSERT_EQ(eastWest.guideCount, 1U) << wallTool;
		EXPECT_EQ(eastWest.guides[0].source, GuideSource::WorldAxes);
		EXPECT_EQ(eastWest.guides[0].relation, GuideRelation::Parallel);
		expectPoint(eastWest.guides[0].from, 6.0, 6.0);

		const SnapResult northSouth = snapWith(wallTool, points, {0.2F, 3.0F}, worldAxesAndCrossings());
		EXPECT_EQ(northSouth.kind, SnapKind::GuideLine) << wallTool;
		expectPoint(northSouth.point, 0.0, 3.0);
		ASSERT_EQ(northSouth.guideCount, 1U) << wallTool;
		EXPECT_EQ(northSouth.guides[0].source, GuideSource::WorldAxes);
		EXPECT_EQ(northSouth.guides[0].relation, GuideRelation::Perpendicular);
		expectPoint(northSouth.guides[0].from, 0.0, 0.0);
	}
}

TEST_F(SnapGuides, ShippedAidsOnlyRoundTheLength) {
	// No guide, no crossing, no angle step: the point stays on the pointer's direction from
	// the previous point, its length rounded to 0.1 m. That holds at the fourth corner too.
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 6.0F}};
	for (const bool wallTool : {false, true}) {
		for (const Vec2 cursor : {Vec2{4.0F, 3.0F}, Vec2{0.2F, 5.85F}}) {
			const SnapResult result = snapWith(wallTool, points, cursor, shippedAids());
			EXPECT_EQ(result.kind, SnapKind::None) << wallTool;
			EXPECT_EQ(result.guideCount, 0U) << wallTool;
			EXPECT_EQ(result.lengthSnap, LengthSnapKind::Rounded) << wallTool;
			const double rawLength = distanceBetween(points.back(), cursor);
			const double rounded = std::round(rawLength * 10.0) / 10.0;
			EXPECT_NEAR(result.lengthMeters, rounded, kOracleTolerance) << wallTool;
			EXPECT_NEAR(distanceFromLine(points.back(), degreesOf({cursor.x - 6.0F, cursor.y - 6.0F}), result.point), 0.0, kOracleTolerance);
		}
	}
}
