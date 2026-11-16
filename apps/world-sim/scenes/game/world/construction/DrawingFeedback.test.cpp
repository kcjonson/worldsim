#include "DrawingFeedback.h"

#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <world/camera/WorldCamera.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <tuple>
#include <utility>
#include <vector>

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
	constexpr float kZoom = 5.0F; // 40 px to the meter, the prototype's scale
	constexpr float kScreenPixelsPerMeter = kPixelsPerMeter * kZoom;
	constexpr int	kViewportWidth = 1000;
	constexpr int	kViewportHeight = 800;
	constexpr float kTickOuterPx = 16.0F; // the marker's ticks reach this far at the default radius
	constexpr float kLineHeightOfSize = 1.25F;
	// Plates are float sums of the camera's transform: one against a screen edge can land a
	// hair past it (1000.00006).
	constexpr float kViewportTolerancePx = 1e-3F;

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

	Foundation::Vec2 centerOf(const FeedbackBox& box) {
		return {(box.left + box.right) * 0.5F, (box.top + box.bottom) * 0.5F};
	}

	// The snapped-point marker at the default radius: its two tick bars and its dot, each
	// with its outline.
	std::array<FeedbackBox, 3> markerBoxes(Foundation::Vec2 snappedPoint) {
		constexpr float kReach = kTickOuterPx + 3.0F;
		constexpr float kHalfWidth = 3.0F;
		return {{
			{snappedPoint.x - kReach, snappedPoint.y - kHalfWidth, snappedPoint.x + kReach, snappedPoint.y + kHalfWidth},
			{snappedPoint.x - kHalfWidth, snappedPoint.y - kReach, snappedPoint.x + kHalfWidth, snappedPoint.y + kReach},
			boxAround(snappedPoint, 6.0F + 1.25F),
		}};
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

		// The world point at `screenPoint`, at the camera's current zoom.
		Foundation::Vec2 worldAt(Foundation::Vec2 screenPoint) const {
			const Foundation::Vec2 origin = screen({0.0F, 0.0F});
			const float			   perMeterX = screen({1.0F, 0.0F}).x - origin.x;
			const float			   perMeterY = screen({0.0F, 1.0F}).y - origin.y;
			return {(screenPoint.x - origin.x) / perMeterX, (screenPoint.y - origin.y) / perMeterY};
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
			const Foundation::Vec2 center = centerOf(result.labels[0].plate);
			const float			   cross = (tip.x - last.x) * (center.y - last.y) - (tip.y - last.y) * (center.x - last.x);
			return {signOf(center.x - tip.x), signOf(center.y - tip.y), signOf(cross)};
		}

		struct SideSweep {
			int changes = 0;
			int returns = 0; // changes back to a side the label left earlier: flicker
		};

		// Sweeps the cursor out along `direction` from `origin`, the one placed point, in
		// steps of `stepMeters` past `startMeters`, and counts how often the segment label
		// changes side while the snapped point is on screen. Every plate stays inside the
		// screen and off the marker.
		SideSweep
		sweepSegmentLabel(Foundation::Vec2 origin, Foundation::Vec2 direction, int steps, float stepMeters = 0.01F, float startMeters = 0.0F) {
			points = {origin};
			std::vector<std::tuple<int, int, int>> sides;
			SideSweep							   sweep;
			for (int step = 1; step <= steps; ++step) {
				const float distance = startMeters + stepMeters * static_cast<float>(step);
				cursor = {origin.x + direction.x * distance, origin.y + direction.y * distance};
				const Foundation::Vec2 tip = screen(cursor);
				if (tip.x < 0.0F || tip.y < 0.0F || tip.x > static_cast<float>(kViewportWidth) || tip.y > static_cast<float>(kViewportHeight)) {
					continue;
				}
				const DrawingFeedbackPlan result = plan();
				EXPECT_EQ(result.labels.size(), 1U);
				const FeedbackBox& plate = result.labels[0].plate;
				expectInside(plate);
				for (const FeedbackBox& marker : markerBoxes(tip)) {
					EXPECT_FALSE(overlaps(plate, marker)) << "on the marker with the snapped point at " << tip.x << ", " << tip.y;
				}
				const auto side = segmentLabelSide(result);
				if (!sides.empty() && side != sides.back()) {
					++sweep.changes;
					if (std::find(sides.begin(), sides.end(), side) != sides.end()) {
						++sweep.returns;
					}
				}
				sides.push_back(side);
			}
			return sweep;
		}

		static void expectInside(const FeedbackBox& box) {
			EXPECT_GE(box.left, -kViewportTolerancePx);
			EXPECT_GE(box.top, -kViewportTolerancePx);
			EXPECT_LE(box.right, static_cast<float>(kViewportWidth) + kViewportTolerancePx);
			EXPECT_LE(box.bottom, static_cast<float>(kViewportHeight) + kViewportTolerancePx);
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
	// The first guide's label keeps its place centered below its guide's middle.
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

TEST_F(DrawingFeedbackTest, DuplicateGuidesKeepEachRightAngleMarker) {
	cursor = {0.0F, 3.0F};
	const SnapGuide plain{.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes};
	const SnapGuide square{.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::DrawnShape, .rightAngle = true, .rightAngleEdgeDirection = {1.0F, 0.0F}};
	SnapGuide		otherEdge = square;
	otherEdge.rightAngleEdgeDirection = {-1.0F, 0.0F};

	for (const auto& [name, first, second, markers] : {
			 std::tuple{"marker on the second", plain, square, 1U},
			 std::tuple{"marker on the first", square, plain, 1U},
			 std::tuple{"the same edge twice", square, square, 1U},
			 std::tuple{"two edges", square, otherEdge, 2U},
		 }) {
		SCOPED_TRACE(name);
		snap.guideCount = 0;
		addGuide(first);
		addGuide(second);
		const DrawingFeedbackPlan result = plan();
		EXPECT_EQ(strokesIn(result, FeedbackLayer::Alignment).size(), 1U);
		EXPECT_EQ(strokesIn(result, FeedbackLayer::Marker).size(), markers);
	}
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
	EXPECT_NEAR(longLabel.position.x + longLabel.width * 0.5F, middle.x, 1e-3F) << "centered on the middle";
}

TEST_F(DrawingFeedbackTest, SegmentLabelChangesSideAtMostOnceAlongARay) {
	const float diagonal = std::numbers::sqrt2_v<float> * 0.5F;
	for (const auto& [name, direction] : {
			 std::pair{"45 degrees down right", Foundation::Vec2{diagonal, diagonal}},
			 std::pair{"leftward", Foundation::Vec2{-1.0F, 0.0F}},
			 std::pair{"upward", Foundation::Vec2{0.0F, -1.0F}},
		 }) {
		SCOPED_TRACE(name);
		EXPECT_LE(sweepSegmentLabel({0.0F, 0.0F}, direction, 800).changes, 1);
		// By 8 m the label has moved to the segment's middle, behind the snapped point.
		const Foundation::Vec2 tip = screen(cursor);
		const Foundation::Vec2 center = centerOf(plan().labels[0].plate);
		EXPECT_LT((center.x - tip.x) * direction.x + (center.y - tip.y) * direction.y, 0.0F);
	}
}

// The label grows a digit at 10 m and at 100 m. At these zooms each growth lands just past
// where the label at its own width would first fit at the middle: judged at its own width
// it would go to the middle, back to the end, and to the middle again.
TEST_F(DrawingFeedbackTest, SegmentLabelReserveHoldsPastTenAndAHundredMeters) {
	for (const auto& [name, zoom, stepMeters, steps] : {
			 std::tuple{"past 10 m", 1.65F, 0.01F, 1200},
			 std::tuple{"past 100 m", 0.175F, 0.05F, 2200},
		 }) {
		SCOPED_TRACE(name);
		camera.setZoom(zoom);
		const SideSweep sweep = sweepSegmentLabel({0.0F, 0.0F}, {-1.0F, 0.0F}, steps, stepMeters);
		EXPECT_EQ(sweep.changes, 1);
		EXPECT_EQ(sweep.returns, 0);
		EXPECT_GT(centerOf(plan().labels[0].plate).x, screen(cursor).x) << "at the middle by the end";
	}
}

// Near a screen edge the middle placement's fit is judged at the reserved width too, so the
// label growing a digit can't send it to the other side and back. The first ray is the
// reader's case: at 10.00 m the plate's right edge passed 1000 and it flipped until 10.10 m.
TEST_F(DrawingFeedbackTest, SegmentLabelKeepsItsSideAsItGainsADigitNearAScreenEdge) {
	for (const auto& [name, zoom, startMeters] : {
			 std::tuple{"past 10 m", 1.65F, 9.9F},
			 std::tuple{"past 100 m", 0.175F, 99.9F},
		 }) {
		camera.setZoom(zoom);
		for (const auto& [edge, firstPoint] : {
				 std::pair{"bottom right", Foundation::Vec2{960.0F, 760.0F}},
				 std::pair{"right", Foundation::Vec2{960.0F, 400.0F}},
				 std::pair{"bottom", Foundation::Vec2{500.0F, 760.0F}},
			 }) {
			for (int degrees = 250; degrees < 250 + 360; degrees += 5) {
				SCOPED_TRACE(::testing::Message() << name << ", from the " << edge << " edge at " << degrees % 360 << " degrees");
				const float radians = static_cast<float>(degrees) * std::numbers::pi_v<float> / 180.0F;
				const SideSweep sweep = sweepSegmentLabel(worldAt(firstPoint), {std::cos(radians), std::sin(radians)}, 30, 0.01F, startMeters);
				EXPECT_EQ(sweep.returns, 0);
			}
		}
	}
}

// A segment from off the screen: its middle label is moved inside, which near the snapped
// point would put it on the marker's ticks. It takes the end until the moved plate clears
// the pointer, one change of side and no flicker.
TEST_F(DrawingFeedbackTest, SegmentLabelMovedInsideTheScreenKeepsOffTheMarker) {
	for (const auto& [name, origin, direction] : {
			 std::tuple{"from past the right edge", Foundation::Vec2{17.5F, 0.0F}, Foundation::Vec2{-1.0F, 0.0F}},
			 std::tuple{"from past the bottom edge", Foundation::Vec2{0.0F, 17.5F}, Foundation::Vec2{0.0F, -1.0F}},
		 }) {
		SCOPED_TRACE(name);
		const SideSweep sweep = sweepSegmentLabel(origin, direction, 1500);
		EXPECT_LE(sweep.changes, 2);
		EXPECT_EQ(sweep.returns, 0);
	}

	points = {{17.5F, 0.0F}};
	cursor = {11.55F, 0.0F}; // 962 px across a 1000 px screen
	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 1U);
	for (const FeedbackBox& marker : markerBoxes(screen(cursor))) {
		EXPECT_FALSE(overlaps(result.labels[0].plate, marker));
	}
	expectInside(result.labels[0].plate);
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
		const Foundation::Vec2 center = centerOf(result.labels[0].plate);
		EXPECT_FALSE(center.x > tip.x && center.y > tip.y) << "arriving along " << direction.x << ", " << direction.y;
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

// The world-axis guide from an older corner wins the tie and runs on through the previous
// point to the snapped point: past the previous point it is the segment.
TEST_F(DrawingFeedbackTest, GuideFromAnOlderNodeAlongTheSegmentDrawsOnlyUpToThePreviousPoint) {
	display.guideDistances = true;
	points = {{0.0F, -4.0F}, {0.0F, 1.0F}};
	cursor = {0.0F, 6.0F};
	snap.kind = SnapKind::GuideLine;
	addGuide({.from = points[0], .to = cursor, .source = GuideSource::WorldAxes, .relation = GuideRelation::Perpendicular});

	const DrawingFeedbackPlan result = plan();
	const auto				  guides = strokesIn(result, FeedbackLayer::Alignment);
	ASSERT_EQ(guides.size(), 1U);
	expectNear(guides[0].points[0], screen(points[0]));
	expectNear(guides[0].points[1], screen(points[1]));
	ASSERT_EQ(result.labels.size(), 2U);
	EXPECT_EQ(result.labels[0].text, "5.00 m");
	EXPECT_EQ(result.labels[1].text, "10.00 m");
	EXPECT_FALSE(overlaps(result.labels[0].plate, result.labels[1].plate));

	points = {{0.0F, -4.0F}, {0.0F, -6.0F}}; // the segment doubles back over the node: all of the guide is under it
	EXPECT_TRUE(strokesIn(plan(), FeedbackLayer::Alignment).empty());
}

// A guide's distance label draws only when its stroke draws, beside the middle of what is
// drawn, and reads the guide's whole distance from its node.
TEST_F(DrawingFeedbackTest, GuideDistanceLabelFollowsTheVisibleStroke) {
	display.guideDistances = true;
	cursor = {0.0F, 6.0F};
	addGuide({.from = {0.0F, -4.0F}, .to = cursor, .source = GuideSource::WorldAxes});

	points = {{5.0F, -5.0F}, {-3.0F, 6.0F}}; // off the guide: the whole stroke
	const DrawingFeedbackPlan whole = plan();
	ASSERT_EQ(whole.labels.size(), 2U);
	EXPECT_EQ(whole.labels[1].text, "10.00 m");
	EXPECT_NEAR(centerOf(whole.labels[1].plate).y, screen({0.0F, 1.0F}).y, 1e-3F);

	points = {{0.0F, -4.0F}, {0.0F, -2.0F}}; // along the guide: dotted from -4 m to -2 m
	const DrawingFeedbackPlan shortened = plan();
	ASSERT_EQ(shortened.labels.size(), 2U);
	EXPECT_EQ(shortened.labels[1].text, "10.00 m");
	EXPECT_NEAR(centerOf(shortened.labels[1].plate).y, screen({0.0F, -3.0F}).y, 1e-3F);

	points = {{0.0F, -4.0F}, {0.0F, -6.0F}}; // the previous point behind the node: no stroke
	const DrawingFeedbackPlan hidden = plan();
	EXPECT_TRUE(strokesIn(hidden, FeedbackLayer::Alignment).empty());
	ASSERT_EQ(hidden.labels.size(), 1U); // the segment label alone
	EXPECT_EQ(hidden.labels[0].text, "12.00 m");
}

TEST_F(DrawingFeedbackTest, GuideOffTheSegmentKeepsItsWholeStroke) {
	cursor = {0.0F, 6.0F};
	for (const auto& [name, previous, from] : {
			 std::tuple{"at a right angle", Foundation::Vec2{-3.0F, 6.0F}, Foundation::Vec2{0.0F, -4.0F}},
			 std::tuple{"two pixels off the previous point", Foundation::Vec2{0.05F, 1.0F}, Foundation::Vec2{0.0F, -4.0F}},
			 std::tuple{"collinear, the previous point past the snapped point", Foundation::Vec2{0.0F, 8.0F}, Foundation::Vec2{0.0F, -4.0F}},
		 }) {
		SCOPED_TRACE(name);
		points = {{5.0F, -5.0F}, previous};
		snap.guideCount = 0;
		addGuide({.from = from, .to = cursor, .source = GuideSource::WorldAxes});
		const auto guides = strokesIn(plan(), FeedbackLayer::Alignment);
		ASSERT_EQ(guides.size(), 1U);
		expectNear(guides[0].points[0], screen(from));
		expectNear(guides[0].points[1], screen(cursor));
	}
}

// A short closing segment's label sits at the snapped end, which is the first point: it
// keeps off the halo round it.
TEST_F(DrawingFeedbackTest, SegmentLabelKeepsOffTheCloseHalo) {
	points = {{0.0F, 0.0F}, {1.2F, 0.0F}, {1.2F, 0.6F}};
	canClose = true;
	willClose = true;
	cursor = points[0];
	snap.kind = SnapKind::Origin;

	const DrawingFeedbackPlan result = plan();
	const auto				  halo = ringsIn(result, FeedbackLayer::Marker);
	ASSERT_EQ(halo.size(), 1U);
	ASSERT_EQ(result.labels.size(), 1U);
	const FeedbackBox& plate = result.labels[0].plate;
	EXPECT_FALSE(overlaps(plate, boxAround(halo[0].center, halo[0].radius + halo[0].width * 0.5F)));
	EXPECT_LT(distanceToBox(screen(cursor), plate), halo[0].radius + 30.0F) << "still beside the point it reads";
	const Foundation::Vec2 tip = screen(cursor);
	const Foundation::Vec2 center = centerOf(plate);
	EXPECT_FALSE(center.x > tip.x && center.y > tip.y) << "never lower right";
}

// Against the top edge neither upper quarter fits: the label takes down left, the next in
// order, never down right.
TEST_F(DrawingFeedbackTest, SegmentLabelAgainstTheTopEdgeTakesDownLeftNeverDownRight) {
	points = {worldAt({460.0F, 10.0F})};
	cursor = worldAt({500.0F, 10.0F}); // 40 px, arriving from the left: up right comes first
	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 1U);
	const FeedbackBox&	   plate = result.labels[0].plate;
	const Foundation::Vec2 tip = screen(cursor);
	expectInside(plate);
	EXPECT_LT(plate.right, tip.x);
	EXPECT_GT(plate.top, tip.y);
}

// From past the right edge the middle label is moved inside, where it clears the marker but
// sits within 40 px of the snapped point: the label goes to the end instead.
TEST_F(DrawingFeedbackTest, SegmentLabelMovedInsideStillKeepsFortyPixelsFromThePointer) {
	points = {worldAt({1300.0F, 400.0F})};
	cursor = worldAt({920.0F, 400.0F});
	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 1U);
	const FeedbackBox&	   plate = result.labels[0].plate;
	const Foundation::Vec2 tip = screen(cursor);
	EXPECT_LT(plate.right, tip.x) << "at the end, up left";
	EXPECT_LT(plate.bottom, tip.y);
}

// The near-start rescue: the click closes from within the close radius, so the halo is
// round the first point, off the snapped one. The end placement up right would sit on the
// halo and gives way to up left.
TEST_F(DrawingFeedbackTest, SegmentLabelEndPlacementOnAnOffCentreHaloTakesTheNext) {
	closeRadiusMeters = 1.5F; // 60 px: the halo is 30 px round the first point
	canClose = true;
	willClose = true;
	points = {worldAt({500.0F, 300.0F}), worldAt({600.0F, 500.0F}), worldAt({420.0F, 340.0F})};
	cursor = worldAt({460.0F, 340.0F}); // 57 px from the first point, outside the halo
	const DrawingFeedbackPlan result = plan();
	const auto				  halo = ringsIn(result, FeedbackLayer::Marker);
	ASSERT_EQ(halo.size(), 1U);
	ASSERT_EQ(result.labels.size(), 1U);
	const FeedbackBox&	   plate = result.labels[0].plate;
	const Foundation::Vec2 tip = screen(cursor);
	EXPECT_FALSE(overlaps(plate, boxAround(halo[0].center, halo[0].radius + halo[0].width * 0.5F)));
	EXPECT_LT(plate.right, tip.x) << "up left";
	EXPECT_LT(plate.bottom, tip.y);

	willClose = false;
	EXPECT_GT(plan().labels[0].plate.left, tip.x) << "up right without the halo";
}

// The same rescue with a wide close radius: the label beside the segment's middle would sit
// on the halo, so it takes the other side of the segment.
TEST_F(DrawingFeedbackTest, SegmentLabelAtTheMiddleKeepsOffAnOffCentreHalo) {
	closeRadiusMeters = 4.0F; // 160 px: the halo is 80 px round the first point
	canClose = true;
	willClose = true;
	points = {worldAt({500.0F, 250.0F}), worldAt({450.0F, 650.0F}), worldAt({350.0F, 350.0F})};
	cursor = worldAt({500.0F, 350.0F}); // 100 px below the first point, outside the halo
	const DrawingFeedbackPlan result = plan();
	const auto				  halo = ringsIn(result, FeedbackLayer::Marker);
	ASSERT_EQ(halo.size(), 1U);
	ASSERT_EQ(result.labels.size(), 1U);
	const FeedbackBox&	   plate = result.labels[0].plate;
	const Foundation::Vec2 tip = screen(cursor);
	EXPECT_FALSE(overlaps(plate, boxAround(halo[0].center, halo[0].radius + halo[0].width * 0.5F)));
	EXPECT_GT(plate.top, tip.y) << "below the segment";
	EXPECT_GE(distanceToBox(tip, plate), 40.0F);

	willClose = false;
	EXPECT_LT(plan().labels[0].plate.bottom, tip.y) << "above the segment, away from the shape, without the halo";
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
