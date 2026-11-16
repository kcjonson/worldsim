#include "DrawingFeedback.h"

#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <world/camera/WorldCamera.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <tuple>
#include <utility>

namespace {

	using construction::GuideRelation;
	using construction::GuideSource;
	using construction::SnapGuide;
	using construction::SnapKind;
	using world_sim::DrawingFeedbackPlan;
	using world_sim::FeedbackBox;
	using world_sim::FeedbackDot;
	using world_sim::FeedbackLabel;
	using world_sim::FeedbackLayer;
	using world_sim::FeedbackRing;
	using world_sim::FeedbackStroke;

	constexpr float kPixelsPerMeter = 8.0F;
	constexpr float kZoom = 5.0F; // 40 px to the metre, the prototype's scale
	constexpr float kScreenPixelsPerMeter = kPixelsPerMeter * kZoom;
	constexpr int	kViewportWidth = 1000;
	constexpr int	kViewportHeight = 800;
	constexpr float kTickOuterPx = 16.0F; // the marker's ticks reach this far at the default radius
	constexpr float kLineHeightOfSize = 1.25F;

	Foundation::Vec2 fakeSize(const std::string& text, float sizePx) {
		return {0.6F * sizePx * static_cast<float>(text.size()), kLineHeightOfSize * sizePx};
	}

	bool overlaps(const FeedbackBox& a, const FeedbackBox& b) {
		return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
	}

	float distanceToBox(Foundation::Vec2 point, const FeedbackBox& box) {
		const float dx = std::max({box.left - point.x, 0.0F, point.x - box.right});
		const float dy = std::max({box.top - point.y, 0.0F, point.y - box.bottom});
		return std::hypot(dx, dy);
	}

	FeedbackBox boxAround(Foundation::Vec2 center, float halfSize) {
		return {center.x - halfSize, center.y - halfSize, center.x + halfSize, center.y + halfSize};
	}

	Foundation::Vec2 centreOf(const FeedbackBox& box) {
		return {(box.left + box.right) * 0.5F, (box.top + box.bottom) * 0.5F};
	}

	int signOf(float value) {
		return (value > 0.0F) - (value < 0.0F);
	}

	class DrawingFeedbackTest : public ::testing::Test {
	  protected:
		world_sim::ToolKind			  tool = world_sim::ToolKind::Foundation;
		construction::SnapResult	  snap;
		std::vector<Foundation::Vec2> points;
		Foundation::Vec2			  cursor{0.0F, 0.0F};
		world_sim::DrawingSettings	  display;
		assets::PreviewStyle		  style;
		world::WorldCamera			  camera;
		float						  closeRadiusMeters = 0.5F;
		bool						  canClose = false;
		bool						  willClose = false;
		bool						  snappedPointInvalid = false;
		Foundation::Color			  invalidColor{0.9F, 0.2F, 0.2F, 1.0F};

		void SetUp() override { camera.setZoom(kZoom); }

		void addGuide(SnapGuide guide) {
			guide.distanceFromNodeMeters = std::hypot(guide.to.x - guide.from.x, guide.to.y - guide.from.y);
			snap.guides[snap.guideCount++] = guide;
		}

		Foundation::Vec2 screen(Foundation::Vec2 world) const {
			return camera.worldToScreen(world.x, world.y, kViewportWidth, kViewportHeight, kPixelsPerMeter);
		}

		DrawingFeedbackPlan plan() {
			snap.point = cursor;
			const world_sim::DrawingFeedbackInput input{
				.tool = tool,
				.snap = snap,
				.points = points,
				.cursor = cursor,
				.display = display,
				.style = style,
				.camera = camera,
				.viewportWidth = kViewportWidth,
				.viewportHeight = kViewportHeight,
				.pixelsPerMeter = kPixelsPerMeter,
				.closeRadiusMeters = closeRadiusMeters,
				.canClose = canClose,
				.willClose = willClose,
				.snappedPointInvalid = snappedPointInvalid,
				.invalidColor = invalidColor,
			};
			return world_sim::planSnapFeedback(input, fakeSize);
		}

		static std::vector<FeedbackStroke> strokesIn(const DrawingFeedbackPlan& plan, FeedbackLayer layer) {
			std::vector<FeedbackStroke> found;
			std::copy_if(plan.strokes.begin(), plan.strokes.end(), std::back_inserter(found), [&](const FeedbackStroke& s) { return s.layer == layer; });
			return found;
		}

		static std::vector<FeedbackDot> dotsIn(const DrawingFeedbackPlan& plan, FeedbackLayer layer) {
			std::vector<FeedbackDot> found;
			std::copy_if(plan.dots.begin(), plan.dots.end(), std::back_inserter(found), [&](const FeedbackDot& d) { return d.layer == layer; });
			return found;
		}

		static std::vector<FeedbackRing> ringsIn(const DrawingFeedbackPlan& plan, FeedbackLayer layer) {
			std::vector<FeedbackRing> found;
			std::copy_if(plan.rings.begin(), plan.rings.end(), std::back_inserter(found), [&](const FeedbackRing& r) { return r.layer == layer; });
			return found;
		}

		// Which side of the snapped point and of the segment the segment label is on.
		std::tuple<int, int, int> segmentLabelSide(const DrawingFeedbackPlan& result) const {
			const Foundation::Vec2 tip = screen(cursor);
			const Foundation::Vec2 last = screen(points.back());
			const Foundation::Vec2 centre = centreOf(result.labels[0].plate);
			const float			   cross = (tip.x - last.x) * (centre.y - last.y) - (tip.y - last.y) * (centre.x - last.x);
			return {signOf(centre.x - tip.x), signOf(centre.y - tip.y), signOf(cross)};
		}

		// Sweeps the cursor out along `direction` from the one placed point in 1 cm steps
		// and counts how often the segment label changes side.
		int segmentLabelSideChanges(Foundation::Vec2 direction, int steps) {
			points = {{0.0F, 0.0F}};
			std::tuple<int, int, int> previous{};
			int						  changes = 0;
			for (int step = 1; step <= steps; ++step) {
				const float distance = 0.01F * static_cast<float>(step);
				cursor = {direction.x * distance, direction.y * distance};
				const DrawingFeedbackPlan result = plan();
				EXPECT_EQ(result.labels.size(), 1U);
				const auto side = segmentLabelSide(result);
				if (step > 1 && side != previous) {
					++changes;
				}
				previous = side;
			}
			return changes;
		}
	};

	void expectColor(const Foundation::Color& actual, const assets::StyleColor& rgb, float alpha) {
		EXPECT_FLOAT_EQ(actual.r, rgb.r);
		EXPECT_FLOAT_EQ(actual.g, rgb.g);
		EXPECT_FLOAT_EQ(actual.b, rgb.b);
		EXPECT_FLOAT_EQ(actual.a, alpha);
	}

	void expectNear(Foundation::Vec2 actual, Foundation::Vec2 expected) {
		EXPECT_NEAR(actual.x, expected.x, 1e-3F);
		EXPECT_NEAR(actual.y, expected.y, 1e-3F);
	}

	void expectInside(const FeedbackBox& box) {
		EXPECT_GE(box.left, 0.0F);
		EXPECT_GE(box.top, 0.0F);
		EXPECT_LE(box.right, static_cast<float>(kViewportWidth));
		EXPECT_LE(box.bottom, static_cast<float>(kViewportHeight));
	}

} // namespace

TEST_F(DrawingFeedbackTest, WorldGuideIsADottedAlignmentStrokeFromItsNode) {
	cursor = {0.0F, 3.0F};
	snap.kind = SnapKind::GuideLine;
	addGuide({.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes, .relation = GuideRelation::Perpendicular});

	const auto guides = strokesIn(plan(), FeedbackLayer::Alignment);
	ASSERT_EQ(guides.size(), 1U);
	ASSERT_EQ(guides[0].pointCount, 2U);
	expectNear(guides[0].points[0], screen({0.0F, 0.0F}));
	expectNear(guides[0].points[1], screen(cursor));
	expectColor(guides[0].color, style.guideColor, style.guideOpacity);
	EXPECT_FLOAT_EQ(guides[0].width, style.guideWidth);
	ASSERT_TRUE(guides[0].dash.has_value());
	EXPECT_FLOAT_EQ(guides[0].dash->dash, style.guideDashPx);
	EXPECT_FLOAT_EQ(guides[0].dash->gap, style.guideGapPx);
	EXPECT_FLOAT_EQ(guides[0].dash->phase, 0.0F);
	EXPECT_EQ(guides[0].dash->cap, Foundation::DashCap::Round);
	EXPECT_TRUE(strokesIn(plan(), FeedbackLayer::Marker).empty());
}

TEST_F(DrawingFeedbackTest, LineStyleSetsTheDash) {
	cursor = {0.0F, 3.0F};
	addGuide({.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes});

	style.guideLineStyle = assets::GuideLineStyle::Dashed;
	const auto dashed = strokesIn(plan(), FeedbackLayer::Alignment);
	ASSERT_TRUE(dashed[0].dash.has_value());
	EXPECT_EQ(dashed[0].dash->cap, Foundation::DashCap::Butt);

	style.guideLineStyle = assets::GuideLineStyle::Solid;
	EXPECT_FALSE(strokesIn(plan(), FeedbackLayer::Alignment)[0].dash.has_value());
}

TEST_F(DrawingFeedbackTest, CrossingOfTwoSourcesColoursEachGuideAndDotsThePointOverTheMarker) {
	cursor = {3.0F, 4.0F};
	snap.kind = SnapKind::GuideCrossing;
	style.guideColorMode = assets::GuideColorMode::BySource;
	display.guideDistances = true;
	addGuide({.from = {3.0F, 0.0F}, .to = cursor, .source = GuideSource::NearbyStructure});
	addGuide({.from = {0.0F, 4.0F}, .to = cursor, .source = GuideSource::WorldAxes});

	const DrawingFeedbackPlan result = plan();
	const auto				  guides = strokesIn(result, FeedbackLayer::Alignment);
	ASSERT_EQ(guides.size(), 2U);
	expectColor(guides[0].color, style.guideColorNearby, style.guideOpacity);
	expectColor(guides[1].color, style.guideColorWorld, style.guideOpacity);

	const auto crossing = dotsIn(result, FeedbackLayer::Target);
	ASSERT_EQ(crossing.size(), 1U);
	expectNear(crossing[0].center, screen(cursor));
	EXPECT_FLOAT_EQ(crossing[0].radius, style.guideCrossingDotPx);
	EXPECT_LT(crossing[0].radius, style.cursorRadiusPx) << "inside the marker's dot";
	expectColor(crossing[0].fill, style.guideColorNearby, style.markerOpacity);
	EXPECT_GT(FeedbackLayer::Target, FeedbackLayer::Cursor);

	ASSERT_EQ(result.labels.size(), 2U);
	EXPECT_EQ(result.labels[0].text, "4.00 m");
	expectColor(result.labels[0].color, style.guideColorNearby, 1.0F);
	EXPECT_EQ(result.labels[1].text, "3.00 m");
	expectColor(result.labels[1].color, style.guideColorWorld, 1.0F);
}

TEST_F(DrawingFeedbackTest, RightAngleMarkerSitsAtTheNodeAlongTheEdgeAndTheGuide) {
	cursor = {0.0F, 3.0F};
	addGuide({.from = {0.0F, 0.0F}, .to = cursor, .rightAngle = true, .rightAngleEdgeDirection = {1.0F, 0.0F}});

	const auto markers = strokesIn(plan(), FeedbackLayer::Marker);
	ASSERT_EQ(markers.size(), 1U);
	const Foundation::Vec2 node = screen({0.0F, 0.0F});
	const float			   size = style.rightAngleMarkerPx;
	ASSERT_EQ(markers[0].pointCount, 3U);
	expectNear(markers[0].points[0], {node.x + size, node.y});
	expectNear(markers[0].points[1], {node.x + size, node.y + size});
	expectNear(markers[0].points[2], {node.x, node.y + size});
	EXPECT_FALSE(markers[0].dash.has_value());
	EXPECT_FLOAT_EQ(markers[0].width, 1.5F);
	expectColor(markers[0].color, style.guideColor, style.markerOpacity);
}

TEST_F(DrawingFeedbackTest, RightAngleEdgeAlongTheGuideDrawsNoMarker) {
	cursor = {0.0F, 3.0F};
	addGuide({.from = {0.0F, 0.0F}, .to = cursor, .rightAngle = true, .rightAngleEdgeDirection = {0.0F, 1.0F}});
	EXPECT_TRUE(strokesIn(plan(), FeedbackLayer::Marker).empty()) << "parallel";

	snap.guides[0].rightAngleEdgeDirection = {0.0F, -1.0F};
	EXPECT_TRUE(strokesIn(plan(), FeedbackLayer::Marker).empty()) << "antiparallel";
}

TEST_F(DrawingFeedbackTest, GuideDistanceLabelOnlyWhenTheNodeIsNotThePreviousPoint) {
	display.guideDistances = true;
	points = {{0.0F, 0.0F}};
	cursor = {0.0F, 3.0F};
	addGuide({.from = points[0], .to = cursor, .source = GuideSource::WorldAxes, .throughPreviousPoint = true});

	const DrawingFeedbackPlan through = plan();
	ASSERT_EQ(through.labels.size(), 1U); // the segment label alone
	EXPECT_EQ(through.labels[0].text, "3.00 m");

	snap.guides[0].from = {2.0F, 3.0F};
	snap.guides[0].distanceFromNodeMeters = 2.0F;
	snap.guides[0].throughPreviousPoint = false;
	const DrawingFeedbackPlan elsewhere = plan();
	ASSERT_EQ(elsewhere.labels.size(), 2U);
	EXPECT_EQ(elsewhere.labels[1].text, "2.00 m");

	display.guideDistances = false;
	EXPECT_EQ(plan().labels.size(), 1U);
}

TEST_F(DrawingFeedbackTest, GuideShorterThanFortyPixelsHasNoLabel) {
	display.guideDistances = true;
	cursor = {0.0F, 0.9F}; // 36 px
	addGuide({.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	EXPECT_TRUE(plan().labels.empty());

	snap.guides[0].to = cursor = {0.0F, 1.1F}; // 44 px
	EXPECT_EQ(plan().labels.size(), 1U);
}

TEST_F(DrawingFeedbackTest, OverlappingGuideLabelMovesAlongItsGuide) {
	display.guideDistances = true;
	points = {{0.0F, -3.0F}}; // the segment comes down from above, so both labels go below their guides
	cursor = {0.0F, 0.0F};
	addGuide({.from = {-4.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	addGuide({.from = {-4.0F, 0.2F}, .to = cursor, .source = GuideSource::NearbyStructure});

	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 3U); // the segment's, then each guide's
	const FeedbackLabel& first = result.labels[1];
	const FeedbackLabel& second = result.labels[2];
	EXPECT_FALSE(overlaps(first.plate, second.plate));
	// The first guide's label keeps its place centred below its guide's middle.
	const Foundation::Vec2 middle = screen({-2.0F, 0.0F});
	EXPECT_NEAR(first.position.x + first.width * 0.5F, middle.x, 1e-3F);
	EXPECT_NEAR(first.position.y, middle.y + 10.0F, 1e-3F);
	// The second has moved along its guide by at least its own width.
	const float secondMiddleX = screen({-2.0F, 0.1F}).x;
	EXPECT_GT(std::abs(second.position.x + second.width * 0.5F - secondMiddleX), second.width);
}

TEST_F(DrawingFeedbackTest, GuideLabelCoveredAfterEveryShiftIsDropped) {
	display.guideDistances = true;
	points = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}};
	canClose = true;
	willClose = true;
	closeRadiusMeters = 20.0F; // a 400 px halo round the first point, whose box covers every place the label can go
	cursor = {-1.0F, 1.0F};
	addGuide({.from = {-3.0F, 1.0F}, .to = cursor, .source = GuideSource::WorldAxes});

	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 1U);
	EXPECT_NE(result.labels[0].text, "2.00 m");
}

TEST_F(DrawingFeedbackTest, GuideLabelsKeepClearOfTheMarkerAndTheHalo) {
	display.guideDistances = true;
	cursor = {0.0F, 0.0F};
	addGuide({.from = {0.0F, -1.2F}, .to = cursor, .source = GuideSource::WorldAxes}); // 48 px, ending at the marker

	const DrawingFeedbackPlan alone = plan();
	ASSERT_EQ(alone.labels.size(), 1U);
	EXPECT_FALSE(overlaps(alone.labels[0].plate, boxAround(screen(cursor), kTickOuterPx)));

	points = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}};
	canClose = true;
	willClose = true;
	snap.guides[0] = {};
	snap.guideCount = 0;
	addGuide({.from = {-1.5F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes}); // 60 px, into the halo
	const DrawingFeedbackPlan closing = plan();
	const auto				  halo = ringsIn(closing, FeedbackLayer::Marker);
	ASSERT_EQ(halo.size(), 1U);
	ASSERT_EQ(closing.labels.size(), 2U);
	EXPECT_FALSE(overlaps(closing.labels[1].plate, boxAround(halo[0].center, halo[0].radius)));
}

TEST_F(DrawingFeedbackTest, DuplicateGuidesDrawOneStrokeAndOneLabel) {
	display.guideDistances = true;
	cursor = {0.0F, 3.0F};
	addGuide({.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	addGuide({.from = {0.01F, 0.0F}, .to = cursor, .source = GuideSource::NearbyStructure}); // under half a pixel away

	const DrawingFeedbackPlan result = plan();
	EXPECT_EQ(strokesIn(result, FeedbackLayer::Alignment).size(), 1U);
	EXPECT_EQ(result.labels.size(), 1U);
}

TEST_F(DrawingFeedbackTest, SegmentLabelReadsTheLengthClearOfThePointer) {
	points = {{0.0F, 0.0F}};
	cursor = {1.234F, 0.0F}; // about 49 px: too short for a label at its middle
	const DrawingFeedbackPlan shortSegment = plan();
	ASSERT_EQ(shortSegment.labels.size(), 1U);
	EXPECT_EQ(shortSegment.labels[0].text, "1.23 m");
	const Foundation::Vec2 tip = screen(cursor);
	const FeedbackBox	   shortBox = shortSegment.labels[0].plate;
	EXPECT_GE(distanceToBox(tip, shortBox), 12.0F);
	EXPECT_FALSE(shortBox.right > tip.x && shortBox.bottom > tip.y) << "the label sits under the pointer";
	EXPECT_GT(shortBox.left, tip.x) << "the segment arrives from the left, so the label goes right";

	style.guideDistanceDecimals = 1;
	EXPECT_EQ(plan().labels[0].text, "1.2 m");

	style.guideDistanceDecimals = 2;
	cursor = {6.0F, 0.0F}; // 240 px: the label sits beside the middle
	const FeedbackLabel longLabel = plan().labels[0];
	EXPECT_EQ(longLabel.text, "6.00 m");
	EXPECT_GE(distanceToBox(screen(cursor), longLabel.plate), 40.0F);
	const Foundation::Vec2 middle = screen({3.0F, 0.0F});
	EXPECT_NEAR(longLabel.position.y + longLabel.height, middle.y - 10.0F, 1e-3F) << "above the segment";
	EXPECT_NEAR(longLabel.position.x + longLabel.width * 0.5F, middle.x, 1e-3F) << "centred on the middle";
}

TEST_F(DrawingFeedbackTest, SegmentLabelChangesSideAtMostOnceAlongARay) {
	const float diagonal = std::numbers::sqrt2_v<float> * 0.5F;
	for (const auto& [name, direction] : {
			 std::pair{"45 degrees down right", Foundation::Vec2{diagonal, diagonal}},
			 std::pair{"leftward", Foundation::Vec2{-1.0F, 0.0F}},
			 std::pair{"upward", Foundation::Vec2{0.0F, -1.0F}},
		 }) {
		SCOPED_TRACE(name);
		EXPECT_LE(segmentLabelSideChanges(direction, 800), 1);
		// By 8 m the label has moved to the segment's middle, behind the snapped point.
		const Foundation::Vec2 tip = screen(cursor);
		const Foundation::Vec2 centre = centreOf(plan().labels[0].plate);
		EXPECT_LT((centre.x - tip.x) * direction.x + (centre.y - tip.y) * direction.y, 0.0F);
	}
}

TEST_F(DrawingFeedbackTest, SegmentLabelNeverSitsLowerRightOfThePointerFromAnyDiagonal) {
	const float diagonal = std::numbers::sqrt2_v<float> * 0.5F;
	for (const Foundation::Vec2 direction : {Foundation::Vec2{diagonal, diagonal}, Foundation::Vec2{-diagonal, diagonal},
											 Foundation::Vec2{diagonal, -diagonal}, Foundation::Vec2{-diagonal, -diagonal}}) {
		points = {{0.0F, 0.0F}};
		cursor = direction; // 40 px: the label sits at the snapped end
		const DrawingFeedbackPlan result = plan();
		ASSERT_EQ(result.labels.size(), 1U);
		const Foundation::Vec2 tip = screen(cursor);
		const Foundation::Vec2 centre = centreOf(result.labels[0].plate);
		EXPECT_FALSE(centre.x > tip.x && centre.y > tip.y) << "arriving along " << direction.x << ", " << direction.y;
	}
}

TEST_F(DrawingFeedbackTest, LabelsStayInsideTheViewport) {
	display.guideDistances = true;

	points = {{11.5F, 0.0F}};
	cursor = {12.25F, 0.0F}; // 10 px from the right edge, arriving from the left
	const DrawingFeedbackPlan right = plan();
	ASSERT_EQ(right.labels.size(), 1U);
	expectInside(right.labels[0].plate);
	EXPECT_LT(right.labels[0].plate.right, screen(cursor).x) << "moved to the other side of the point, not squashed against the edge";

	points = {{-3.0F, 9.0F}};
	cursor = {0.0F, 9.75F}; // 10 px from the bottom edge
	addGuide({.from = {-3.0F, 9.75F}, .to = cursor, .source = GuideSource::WorldAxes});
	const DrawingFeedbackPlan bottom = plan();
	ASSERT_EQ(bottom.labels.size(), 2U);
	for (const FeedbackLabel& label : bottom.labels) {
		SCOPED_TRACE(label.text);
		expectInside(label.plate);
	}
}

TEST_F(DrawingFeedbackTest, LabelsSitOnPlatesAsTallAsTheFontsLineHeight) {
	points = {{0.0F, 0.0F}};
	cursor = {1.0F, 0.0F};
	const FeedbackLabel label = plan().labels[0];
	EXPECT_FLOAT_EQ(label.height, kLineHeightOfSize * label.sizePx);
	EXPECT_FLOAT_EQ(label.width, fakeSize(label.text, label.sizePx).x);
	EXPECT_FLOAT_EQ(label.plate.left, label.position.x - 3.0F);
	EXPECT_FLOAT_EQ(label.plate.top, label.position.y - 3.0F);
	EXPECT_FLOAT_EQ(label.plate.right, label.position.x + label.width + 3.0F);
	EXPECT_FLOAT_EQ(label.plate.bottom, label.position.y + label.height + 3.0F);
}

TEST_F(DrawingFeedbackTest, GuideThroughThePreviousPointDrawsNoStrokeButKeepsItsMarker) {
	display.guideDistances = true;
	points = {{0.0F, 0.0F}, {4.0F, 0.0F}};
	cursor = {4.0F, 3.0F};
	snap.kind = SnapKind::GuideLine;
	addGuide(
		{.from = points[1],
		 .to = cursor,
		 .source = GuideSource::DrawnShape,
		 .relation = GuideRelation::Perpendicular,
		 .rightAngle = true,
		 .rightAngleEdgeDirection = {-1.0F, 0.0F},
		 .throughPreviousPoint = true}
	);

	DrawingFeedbackPlan result = plan();
	EXPECT_TRUE(strokesIn(result, FeedbackLayer::Alignment).empty());
	EXPECT_EQ(strokesIn(result, FeedbackLayer::Marker).size(), 1U);

	snappedPointInvalid = true; // an invalid segment draws no teal beads over its red line either
	result = plan();
	EXPECT_TRUE(strokesIn(result, FeedbackLayer::Alignment).empty());
}

TEST_F(DrawingFeedbackTest, AngleStepRayDrawsNothingBeyondTheSegment) {
	display.guideDistances = true;
	points = {{0.0F, 0.0F}, {4.0F, 0.0F}};
	cursor = {4.0F, 3.0F};
	snap.kind = SnapKind::Angle;
	addGuide({.from = points[1], .to = cursor, .source = GuideSource::DrawnShape, .relation = GuideRelation::AngleStep, .throughPreviousPoint = true});

	const DrawingFeedbackPlan result = plan();
	EXPECT_TRUE(strokesIn(result, FeedbackLayer::Alignment).empty());
	EXPECT_TRUE(strokesIn(result, FeedbackLayer::Marker).empty());
	ASSERT_EQ(result.labels.size(), 1U);
	EXPECT_EQ(result.labels[0].text, "3.00 m");
}

TEST_F(DrawingFeedbackTest, CloseRingWhileTheShapeCanCloseAndHaloOutsideTheTicksWhenTheClickCloses) {
	points = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}};
	cursor = {1.0F, 2.0F};
	EXPECT_TRUE(plan().rings.empty());

	canClose = true;
	const DrawingFeedbackPlan open = plan();
	ASSERT_EQ(open.rings.size(), 1U);
	EXPECT_EQ(open.rings[0].layer, FeedbackLayer::Alignment);
	expectNear(open.rings[0].center, screen(points[0]));
	EXPECT_FLOAT_EQ(open.rings[0].radius, closeRadiusMeters * kScreenPixelsPerMeter);
	EXPECT_FLOAT_EQ(open.rings[0].width, 1.5F);
	EXPECT_TRUE(open.rings[0].dash.has_value());
	expectColor(open.rings[0].color, style.originHaloColor, style.originHaloColor.a);

	willClose = true;
	cursor = points[0];
	const DrawingFeedbackPlan closing = plan();
	ASSERT_EQ(closing.rings.size(), 2U);
	const auto& halo = closing.rings[1];
	EXPECT_EQ(halo.layer, FeedbackLayer::Marker);
	EXPECT_FLOAT_EQ(halo.radius, kTickOuterPx + 6.0F) << "the 20 px close radius halves to 10 px, inside the ticks: the floor holds";
	EXPECT_FLOAT_EQ(halo.width, 3.0F);
	EXPECT_FALSE(halo.dash.has_value());
	expectColor(halo.color, style.guideColor, style.markerOpacity);

	closeRadiusMeters = 2.0F; // 80 px on screen: the halo is half of it
	EXPECT_FLOAT_EQ(plan().rings[1].radius, 40.0F);

	style.guideColorMode = assets::GuideColorMode::BySource;
	expectColor(plan().rings[1].color, style.guideColorShape, style.markerOpacity);
}

TEST_F(DrawingFeedbackTest, NoPointsPlacedDrawsOnlyTheSnappedPointMarker) {
	cursor = {1.0F, 1.0F};
	addGuide({.from = cursor, .to = cursor, .source = GuideSource::WorldAxes}); // zero length

	const DrawingFeedbackPlan result = plan();
	EXPECT_TRUE(result.labels.empty());
	EXPECT_TRUE(result.rings.empty());
	EXPECT_TRUE(strokesIn(result, FeedbackLayer::Alignment).empty());

	ASSERT_EQ(result.dots.size(), 1U);
	const Foundation::Vec2 at = screen(cursor);
	expectNear(result.dots[0].center, at);
	EXPECT_FLOAT_EQ(result.dots[0].radius, 6.0F);
	EXPECT_FLOAT_EQ(result.dots[0].outlineWidth, 2.5F);
	EXPECT_EQ(result.dots[0].layer, FeedbackLayer::Cursor);
	EXPECT_FLOAT_EQ(result.dots[0].fill.g, 1.0F) << "white";

	const auto outlines = strokesIn(result, FeedbackLayer::CursorOutline);
	const auto ticks = strokesIn(result, FeedbackLayer::Cursor);
	ASSERT_EQ(outlines.size(), 4U);
	ASSERT_EQ(ticks.size(), 4U);
	EXPECT_FLOAT_EQ(outlines[0].width, 6.0F);
	EXPECT_FLOAT_EQ(ticks[0].width, 2.0F);
	expectNear(ticks[0].points[0], {at.x - 11.0F, at.y});
	expectNear(ticks[0].points[1], {at.x - 16.0F, at.y});

	style.cursorRadiusPx = 2.0F;
	EXPECT_FLOAT_EQ(plan().dots[0].radius, 4.0F);
}

TEST_F(DrawingFeedbackTest, OffBuildableGroundTheMarkerDotIsTheInvalidColour) {
	points = {{0.0F, 0.0F}};
	cursor = {2.0F, 1.0F};
	snappedPointInvalid = true;

	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.dots.size(), 1U) << "one marker, no separate red dot";
	EXPECT_EQ(result.dots[0].layer, FeedbackLayer::Cursor);
	EXPECT_FLOAT_EQ(result.dots[0].fill.r, invalidColor.r);
	EXPECT_FLOAT_EQ(result.dots[0].fill.g, invalidColor.g);
	EXPECT_FLOAT_EQ(result.dots[0].fill.b, invalidColor.b);
	EXPECT_FLOAT_EQ(result.dots[0].fill.a, invalidColor.a);
	EXPECT_FLOAT_EQ(result.dots[0].outlineWidth, 2.5F);
	EXPECT_EQ(strokesIn(result, FeedbackLayer::Cursor).size(), 4U);
	EXPECT_EQ(strokesIn(result, FeedbackLayer::CursorOutline).size(), 4U);
}

TEST_F(DrawingFeedbackTest, WallSnapTargetRingSitsOverAndOutsideTheMarker) {
	tool = world_sim::ToolKind::Wall;
	cursor = {1.0F, 1.0F};

	snap.kind = SnapKind::WallEndpoint;
	const auto vertex = ringsIn(plan(), FeedbackLayer::Target);
	ASSERT_EQ(vertex.size(), 1U);
	expectNear(vertex[0].center, screen(cursor));
	EXPECT_GE(vertex[0].radius - vertex[0].width * 0.5F, kTickOuterPx + 2.0F);
	EXPECT_FLOAT_EQ(vertex[0].width, 2.0F);
	EXPECT_FALSE(vertex[0].dash.has_value());
	expectColor(vertex[0].color, style.snapVertexColor, style.snapVertexColor.a);

	snap.kind = SnapKind::Vertex;
	expectColor(ringsIn(plan(), FeedbackLayer::Target)[0].color, style.snapVertexColor, style.snapVertexColor.a);

	snap.kind = SnapKind::WallSegment;
	expectColor(ringsIn(plan(), FeedbackLayer::Target)[0].color, style.snapEdgeColor, style.snapEdgeColor.a);

	snap.kind = SnapKind::Edge;
	expectColor(ringsIn(plan(), FeedbackLayer::Target)[0].color, style.snapEdgeColor, style.snapEdgeColor.a);

	snap.kind = SnapKind::GuideLine;
	EXPECT_TRUE(ringsIn(plan(), FeedbackLayer::Target).empty());

	tool = world_sim::ToolKind::Foundation;
	snap.kind = SnapKind::Vertex;
	EXPECT_TRUE(ringsIn(plan(), FeedbackLayer::Target).empty()) << "the foundation tools draw no snap-target ring";
}

TEST_F(DrawingFeedbackTest, NonFiniteInputDrawsNothingForThatElement) {
	display.guideDistances = true;
	points = {{0.0F, 0.0F}};
	cursor = {2.0F, 0.0F};
	const float nan = std::numeric_limits<float>::quiet_NaN();
	addGuide({.from = {nan, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes});

	const DrawingFeedbackPlan badGuide = plan();
	EXPECT_TRUE(strokesIn(badGuide, FeedbackLayer::Alignment).empty());
	ASSERT_EQ(badGuide.labels.size(), 1U);
	EXPECT_EQ(badGuide.labels[0].text, "2.00 m");

	cursor = {nan, nan};
	const DrawingFeedbackPlan badPoint = plan();
	EXPECT_TRUE(badPoint.labels.empty());
	EXPECT_TRUE(badPoint.dots.empty());
	EXPECT_TRUE(strokesIn(badPoint, FeedbackLayer::Cursor).empty());
}

TEST_F(DrawingFeedbackTest, DistanceThatRoundsToZeroPrintsUnsigned) {
	display.guideDistances = true;
	cursor = {0.0F, 3.0F};
	addGuide({.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	snap.guides[0].distanceFromNodeMeters = -0.001F;
	ASSERT_EQ(plan().labels.size(), 1U);
	EXPECT_EQ(plan().labels[0].text, "0.00 m");
}
