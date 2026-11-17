// The length step (building-construction D12, Common lengths and The length step), checked
// against the tuned prototype's lengths, levels, and guide-rounding cases with both length
// aids on, as the prototype's page runs them. Expected points are the prototype's outputs for
// the same inputs; D12's names are kept where a case is D12's.

#include "SnapEngine.h"
#include "SnapGuidesDetail.h"
#include "SnapLengthsDetail.h"
#include "SnapTestFixtures.h"

#include <assets/ConstructionRegistry.h>
#include <core/Vec2i64.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace construction;
using namespace construction::snap_test;
using assets::SnappingConfig;
using snap_lengths::LengthEntryStatus;
using snap_lengths::LengthStudy;

namespace {

	constexpr float kOracleTolerance = 1e-4F;
	constexpr float kNearTolerance = 1e-3F;
	// Lengths between millimeter corners, in double: past this is a real difference, not rounding.
	constexpr double kExactTolerance = 1e-9;
	// A drawn corner reads square to the prototype check's three printed decimals.
	constexpr double kSquareCornerToleranceDegrees = 0.005;
	// A drawn edge to the millimeter it commits at.
	constexpr double kHalfMillimeterMeters = 0.0005;
	constexpr double kHalfMillimeter = 0.5;

	// The wall tool reads a 4 m by 3 m foundation's edges between inset corners: each edge less
	// twice the inset, the 0.1 m half thickness and its 2 mm safety (kFlushInsetSafetyMm), so
	// 4 - 2 * 0.102 = 3.796 and 3 - 2 * 0.102 = 2.796.
	constexpr double kInsetFourMeters = 3.796;
	constexpr double kInsetThreeMeters = 2.796;

	void expectPoint(Vec2 actual, double x, double y, float tolerance = kOracleTolerance) {
		EXPECT_NEAR(actual.x, x, tolerance);
		EXPECT_NEAR(actual.y, y, tolerance);
	}

	std::size_t countEntries(const LengthStudy& study, LengthEntryStatus status) {
		return static_cast<std::size_t>(std::count_if(study.entries.begin(), study.entries.end(), [&](const snap_lengths::LengthEntry& entry) {
			return entry.status == status;
		}));
	}

	/// The kept lengths, best first.
	std::vector<double> keptLengths(const LengthStudy& study) {
		std::vector<double> lengths;
		for (std::size_t rank = 0; rank < study.keptCount; ++rank) {
			lengths.push_back(study.groups[study.ranked[rank]].lengthMeters);
		}
		return lengths;
	}

	void expectLengths(const std::vector<double>& actual, const std::vector<double>& expected) {
		ASSERT_EQ(actual.size(), expected.size());
		for (std::size_t index = 0; index < expected.size(); ++index) {
			EXPECT_NEAR(actual[index], expected[index], kNearTolerance) << index;
		}
	}

	DrawingAids without(DrawingAids aids, bool DrawingAids::* aid) {
		aids.*aid = false;
		return aids;
	}

	class LengthCases : public ::testing::Test {
	  protected:
		SnappingConfig snapping;
		SnapWorld	   built;

		[[nodiscard]] SnapEngine engine() const { return SnapEngine(snapping, built.world); }
		[[nodiscard]] SnapResult
		snap(const std::vector<Vec2>& points, Vec2 cursor, const SnapInput& input = foundationInput(everyAidOn())) const {
			return engine().snap(points, cursor, input);
		}
		[[nodiscard]] SnapResult snapWall(const std::vector<Vec2>& points, Vec2 cursor, const SnapInput& input = wallInput(everyAidOn())) const {
			return engine().snapWall(points, cursor, input);
		}
		[[nodiscard]] LengthStudy lengths(const std::vector<Vec2>& points, Vec2 cursor, snap_guides::SnapTool tool = snap_guides::SnapTool::Foundation) const {
			const snap_guides::GuideContext context{
				.snapping = &snapping,
				.world = &built.world,
				.points = &points,
				.cursor = {static_cast<double>(cursor.x), static_cast<double>(cursor.y)},
				.aids = everyAidOn(),
				.tool = tool,
				.wallHalfThicknessMm = tool == snap_guides::SnapTool::Wall ? kWallHalfThicknessMm : 0,
			};
			LengthStudy study;
			snap_lengths::studyCommonLengths(context, study);
			return study;
		}
	};

	class CommonLengths : public LengthCases {};
	class DrawingLevels : public LengthCases {};
	class GuideRounding : public LengthCases {};

	// The designer's case: a 4 m by 3 m shack at 20 degrees, a second drawn beside it.
	const Vec2 kShackStart{7.177F, -2.177F};

} // namespace

// --- Common lengths -------------------------------------------------------------------

TEST_F(CommonLengths, FirstShackGivesTwoLengths) {
	built.addFoundation(rectangle(0.0, 0.0, 20.0, 4.0, 3.0));
	const LengthStudy study = lengths({kShackStart}, polar(kShackStart, 20.0, 3.85));
	ASSERT_EQ(study.keptCount, 2U);
	for (std::size_t rank = 0; rank < 2; ++rank) {
		EXPECT_EQ(study.groups[study.ranked[rank]].memberCount, 2U);
	}
	expectLengths(keptLengths(study), {3.0, 4.0});
}

TEST_F(CommonLengths, CatchFromShortAndLong) {
	built.addFoundation(rectangle(0.0, 0.0, 20.0, 4.0, 3.0));
	for (const double along : {3.85, 4.2}) {
		const SnapResult result = snap({kShackStart}, polar(kShackStart, 20.0, along));
		EXPECT_EQ(result.kind, SnapKind::GuideLine) << along;
		EXPECT_EQ(result.guides[0].source, GuideSource::NearbyStructure) << along;
		EXPECT_EQ(result.lengthSnap, LengthSnapKind::CommonLength) << along;
		EXPECT_NEAR(result.lengthMeters, 4.0F, kNearTolerance) << along;
		EXPECT_NEAR(distanceBetween(kShackStart, result.point), 4.0, kNearTolerance) << along;
		expectPoint(result.lengthFrom, kShackStart.x, kShackStart.y);
		EXPECT_EQ(result.matchedEdgeCount, 2U) << along;
	}
	const SnapResult far = snap({kShackStart}, polar(kShackStart, 20.0, 4.5));
	EXPECT_EQ(far.lengthSnap, LengthSnapKind::Rounded);
	EXPECT_NEAR(distanceBetween(kShackStart, far.point), 4.5, kOracleTolerance);
	EXPECT_EQ(far.matchedEdgeCount, 0U);
}

TEST_F(CommonLengths, SecondEdgeCatchesOnPerpendicular) {
	built.addFoundation(rectangle(0.0, 0.0, 20.0, 4.0, 3.0));
	std::vector<Vec2> points{kShackStart};
	points.push_back(placed(snap(points, polar(kShackStart, 20.0, 3.85)).point));
	const SnapResult result = snap(points, polar(points[1], 110.0, 2.85));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_EQ(result.guides[0].source, GuideSource::DrawnShape);
	EXPECT_EQ(result.guides[0].relation, GuideRelation::Perpendicular);
	EXPECT_EQ(result.lengthSnap, LengthSnapKind::CommonLength);
	EXPECT_NEAR(distanceBetween(points[1], result.point), 3.0, kNearTolerance);
}

TEST_F(CommonLengths, SecondShackClosesSquare) {
	built.addFoundation(rectangle(0.0, 0.0, 20.0, 4.0, 3.0));
	std::vector<Vec2> points{kShackStart};
	points.push_back(placed(snap(points, polar(kShackStart, 20.0, 3.85)).point));
	points.push_back(placed(snap(points, polar(points[1], 110.0, 2.85)).point));
	const SnapResult third = snap(points, polar(points[2], 200.0, 3.9));
	EXPECT_EQ(third.kind, SnapKind::GuideCrossing);
	points.push_back(placed(third.point));
	const SnapResult closing = snap(points, {kShackStart.x + 0.12F, kShackStart.y + 0.05F});
	EXPECT_EQ(closing.kind, SnapKind::Origin);

	const std::size_t count = points.size();
	const double	  edges[] = {4.0, 3.0, 4.0, 3.0};
	for (std::size_t index = 0; index < count; ++index) {
		const Vec2 previous = points[(index + count - 1) % count];
		const Vec2 next = points[(index + 1) % count];
		EXPECT_NEAR(cornerDegrees(previous, points[index], next), 90.0, kSquareCornerToleranceDegrees) << index;
		const double edge = distanceBetween(points[index], next);
		EXPECT_NEAR(edge, edges[index], kHalfMillimeterMeters) << index;
		const auto& kept = closing.commonLengths;
		EXPECT_TRUE(std::any_of(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(closing.commonLengthCount), [&](float length) {
			return std::abs(static_cast<double>(length) - edge) <= static_cast<double>(snapping.commonLengthMergeMeters);
		})) << index;
	}
}

TEST_F(CommonLengths, NearerOfTwoLengthsWins) {
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 4.25));
	const SnapResult result = snap({{0.0F, -10.0F}}, {4.15F, -10.0F});
	EXPECT_EQ(result.lengthSnap, LengthSnapKind::CommonLength);
	EXPECT_NEAR(result.lengthMeters, 4.25F, kOracleTolerance);
	expectPoint(result.point, 4.25, -10.0);
}

TEST_F(CommonLengths, EqualSlideKeepsBetterRank) {
	// 3.75 m and 4.25 m are both exactly 0.25 m from the cursor; 3.75 ranks first (shorter).
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 3.75, 4.25));
	const SnapResult result = snap({{0.0F, -10.0F}}, {4.0F, -10.0F});
	EXPECT_EQ(result.lengthSnap, LengthSnapKind::CommonLength);
	expectPoint(result.point, 3.75, -10.0);
}

TEST_F(CommonLengths, CatchIncludesItsBoundary) {
	snapping.commonLengthCatchMeters = 0.25F;
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 6.0));
	EXPECT_EQ(snap({{0.0F, -10.0F}}, {4.25F, -10.0F}).lengthSnap, LengthSnapKind::CommonLength);
	EXPECT_EQ(snap({{0.0F, -10.0F}}, {std::nextafter(4.25F, 5.0F), -10.0F}).lengthSnap, LengthSnapKind::Rounded);
}

TEST_F(CommonLengths, MergeKeepsHigherPriorityLength) {
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 4.03));
	const LengthStudy study = lengths({{0.0F, -10.0F}}, {4.1F, -10.0F});
	ASSERT_EQ(study.groups.size(), 1U);
	EXPECT_EQ(study.groups[0].memberCount, 4U);
	EXPECT_NEAR(study.groups[0].lengthMeters, 4.0, kExactTolerance);
	EXPECT_EQ(countEntries(study, LengthEntryStatus::Merged), 2U);
	expectPoint(snap({{0.0F, -10.0F}}, {4.1F, -10.0F}).point, 4.0, -10.0);

	// A newer building's 4.03 m leads the group over an older 4.00 m.
	SnapWorld older;
	older.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 9.0));
	older.addFoundation(rectangle(10.0, 0.0, 0.0, 4.03, 7.0));
	const SnapResult newer = SnapEngine(snapping, older.world).snap({{0.0F, -10.0F}}, {4.1F, -10.0F}, foundationInput(everyAidOn()));
	EXPECT_NEAR(newer.lengthMeters, 4.03F, kOracleTolerance);
}

TEST_F(CommonLengths, EntryJoinsFirstGroupInReach) {
	// 4.04 m is within the merge distance of the newest 4.00 m and of 4.08 m; it joins 4.00, made first.
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.04, 13.0));
	built.addFoundation(rectangle(10.0, 0.0, 0.0, 4.08, 11.0));
	built.addFoundation(rectangle(20.0, 0.0, 0.0, 4.0, 9.0));
	const LengthStudy study = lengths({}, {0.0F, -5.0F});
	ASSERT_EQ(study.groups.size(), 5U);
	EXPECT_NEAR(study.groups[study.ranked[0]].lengthMeters, 4.0, kExactTolerance);
	EXPECT_EQ(study.groups[study.ranked[0]].memberCount, 4U);
}

TEST_F(CommonLengths, MergeDistanceBoundary) {
	// Lengths exactly the merge distance apart are one; a millimeter more, two.
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 4.05));
	EXPECT_EQ(lengths({{0.0F, -10.0F}}, {4.1F, -10.0F}).groups.size(), 1U);
	SnapWorld apart;
	apart.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 4.051));
	std::vector<Vec2> points{{0.0F, -10.0F}};
	const snap_guides::GuideContext context{
		.snapping = &snapping, .world = &apart.world, .points = &points, .cursor = {4.1, -10.0}, .aids = everyAidOn()
	};
	LengthStudy study;
	snap_lengths::studyCommonLengths(context, study);
	EXPECT_EQ(study.groups.size(), 2U);
}

TEST_F(CommonLengths, CrossingKeepsItsPoint) {
	built.addFoundation(rectangle(-3.0, 7.0, 0.0, 5.8, 2.0));
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 4.0F}};
	const SnapResult		kept = snap(points, {0.18F, 4.05F});
	EXPECT_EQ(kept.kind, SnapKind::GuideCrossing);
	expectPoint(kept.point, 0.0, 4.0);
	EXPECT_EQ(kept.lengthSnap, LengthSnapKind::None);

	snapping.commonLengthBeatsCrossing = true;
	const SnapResult yielded = snap(points, {0.18F, 4.05F});
	EXPECT_EQ(yielded.kind, SnapKind::GuideLine);
	EXPECT_EQ(yielded.guideCount, 1U);
	expectPoint(yielded.point, 0.2, 4.0);
	EXPECT_EQ(yielded.lengthSnap, LengthSnapKind::CommonLength);
	EXPECT_NEAR(yielded.lengthMeters, 5.8F, kOracleTolerance);
}

TEST_F(CommonLengths, ExistingCornerKeepsItsPoint) {
	// With common lengths on and off (levels' IncrementLeavesExistingCorner): no length step.
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 3.0));
	const SnapResult corner = snap({{0.0F, -5.0F}}, {4.2F, 0.1F});
	EXPECT_EQ(corner.kind, SnapKind::Vertex);
	expectPoint(corner.point, 4.0, 0.0);
	EXPECT_EQ(corner.lengthSnap, LengthSnapKind::None);

	SnapWorld world;
	world.addFoundation({{3.03, 0.07}, {7.0, 0.07}, {7.0, 3.0}, {3.03, 3.0}});
	const SnapResult rounding =
		SnapEngine(snapping, world.world).snap({{0.0F, -4.0F}}, {3.2F, 0.2F}, foundationInput(without(everyAidOn(), &DrawingAids::commonLengths)));
	EXPECT_EQ(rounding.kind, SnapKind::Vertex);
	expectPoint(rounding.point, 3.03, 0.07);
	EXPECT_EQ(rounding.lengthSnap, LengthSnapKind::None);
}

TEST_F(CommonLengths, FreeformDropsLengths) {
	// Alt drops common lengths and rounding alike (levels' IncrementOffUnderAlt).
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 3.0));
	const SnapResult result = snap({{0.0F, -10.0F}}, {3.9F, -10.0F}, SnapInput{.aids = everyAidOn(), .freeform = true});
	EXPECT_EQ(result.kind, SnapKind::None);
	expectPoint(result.point, 3.9, -10.0);
	EXPECT_EQ(result.lengthSnap, LengthSnapKind::None);
	EXPECT_EQ(result.commonLengthCount, 0U);
	const Vec2		 stepped = polar({0.0F, 20.0F}, 31.0, 5.06);
	const SnapResult raw = snap({{0.0F, 20.0F}}, stepped, SnapInput{.aids = everyAidOn(), .freeform = true});
	expectPoint(raw.point, stepped.x, stepped.y);
}

TEST_F(CommonLengths, RankingNewestBuildingFirst) {
	const geometry::Ring shack = rectangle(0.0, 0.0, 0.0, 4.0, 3.0);
	const geometry::Ring longhouse = rectangle(10.0, 0.0, 0.0, 10.0, 5.0);
	built.addFoundation(shack);
	built.addFoundation(longhouse);
	const Vec2 start{0.0F, -8.0F};
	expectLengths(keptLengths(lengths({start}, {3.9F, -8.0F})), {5.0, 10.0, 3.0, 4.0});
	expectPoint(snap({start}, {3.9F, -8.0F}).point, 4.0, -8.0);

	snapping.commonLengthMaxCount = 2;
	expectLengths(keptLengths(lengths({start}, {3.9F, -8.0F})), {5.0, 10.0});
	const SnapResult limited = snap({start}, {3.9F, -8.0F});
	EXPECT_NE(limited.lengthSnap, LengthSnapKind::CommonLength);
	expectPoint(limited.point, 3.9, -8.0);
	snapping.commonLengthMaxCount = 6;

	SnapWorld swapped;
	swapped.addFoundation(longhouse);
	swapped.addFoundation(shack);
	std::vector<Vec2>				points{start};
	const snap_guides::GuideContext context{
		.snapping = &snapping, .world = &swapped.world, .points = &points, .cursor = {3.9, -8.0}, .aids = everyAidOn()
	};
	LengthStudy study;
	snap_lengths::studyCommonLengths(context, study);
	expectLengths(keptLengths(study), {3.0, 4.0, 5.0, 10.0});
	EXPECT_EQ(SnapEngine(snapping, swapped.world).snap(points, {3.9F, -8.0F}, foundationInput(everyAidOn())).lengthSnap, LengthSnapKind::CommonLength);
}

TEST_F(CommonLengths, GroupRankingKeys) {
	// More members first: a staircase whose 5 m edge appears three times leads its shorter 2 m.
	built.addFoundation({{0.0, 0.0}, {5.0, 0.0}, {5.0, 5.0}, {10.0, 5.0}, {10.0, 7.0}, {0.0, 7.0}});
	expectLengths(keptLengths(lengths({}, {0.0F, -3.0F})), {5.0, 2.0, 7.0, 10.0});

	// Equal newest and count: the nearer owner first. Both groups hold the newest shed's edges;
	// the 6 m group also holds a nearer building's, the 4 m group a farther one's.
	SnapWorld world;
	world.addFoundation(rectangle(2.0, 2.0, 0.0, 6.0, 9.0));	 // near: 6 and 9
	world.addFoundation(rectangle(20.0, 0.0, 0.0, 4.0, 11.0)); // far: 4 and 11
	world.addFoundation(rectangle(-20.0, 0.0, 0.0, 4.0, 6.0)); // newest: 4 and 6
	std::vector<Vec2>				points;
	const snap_guides::GuideContext context{.snapping = &snapping, .world = &world.world, .points = &points, .cursor = {}, .aids = everyAidOn()};
	LengthStudy						study;
	snap_lengths::studyCommonLengths(context, study);
	expectLengths(keptLengths(study), {6.0, 4.0, 11.0, 9.0});
}

TEST_F(CommonLengths, MinimumAndRange) {
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 0.4));
	built.addFoundation(rectangle(60.0, 0.0, 0.0, 7.0, 7.0));
	const Vec2		  start{0.0F, -8.0F};
	const LengthStudy study = lengths({start}, {3.9F, -8.0F});
	EXPECT_EQ(countEntries(study, LengthEntryStatus::UnderMinimum), 2U);
	// The far building's bounds lie past the range, so it lends no entries at all.
	EXPECT_EQ(study.entries.size(), 4U);
	EXPECT_EQ(countEntries(study, LengthEntryStatus::OutOfRange), 0U);

	snapping.commonLengthAllBuildings = true;
	const SnapResult everywhere = snap({start}, {6.9F, -8.0F});
	EXPECT_EQ(everywhere.lengthSnap, LengthSnapKind::CommonLength);
	EXPECT_NEAR(everywhere.lengthMeters, 7.0F, kOracleTolerance);
}

TEST_F(CommonLengths, MinimumAndRangeBoundaries) {
	// An edge exactly the minimum is kept; an owner exactly the range away lends, and one a
	// millimeter farther, its bounds past the range, is skipped unread: no entries at all.
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 0.5));
	built.addFoundation(rectangle(20.0, 22.0, 0.0, 3.0, 3.0));
	built.addFoundation(rectangle(30.0, 22.001, 0.0, 7.0, 7.0));
	const LengthStudy study = lengths({}, {20.0F, -8.0F});
	EXPECT_EQ(countEntries(study, LengthEntryStatus::UnderMinimum), 0U);
	EXPECT_EQ(study.entries.size(), 8U);
	EXPECT_EQ(countEntries(study, LengthEntryStatus::OutOfRange), 0U);
	expectLengths(keptLengths(study), {3.0, 0.5, 4.0});
}

TEST_F(CommonLengths, SwitchOffLeavesLength) {
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 0.4));
	built.addFoundation(rectangle(60.0, 0.0, 0.0, 7.0, 7.0));
	const SnapResult result = snap({{0.0F, -8.0F}}, {3.9F, -8.0F}, foundationInput(without(everyAidOn(), &DrawingAids::commonLengths)));
	expectPoint(result.point, 3.9, -8.0);
	EXPECT_EQ(result.commonLengthCount, 0U);
	EXPECT_NE(result.lengthSnap, LengthSnapKind::CommonLength);
}

TEST_F(CommonLengths, SourcesFollowConfig) {
	built.addFoundation(rectangle(0.0, 6.0, 0.0, 4.0, 3.0));
	const std::vector<Vec2> points{{0.0F, 0.0F}, {5.5F, 0.0F}, {5.5F, 2.0F}};
	expectLengths(keptLengths(lengths(points, {2.0F, 2.0F})), {2.0, 5.5, 3.0, 4.0});
	snapping.commonLengthSourceShape = false;
	expectLengths(keptLengths(lengths(points, {2.0F, 2.0F})), {3.0, 4.0});
	snapping.commonLengthSourceShape = true;
	snapping.commonLengthSourceBuildings = false;
	expectLengths(keptLengths(lengths(points, {2.0F, 2.0F})), {2.0, 5.5});
}

TEST_F(CommonLengths, ShapeLendsItsOwnEdge) {
	const std::vector<Vec2> points{{0.0F, 0.0F}, {5.5F, 0.0F}, {5.5F, 2.0F}};
	const SnapResult		crossing = snap(points, {0.2F, 2.15F});
	EXPECT_EQ(crossing.kind, SnapKind::GuideCrossing);
	expectPoint(crossing.point, 0.0, 2.0);

	const SnapResult single = snap(points, {0.2F, 2.15F}, foundationInput(without(everyAidOn(), &DrawingAids::guideCrossings)));
	EXPECT_EQ(single.kind, SnapKind::GuideLine);
	expectPoint(single.point, 0.0, 2.0);
	EXPECT_EQ(single.lengthSnap, LengthSnapKind::CommonLength);
	EXPECT_NEAR(single.lengthMeters, 5.5F, kOracleTolerance);
	ASSERT_EQ(single.matchedEdgeCount, 1U);
	expectPoint(single.matchedEdges[0].start, 0.0, 0.0);
	expectPoint(single.matchedEdges[0].end, 5.5, 0.0);
}

TEST_F(CommonLengths, WallToolReadsInsetAndWalls) {
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 3.0));
	built.addWall({millimeters(0.0, 10.0), millimeters(2.5, 10.0)});
	const Vec2		 start{-0.5F, -6.0F};
	const SnapResult result = snapWall({start}, {3.2F, -6.0F});
	const auto		 kept = [&](const SnapResult& snapped, double length) {
		  return std::any_of(
			  snapped.commonLengths.begin(),
			  snapped.commonLengths.begin() + static_cast<std::ptrdiff_t>(snapped.commonLengthCount),
			  [&](float value) { return std::abs(static_cast<double>(value) - length) < static_cast<double>(kOracleTolerance); }
		  );
	};
	EXPECT_TRUE(kept(result, kInsetFourMeters));
	EXPECT_TRUE(kept(result, kInsetThreeMeters));
	EXPECT_TRUE(kept(result, 2.5));
	expectPoint(result.point, 3.296, -6.0);
	EXPECT_NEAR(distanceBetween(start, result.point), kInsetFourMeters, kOracleTolerance);

	snapping.commonLengthWallsReadInsetFoundation = false;
	const SnapResult raw = snapWall({start}, {3.2F, -6.0F});
	EXPECT_TRUE(kept(raw, 4.0));
	EXPECT_TRUE(kept(raw, 3.0));
	EXPECT_FALSE(kept(raw, kInsetFourMeters));
}

TEST_F(CommonLengths, FoundationToolIgnoresWalls) {
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 3.0));
	built.addWall({millimeters(0.0, 10.0), millimeters(2.5, 10.0)});
	expectLengths(keptLengths(lengths({}, {0.0F, -6.0F})), {3.0, 4.0});
}

TEST_F(CommonLengths, WallToolReadsInsetAtAnyDistanceUnderAllBuildings) {
	snapping.commonLengthAllBuildings = true;
	built.addFoundation(rectangle(60.0, 0.0, 0.0, 4.0, 3.0));
	expectLengths(keptLengths(lengths({}, {0.0F, -6.0F}, snap_guides::SnapTool::Wall)), {kInsetThreeMeters, kInsetFourMeters});
}

TEST_F(CommonLengths, WallToolReadsInsetLengthsExactlyFarFromTheOrigin) {
	// 4000 m from the origin a float step is 2^-12 m, about a quarter of a millimeter: float corners
	// would round each coordinate by up to half a step and miss the exact length by far more than 1e-9.
	built.addFoundation(rectangle(4000.123, 3000.321, 0.0, 4.0, 3.0));
	const std::vector<double> kept = keptLengths(lengths({}, {4000.1F, 2994.0F}, snap_guides::SnapTool::Wall));
	ASSERT_EQ(kept.size(), 2U);
	EXPECT_NEAR(kept[0], kInsetThreeMeters, kExactTolerance);
	EXPECT_NEAR(kept[1], kInsetFourMeters, kExactTolerance);
}

TEST_F(CommonLengths, WallToolReadsInsetOfOwnerExactlyAtRange) {
	built.addFoundation(rectangle(0.0, 22.0, 0.0, 4.0, 3.0)); // its nearest edge is 30 m from the cursor
	expectLengths(keptLengths(lengths({}, {0.0F, -8.0F}, snap_guides::SnapTool::Wall)), {kInsetThreeMeters, kInsetFourMeters});
}

TEST_F(CommonLengths, OwnerDistanceIsToTheNearestEdge) {
	// Beside the middle of a 100 m building: 8 m from its edge, over 50 m from any corner.
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 100.0, 3.0));
	expectLengths(keptLengths(lengths({}, {50.0F, -8.0F})), {3.0, 100.0});
}

TEST_F(CommonLengths, BoundsInsideTheRangeAreRead) {
	// Only bounds past the range cull (MinimumAndRangeBoundaries): a diamond whose box is 14 m
	// away but whose edges are 35 m away, and a wall whose box holds the cursor but whose line
	// is 35 m off, are read, and the range drops them, as it does every owner the cull skips.
	SnapWorld diamond;
	diamond.addFoundation({{30.0, 0.0}, {0.0, 30.0}, {-30.0, 0.0}, {0.0, -30.0}});
	diamond.addWall({millimeters(0.0, 130.0), millimeters(130.0, 0.0)});
	std::vector<Vec2>				points;
	const snap_guides::GuideContext context{
		.snapping = &snapping,
		.world = &diamond.world,
		.points = &points,
		.cursor = {40.0, 40.0},
		.aids = everyAidOn(),
		.tool = snap_guides::SnapTool::Wall,
		.wallHalfThicknessMm = kWallHalfThicknessMm,
	};
	LengthStudy study;
	snap_lengths::studyCommonLengths(context, study);
	EXPECT_EQ(countEntries(study, LengthEntryStatus::OutOfRange), 5U);
	EXPECT_EQ(study.keptCount, 0U);
}

TEST_F(CommonLengths, RecencyRanksFoundationsAndChainsTogether) {
	// A foundation, then a wall, then a foundation: the newest foundation's lengths lead, then the
	// wall's, then the oldest foundation's (2.796 m leads, held by the newest and the oldest).
	built.addFoundation(rectangle(20.0, 0.0, 0.0, 4.0, 3.0));
	built.addWall({millimeters(0.0, 10.0), millimeters(7.0, 10.0)});
	built.addFoundation(rectangle(30.0, 0.0, 0.0, 5.0, 3.0));
	expectLengths(
		keptLengths(lengths({{-0.5F, -6.0F}}, {3.9F, -6.0F}, snap_guides::SnapTool::Wall)),
		{kInsetThreeMeters, 4.796, 7.0, kInsetFourMeters}
	);
}

TEST_F(CommonLengths, ShapeIsTheNearestOwner) {
	// The shape's owner distance is 0, and a group's nearest owner is its nearest member's: the
	// 4 m and 6 m groups each hold a shape edge, so they lead the farther building's 3 m.
	built.addFoundation(rectangle(0.0, 20.0, 0.0, 6.0, 3.0));
	built.addFoundation(rectangle(0.0, 25.0, 0.0, 4.0, 3.0));
	expectLengths(keptLengths(lengths({{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 6.0F}}, {0.0F, 6.0F})), {4.0, 6.0, 3.0});
}

TEST_F(CommonLengths, MoreMembersRankBeforeNearerOwner) {
	built.addFoundation(rectangle(30.0, 0.0, 0.0, 4.0, 4.0));  // far: four 4 m edges
	built.addFoundation(rectangle(2.0, 2.0, 0.0, 6.0, 9.0));   // near: 6 and 9
	built.addFoundation(rectangle(-20.0, 0.0, 0.0, 4.0, 6.0)); // newest: 4 and 6
	expectLengths(keptLengths(lengths({}, {0.0F, 0.0F})), {4.0, 6.0, 9.0});
}

TEST_F(CommonLengths, WallChainIsOneOwner) {
	// One commit through an existing vertex is two segments of one chain: the chain is as near as
	// its nearest segment, so its far 35 m segment still lends inside a 5 m range.
	snapping.commonLengthRangeMeters = 5.0F;
	built.addWall({millimeters(5.0, 0.0), millimeters(5.0, 3.0)});
	built.addWall({millimeters(0.0, 0.0), millimeters(40.0, 0.0)});
	const LengthStudy study = lengths({}, {0.0F, 1.0F}, snap_guides::SnapTool::Wall);
	const auto		  lengthsKept = keptLengths(study);
	EXPECT_NE(
		std::find_if(lengthsKept.begin(), lengthsKept.end(), [](double length) { return std::abs(length - 35.0) < kExactTolerance; }),
		lengthsKept.end()
	);
}

TEST_F(CommonLengths, ChainDrawnInOneCommitIsOneOwner) {
	// Three segments drawn as one chain share its placement order and its nearest segment's
	// distance, so both 40 m segments, far outside a 5 m range, still lend, as one group of two.
	snapping.commonLengthRangeMeters = 5.0F;
	built.addWall({millimeters(0.0, 0.0), millimeters(5.0, 0.0), millimeters(5.0, 40.0), millimeters(45.0, 40.0)});
	const LengthStudy study = lengths({}, {1.0F, 1.0F}, snap_guides::SnapTool::Wall);
	ASSERT_EQ(study.entries.size(), 3U);
	for (const snap_lengths::LengthEntry& entry : study.entries) {
		EXPECT_EQ(entry.recency, study.entries[0].recency);
		EXPECT_NEAR(entry.ownerMeters, 1.0, kExactTolerance);
		EXPECT_EQ(entry.status, LengthEntryStatus::Member);
	}
	expectLengths(keptLengths(study), {40.0, 5.0});
	EXPECT_EQ(study.groups[study.ranked[0]].memberCount, 2U);
}

TEST_F(CommonLengths, ChainsCommittedApartAreTwoOwners) {
	built.addWall({millimeters(0.0, 0.0), millimeters(3.0, 0.0), millimeters(3.0, 2.0)});
	built.addWall({millimeters(10.0, 0.0), millimeters(14.0, 0.0), millimeters(14.0, 6.0)});
	const LengthStudy study = lengths({}, {0.0F, -5.0F}, snap_guides::SnapTool::Wall);
	ASSERT_EQ(study.entries.size(), 4U);
	EXPECT_LT(study.entries[0].recency, study.entries[2].recency);
	expectLengths(keptLengths(study), {4.0, 6.0, 2.0, 3.0});
}

TEST_F(CommonLengths, ChainSegmentsFoundInAnyVectorOrder) {
	// X, then a chain drawn through X's two vertices (one segment each side of it), then a far
	// wall, then a tee that splits the chain's left segment: the halves go to the end of the
	// world's segments, after the far wall, and still belong to the chain near the cursor.
	snapping.commonLengthRangeMeters = 5.0F;
	ConstructionWorld& world = built.world;
	ASSERT_TRUE(world.commitSegment({0, 0}, {10000, 0}, "Wood", "Standard", kInvalidFoundation, world.version()).ok());
	ASSERT_EQ(
		world.commitSegment({-5000, 0}, {15000, 0}, "Wood", "Standard", kInvalidFoundation, world.version()).createdSegments.size(), 2U
	);
	ASSERT_TRUE(world.commitSegment({100000, 100000}, {100000, 103000}, "Wood", "Standard", kInvalidFoundation, world.version()).ok());
	ASSERT_TRUE(world.commitSegment({-2500, -3000}, {-2500, 0}, "Wood", "Standard", kInvalidFoundation, world.version()).ok());
	const LengthStudy study = lengths({}, {12.0F, 1.0F}, snap_guides::SnapTool::Wall);
	expectLengths(keptLengths(study), {2.5, 5.0, 10.0});
	// X's one segment and the chain's three; the far wall and the tee lie wholly past the range.
	EXPECT_EQ(study.entries.size(), 4U);
}

TEST_F(CommonLengths, ReportedLengthsAreTheKeptOnes) {
	snapping.commonLengthMaxCount = 2;
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 3.0));
	built.addFoundation(rectangle(10.0, 0.0, 0.0, 10.0, 5.0));
	const SnapResult result = snap({{0.0F, -8.0F}}, {3.9F, -8.0F});
	ASSERT_EQ(result.commonLengthCount, 2U);
	EXPECT_NEAR(result.commonLengths[0], 5.0F, kOracleTolerance);
	EXPECT_NEAR(result.commonLengths[1], 10.0F, kOracleTolerance);
}

TEST_F(CommonLengths, ReportedListsStopAtTheirCaps) {
	// Twenty distinct lengths: the sixteen best kept and reported, in rank order, newest building
	// first, however high commonLengthMaxCount is set (the validator refuses it past 16).
	snapping.commonLengthMaxCount = 40;
	snapping.commonLengthRangeMeters = 1000.0F;
	for (int index = 0; index < 10; ++index) {
		built.addFoundation(rectangle(10.0 * index, 0.0, 0.0, 1.0 + 0.5 * index, 2.25 + 0.5 * index));
	}
	const SnapResult many = snap({{0.0F, -5.0F}}, {5.1F, -5.0F});
	ASSERT_EQ(many.commonLengthCount, kMaxReportedCommonLengths);
	const double expected[] = {5.5, 6.75, 5.0, 6.25, 4.5, 5.75, 4.0, 5.25, 3.5, 4.75, 3.0, 4.25, 2.5, 3.75, 2.0, 3.25};
	for (std::size_t rank = 0; rank < kMaxReportedCommonLengths; ++rank) {
		EXPECT_NEAR(many.commonLengths[rank], expected[rank], kOracleTolerance) << rank;
	}

	// Thirty-six matched 4 m edges (seventeen sheds, one of them square): the 32 of highest
	// priority, newest shed first, so the two oldest sheds' edges are the ones left out.
	SnapWorld sheds;
	for (int index = 0; index < 17; ++index) {
		sheds.addFoundation(rectangle(10.0 * index, 0.0, 0.0, 4.0, 2.0 + 0.5 * index));
	}
	const SnapResult matched = SnapEngine(snapping, sheds.world).snap({{1.0F, -5.0F}}, {5.1F, -5.0F}, foundationInput(everyAidOn()));
	ASSERT_EQ(matched.lengthSnap, LengthSnapKind::CommonLength);
	ASSERT_EQ(matched.matchedEdgeCount, kMaxMatchedLengthEdges);
	expectPoint(matched.matchedEdges[0].start, 160.0, 0.0);
	expectPoint(matched.matchedEdges[0].end, 164.0, 0.0);
	expectPoint(matched.matchedEdges[kMaxMatchedLengthEdges - 1].start, 24.0, 3.0);
	expectPoint(matched.matchedEdges[kMaxMatchedLengthEdges - 1].end, 20.0, 3.0);
}

TEST_F(CommonLengths, MatchedEdgesLeaveOutDroppedEntries) {
	// The shack's 0.4 m edges are under the minimum: only its two 4 m edges are ticked.
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 0.4));
	const SnapResult result = snap({{0.0F, -8.0F}}, {3.9F, -8.0F});
	ASSERT_EQ(result.lengthSnap, LengthSnapKind::CommonLength);
	EXPECT_EQ(result.matchedEdgeCount, 2U);
}

TEST_F(CommonLengths, ResultFieldsAfterACommonLength) {
	// On a guide through another point, a common length measures the segment, from its start.
	built.addFoundation(rectangle(0.0, 10.0, 0.0, 4.6, 3.0));
	const SnapResult alongGuide = snap({{-6.0F, 6.0F}}, {-3.67F, 10.05F});
	ASSERT_EQ(alongGuide.lengthSnap, LengthSnapKind::CommonLength);
	expectPoint(alongGuide.point, -3.728437, 10.0);
	expectPoint(alongGuide.lengthFrom, -6.0, 6.0);

	// The guide is drawn to the point the length moved it to.
	const SnapResult single = snap(
		{{0.0F, 0.0F}, {5.5F, 0.0F}, {5.5F, 2.0F}}, {0.2F, 2.15F}, foundationInput(without(everyAidOn(), &DrawingAids::guideCrossings))
	);
	ASSERT_EQ(single.lengthSnap, LengthSnapKind::CommonLength);
	expectPoint(single.lengthFrom, 5.5, 2.0);
	ASSERT_EQ(single.guideCount, 1U);
	expectPoint(single.guides[0].to, single.point.x, single.point.y);

	// A crossing that yields reports the line the point slid along: the shape's, through (6, 4).
	SnapWorld yielding;
	yielding.addFoundation(rectangle(-3.0, 7.0, 0.0, 5.8, 2.0));
	snapping.commonLengthBeatsCrossing = true;
	const SnapResult yielded = SnapEngine(snapping, yielding.world)
								   .snap({{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 4.0F}}, {0.18F, 4.05F}, foundationInput(everyAidOn()));
	ASSERT_EQ(yielded.guideCount, 1U);
	expectPoint(yielded.guides[0].from, 6.0, 4.0);
}

TEST_F(CommonLengths, WallEndAndJunctionKeepTheirPoints) {
	// With lengths on, a snap onto a wall's end or interior keeps its point and reports no
	// length, though 2.93 m would round and the 2.5 m wall lends a length.
	built.addWall({millimeters(0.0, 10.0), millimeters(2.5, 10.0)});
	const SnapResult end = snapWall({{-0.43F, 10.0F}}, {2.6F, 10.1F});
	EXPECT_EQ(end.kind, SnapKind::WallEndpoint);
	expectPoint(end.point, 2.5, 10.0);
	EXPECT_EQ(end.lengthSnap, LengthSnapKind::None);
	const SnapResult junction = snapWall({{-0.43F, 12.0F}}, {1.23F, 10.05F});
	EXPECT_EQ(junction.kind, SnapKind::WallSegment);
	expectPoint(junction.point, 1.23, 10.0);
	EXPECT_EQ(junction.lengthSnap, LengthSnapKind::None);
}

TEST(SnapLengths, TargetAtLengthReachesTheTangent) {
	// The line passes exactly 5 m from the start: 5 m reaches it at the foot, a hair less does not.
	const auto tangent = snap_lengths::targetAtLength({0.0, 0.0}, 5.0, {5.0, 6.0}, {0.0, 1.0}, {5.0, 0.2});
	ASSERT_TRUE(tangent.has_value());
	EXPECT_NEAR(tangent->x, 5.0, kExactTolerance);
	EXPECT_NEAR(tangent->y, 0.0, kExactTolerance);
	EXPECT_FALSE(snap_lengths::targetAtLength({0.0, 0.0}, 4.999, {5.0, 6.0}, {0.0, 1.0}, {5.0, 0.2}).has_value());
}

// --- Rounding -------------------------------------------------------------------------

TEST_F(DrawingLevels, IncrementRoundsAngleStep) {
	const Vec2 start{0.0F, 0.0F};
	for (const double along : {4.97, 5.04}) {
		const SnapResult result = snap({start}, polar(start, 31.0, along));
		EXPECT_EQ(result.kind, SnapKind::Angle) << along;
		EXPECT_EQ(result.lengthSnap, LengthSnapKind::Rounded) << along;
		EXPECT_NEAR(distanceBetween(start, result.point), 5.0, kOracleTolerance) << along;
		EXPECT_NEAR(result.guides[0].distanceFromNodeMeters, 5.0F, kOracleTolerance) << along;
	}
	EXPECT_NEAR(distanceBetween(start, snap({start}, polar(start, 31.0, 5.06)).point), 5.1, kOracleTolerance);
}

TEST_F(DrawingLevels, RoundingHalfIncrementRoundsUp) {
	// 5.125 m is exactly 20.5 steps of 0.25 m; under half a step rounds to zero and stays.
	snapping.lengthIncrementMeters = 0.25F;
	DrawingAids aids = allAidsOff();
	aids.angleSteps = true;
	aids.roundLengths = true;
	EXPECT_NEAR(snap({{0.0F, 0.0F}}, {5.125F, 0.0F}, foundationInput(aids)).point.x, 5.25F, kOracleTolerance);
	EXPECT_NEAR(snap({{0.0F, 0.0F}}, {5.12F, 0.0F}, foundationInput(aids)).point.x, 5.0F, kOracleTolerance);
	const SnapResult tiny = snap({{0.0F, 0.0F}}, {0.1F, 0.0F}, foundationInput(aids));
	EXPECT_EQ(tiny.lengthSnap, LengthSnapKind::None);
	expectPoint(tiny.point, 0.1, 0.0);
}

TEST_F(DrawingLevels, RoundedLengthCommitsToTheMillimeter) {
	// The engine rounds in double and returns float meters; the tool's commit quantizes to the
	// millimeter (D1), so an axis-aligned 5 m is 5000 mm and a diagonal one is within half a millimeter.
	const SnapResult straight = snap({{2.0F, 3.0F}}, {7.04F, 3.0F});
	const auto		 straightEnd = geometry::quantize(straight.point);
	EXPECT_EQ(straightEnd.x - 2000, 5000);
	EXPECT_EQ(straightEnd.y, 3000);
	const SnapResult	diagonal = snap({{2.0F, 3.0F}}, polar({2.0F, 3.0F}, 31.0, 4.97));
	const auto			diagonalEnd = geometry::quantize(diagonal.point);
	const double		committed = std::hypot(static_cast<double>(diagonalEnd.x - 2000), static_cast<double>(diagonalEnd.y - 3000));
	EXPECT_NEAR(committed, 5000.0, kHalfMillimeter);
}

TEST_F(DrawingLevels, CommonLengthBeatsRounding) {
	// The levels' and the guide-rounding cases: a building elsewhere lends 3.37 m.
	for (const auto& [corner, cursor] : {std::pair{10.0, Vec2{3.42F, 4.0F}}, std::pair{20.0, Vec2{3.42F, 4.05F}}}) {
		SnapWorld world;
		world.addFoundation({{corner, corner}, {corner + 3.37, corner}, {corner + 3.37, corner + 2.0}, {corner, corner + 2.0}});
		const SnapResult result = SnapEngine(snapping, world.world).snap({{0.0F, 4.0F}}, cursor, foundationInput(everyAidOn()));
		EXPECT_EQ(result.lengthSnap, LengthSnapKind::CommonLength) << corner;
		expectPoint(result.point, 3.37, 4.0);
	}
}

TEST_F(DrawingLevels, HelpersOffRawDirectionRounded) {
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.0F, 0.0F}};
	DrawingAids				aids = allAidsOff();
	aids.roundLengths = true;
	const SnapResult raw = snap(points, {6.2F, 4.04F}, foundationInput(aids));
	EXPECT_EQ(raw.kind, SnapKind::None);
	EXPECT_EQ(raw.lengthSnap, LengthSnapKind::Rounded);
	expectPoint(raw.point, 6.197777, 3.995108);
	EXPECT_NEAR(distanceBetween(points[1], raw.point), 4.0, kOracleTolerance);
	const SnapResult guided = snap(points, {6.2F, 4.04F});
	EXPECT_EQ(guided.kind, SnapKind::GuideLine);
	expectPoint(guided.point, 6.0, 4.0);
}

TEST_F(DrawingLevels, RawCursorLengthStepFollowsConfig) {
	DrawingAids aids = allAidsOff();
	aids.roundLengths = true;
	snapping.commonLengthAfterRawCursor = false;
	const SnapResult raw = snap({{0.0F, 0.0F}, {6.0F, 0.0F}}, {6.2F, 4.04F}, foundationInput(aids));
	EXPECT_EQ(raw.lengthSnap, LengthSnapKind::None);
	expectPoint(raw.point, 6.2, 4.04);
}

// --- Rounding on guides ---------------------------------------------------------------

TEST_F(GuideRounding, NoPointPlacedRoundsGuideDistance) {
	built.addFoundation({{0.0, 10.0}, {3.0, 10.0}, {3.0, 13.0}, {0.0, 13.0}});
	const SnapResult result = snap({}, {-3.67F, 10.05F});
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	expectPoint(result.point, -3.7, 10.0);
	EXPECT_EQ(result.lengthSnap, LengthSnapKind::RoundedAlongGuide);
	EXPECT_NEAR(result.lengthMeters, 3.7F, kOracleTolerance);
	expectPoint(result.lengthFrom, 0.0, 10.0);
}

TEST_F(GuideRounding, GuideThroughOtherPointRoundsGuideDistance) {
	built.addFoundation({{0.0, 10.0}, {3.0, 10.0}, {3.0, 13.0}, {0.0, 13.0}});
	const SnapResult byGuide = snap({{-6.0F, 6.0F}}, {-3.67F, 10.05F});
	EXPECT_EQ(byGuide.kind, SnapKind::GuideLine);
	expectPoint(byGuide.point, -3.7, 10.0);
	EXPECT_EQ(byGuide.lengthSnap, LengthSnapKind::RoundedAlongGuide);

	snapping.lengthRoundingOnGuides = assets::GuideRounding::SegmentLength;
	const SnapResult bySegment = snap({{-6.0F, 6.0F}}, {-3.67F, 10.05F});
	EXPECT_EQ(bySegment.kind, SnapKind::GuideLine);
	expectPoint(bySegment.point, -3.728437, 10.0);
	EXPECT_EQ(bySegment.lengthSnap, LengthSnapKind::Rounded);
	EXPECT_NEAR(distanceBetween({-6.0F, 6.0F}, bySegment.point), 4.6, kOracleTolerance);
}

TEST_F(GuideRounding, SegmentLengthOutOfReachLeavesTheFoot) {
	// The segment rounds down to 5.0 m but the guide is 5.01 m from the start: no point on it is that far.
	snapping.lengthRoundingOnGuides = assets::GuideRounding::SegmentLength;
	built.addFoundation({{-6.0, 5.01}, {-3.0, 5.01}, {-3.0, 8.0}, {-6.0, 8.0}});
	const SnapResult result = snap({{0.0F, 0.0F}}, {0.45F, 5.05F});
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_EQ(result.lengthSnap, LengthSnapKind::None);
	expectPoint(result.point, 0.45, 5.01);
}

TEST_F(GuideRounding, RoundingToZeroLeavesTheFoot) {
	// 0.04 m along the guide through point 2 rounds to nothing: the foot stays, not the node.
	const SnapResult result =
		snap({{0.0F, 0.0F}, {6.0F, 0.0F}, {6.0F, 6.0F}}, {6.03F, 0.04F}, foundationInput(without(everyAidOn(), &DrawingAids::commonLengths)));
	EXPECT_EQ(result.kind, SnapKind::GuideLine);
	EXPECT_EQ(result.lengthSnap, LengthSnapKind::None);
	expectPoint(result.point, 6.0, 0.04);
}

TEST_F(GuideRounding, RoundingLeavesCrossing) {
	// Levels' IncrementLeavesCrossing: 6.03 m stays. Common lengths on, a crossing holds by default;
	// off, rounding never moves it, even where commonLengthBeatsCrossing lets a length try.
	const std::vector<Vec2> points{{0.0F, 0.0F}, {6.03F, 0.0F}, {6.03F, 4.07F}};
	for (const bool beatsCrossing : {false, true}) {
		snapping.commonLengthBeatsCrossing = beatsCrossing;
		const SnapResult result =
			snap(points, {0.15F, 4.0F}, foundationInput(beatsCrossing ? without(everyAidOn(), &DrawingAids::commonLengths) : everyAidOn()));
		EXPECT_EQ(result.kind, SnapKind::GuideCrossing) << beatsCrossing;
		expectPoint(result.point, 0.0, 4.07);
		EXPECT_EQ(result.lengthSnap, LengthSnapKind::None) << beatsCrossing;
	}
}

TEST_F(GuideRounding, RoundingOffLeavesRaw) {
	built.addFoundation({{0.0, 10.0}, {3.0, 10.0}, {3.0, 13.0}, {0.0, 13.0}});
	expectPoint(snap({}, {-3.67F, 10.05F}, foundationInput(without(everyAidOn(), &DrawingAids::roundLengths))).point, -3.67, 10.0);
	// Freeform: no guide and no rounding, the raw cursor.
	expectPoint(snap({}, {-3.67F, 10.05F}, SnapInput{.aids = everyAidOn(), .freeform = true}).point, -3.67, 10.05);
	snapping.lengthIncrementMeters = 0.0F;
	expectPoint(snap({}, {-3.67F, 10.05F}).point, -3.67, 10.0);
	const Vec2 start{0.0F, 20.0F};
	EXPECT_NEAR(distanceBetween(start, snap({start}, polar(start, 31.0, 5.06)).point), 5.06, kOracleTolerance);
}

// --- The prototype's guide cases with lengths on ------------------------------------------

namespace {

	struct LengthsOnCase {
		const char*								 name;
		bool									 wallTool;
		bool									 closest;
		std::vector<Vec2>						 points;
		std::vector<geometry::Ring>				 foundations; // millimeters
		std::vector<std::array<std::int64_t, 4>> walls;		  // millimeters
		Vec2									 cursor;
		SnapKind								 kind;
		double									 x;
		double									 y;
		LengthSnapKind							 lengthSnap;
		double									 lengthMeters;
	};

	// check.mjs's guide cases whose point moves once lengths are on, rerun through the
	// prototype with the inputs as the port sees them (float points, millimeter world).
	// The twelve "worked 3" rows put the cursor 0.5 m off one point of the guide through the
	// previous point, in twelve directions 30 degrees apart, so its foot and the rounded segment
	// length differ row to row.
	const std::vector<LengthsOnCase> kLengthsOnCases = {
		// clang-format off
		{"worked 3: 0.5 m off the corner, perpendicular to the guide", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {3.62901258F, 6.71400023F}, SnapKind::GuideLine, 3.195956, 6.464076, LengthSnapKind::Rounded, 4.0},
		{"worked 3: 0.5 m off at 0 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {3.6960001F, 6.46400023F}, SnapKind::GuideLine, 3.345959, 6.204271, LengthSnapKind::Rounded, 3.7},
		{"worked 3: 0.5 m off at 60 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {3.4460001F, 6.89701271F}, SnapKind::GuideLine, 3.095954, 6.637280, LengthSnapKind::Rounded, 4.2},
		{"worked 3: 0.5 m off at 90 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {3.1960001F, 6.96400023F}, SnapKind::GuideLine, 2.995952, 6.810484, LengthSnapKind::Rounded, 4.4},
		{"worked 3: 0.5 m off at 120 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {2.9460001F, 6.89701271F}, SnapKind::GuideLine, 2.945951, 6.897086, LengthSnapKind::Rounded, 4.5},
		{"worked 3: 0.5 m off at 150 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {2.76298738F, 6.71400023F}, SnapKind::GuideLine, 2.995952, 6.810484, LengthSnapKind::Rounded, 4.4},
		{"worked 3: 0.5 m off at 180 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {2.6960001F, 6.46400023F}, SnapKind::GuideLine, 3.095954, 6.637280, LengthSnapKind::Rounded, 4.2},
		{"worked 3: 0.5 m off at 210 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {2.76298738F, 6.21400023F}, SnapKind::GuideLine, 3.195956, 6.464076, LengthSnapKind::Rounded, 4.0},
		{"worked 3: 0.5 m off at 240 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {2.9460001F, 6.03098726F}, SnapKind::GuideLine, 3.345959, 6.204271, LengthSnapKind::Rounded, 3.7},
		{"worked 3: 0.5 m off at 270 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {3.1960001F, 5.96400023F}, SnapKind::GuideLine, 3.395961, 6.117669, LengthSnapKind::Rounded, 3.6},
		{"worked 3: 0.5 m off at 300 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {3.4460001F, 6.03098726F}, SnapKind::GuideLine, 3.445962, 6.031067, LengthSnapKind::Rounded, 3.5},
		{"worked 3: 0.5 m off at 330 degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {3.62901258F, 6.21400023F}, SnapKind::GuideLine, 3.395961, 6.117669, LengthSnapKind::Rounded, 3.6},
		{"GuideToleranceBoundary: 0.35 m falls through to Angle", false, false, {{0.0F, 0.0F}}, {}, {}, {0.35F, 5.0F}, SnapKind::Angle, 0.0, 5.0, LengthSnapKind::Rounded, 5.0},
		{"NoOriginCloseBeforeThreePoints, at 0.29 m: the first edge's 6 m catches", false, false, {{0.0F, 0.0F}, {6.0F, 0.0F}}, {}, {}, {0.29F, 0.2F}, SnapKind::GuideLine, 0.0, 0.0, LengthSnapKind::CommonLength, 6.0},
		{"GuidePerpendicularOffFirstEdgeAt30Degrees", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {3.3181138F, 6.53179026F}, SnapKind::GuideLine, 3.195956, 6.464076, LengthSnapKind::Rounded, 4.0},
		{"GuidePerpendicularCapturedByAngleFarOut", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {0.9698174F, 12.0630779F}, SnapKind::GuideLine, 0.195890, 11.660191, LengthSnapKind::Rounded, 10.0},
		{"GuideTwoLinesThroughOneNodeDoNotCross", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {5.00869656F, 3.46359181F}, SnapKind::GuideLine, 4.945995, 3.433010, LengthSnapKind::Rounded, 0.5},
		{"GuideCrossingOutsideRadiusIgnored", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {{{1100, 15000}, {3000, 15000}, {3000, 17000}, {1100, 17000}}}, {}, {0.9698174F, 12.0630779F}, SnapKind::GuideLine, 0.195890, 11.660191, LengthSnapKind::Rounded, 10.0},
		{"GuideNearParallelLinesNeverCross", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {{{8000, 9000}, {11411, 11090}, {9843, 13648}, {6433, 11558}}}, {}, {13.7900639F, 8.11293125F}, SnapKind::GuideLine, 13.856305, 8.000176, LengthSnapKind::RoundedAlongGuide, 16.0},
		{"GuideParallelToNearbyRotatedFoundation", false, false, {{0.0F, 0.0F}}, {{{6000, -2000}, {9195, 407}, {7389, 2803}, {4195, 396}}}, {}, {3.88572979F, 3.14660192F}, SnapKind::GuideLine, 3.993088, 3.009193, LengthSnapKind::Rounded, 5.0},
		{"GuideDrawnShapeBeatsCloserWorldAxis", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {5.14600658F, 3.58659148F}, SnapKind::GuideLine, 4.945995, 3.433010, LengthSnapKind::Rounded, 0.5},
		{"GuideDrawnShapeBeatsCloserWorldAxis, closest wins", false, true, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {5.14600658F, 3.58659148F}, SnapKind::GuideLine, 5.196, 3.6, LengthSnapKind::Rounded, 0.6},
		{"GuideParallelNeighboursBothGetLines", false, false, {}, {{{-10000, 3000}, {-6805, 5407}, {-8611, 7803}, {-11805, 5396}}, {{-1500, -500}, {1695, 1907}, {-111, 4303}, {-3305, 1896}}}, {}, {-2.5999999F, 8.67569447F}, SnapKind::GuideLine, -2.572643, 8.597165, LengthSnapKind::RoundedAlongGuide, 5.3},
		{"GuideNoMarkerOnParallel", false, false, {{0.0F, 0.0F}, {5.196F, 3.0F}}, {}, {}, {7.796F, 4.6F}, SnapKind::GuideLine, 7.880773, 4.550100, LengthSnapKind::RoundedAlongGuide, 9.1},
		{"WallGuidePerpendicularToNearbyWallSegment", true, false, {{1.0F, 2.5F}}, {}, {{0, 0, 5638, 2052}}, {-0.498426378F, 6.20873547F}, SnapKind::GuideLine, -0.368043, 6.258784, LengthSnapKind::Rounded, 4.0},
		// clang-format on
	};

} // namespace

TEST(SnapLengths, PrototypeGuideCasesWithLengthsOn) {
	for (const LengthsOnCase& row : kLengthsOnCases) {
		SCOPED_TRACE(row.name);
		SnapWorld built;
		for (const geometry::Ring& ring : row.foundations) {
			built.addFoundation(ring);
		}
		for (const auto& wall : row.walls) {
			built.addWall({{wall[0], wall[1]}, {wall[2], wall[3]}});
		}
		SnappingConfig snapping;
		snapping.priorityRule = row.closest ? assets::GuidePriorityRule::Closest : assets::GuidePriorityRule::SourceFirst;
		const SnapEngine engine(snapping, built.world);
		const SnapResult result = row.wallTool ? engine.snapWall(row.points, row.cursor, wallInput(everyAidOn()))
											   : engine.snap(row.points, row.cursor, foundationInput(everyAidOn()));
		EXPECT_EQ(result.kind, row.kind);
		expectPoint(result.point, row.x, row.y);
		EXPECT_EQ(result.lengthSnap, row.lengthSnap);
		EXPECT_NEAR(result.lengthMeters, row.lengthMeters, kOracleTolerance);
	}
}

// --- The shipped settings ---------------------------------------------------------------

TEST(SnapLengths, ShippedAidsRoundLengthsAndKeepNoCommonLengths) {
	// Round lengths ships on and common lengths off: a segment's length rounds to whatever
	// increment the config sets, and no common length catches however far its catch reaches.
	SnapWorld built;
	built.addFoundation(rectangle(0.0, 0.0, 0.0, 4.0, 3.0));
	SnappingConfig shipped;
	SnappingConfig extreme;
	extreme.lengthIncrementMeters = 1.0F;
	extreme.commonLengthCatchMeters = 5.0F;
	extreme.commonLengthAllBuildings = true;
	extreme.commonLengthBeatsCrossing = true;
	extreme.lengthRoundingOnGuides = assets::GuideRounding::SegmentLength;
	const struct {
		std::vector<Vec2> points;
		Vec2			  cursor;
	} inputs[] = {
		{{{0.0F, -10.0F}}, polar({0.0F, -10.0F}, 31.0, 3.94)}, // near the shack's 4 m, off any step
		{{{0.0F, -10.0F}}, {3.9F, -10.1F}},					   // near the world line through the previous point
		{{}, {-3.67F, 0.05F}},								   // near the line through the shack's corner
		{{{0.0F, -10.0F}, {6.03F, -10.0F}, {6.03F, -5.93F}}, {0.15F, -6.0F}}, // near where two guides would cross
	};
	for (const bool wallTool : {false, true}) {
		const SnapInput input = wallTool ? wallInput(shippedAids()) : foundationInput(shippedAids());
		for (const auto& [points, cursor] : inputs) {
			const SnapResult usual = wallTool ? SnapEngine(shipped, built.world).snapWall(points, cursor, input)
											  : SnapEngine(shipped, built.world).snap(points, cursor, input);
			const SnapResult pushed = wallTool ? SnapEngine(extreme, built.world).snapWall(points, cursor, input)
											   : SnapEngine(extreme, built.world).snap(points, cursor, input);
			EXPECT_EQ(usual.commonLengthCount, 0U);
			EXPECT_EQ(pushed.commonLengthCount, 0U);
			EXPECT_EQ(usual.kind, SnapKind::None);
			EXPECT_EQ(pushed.kind, SnapKind::None);
			// With no point placed there is no segment, and no guide to round along.
			const LengthSnapKind expected = points.empty() ? LengthSnapKind::None : LengthSnapKind::Rounded;
			EXPECT_EQ(usual.lengthSnap, expected);
			EXPECT_EQ(pushed.lengthSnap, expected);
			if (!points.empty()) {
				EXPECT_NEAR(usual.lengthMeters, std::round(usual.lengthMeters * 10.0F) / 10.0F, 1e-4F);
				EXPECT_NEAR(pushed.lengthMeters, std::round(pushed.lengthMeters), 1e-4F);
			}
		}
	}
}
