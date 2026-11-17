#include "DrawingFeedback.h"

#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <world/camera/WorldCamera.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <string>
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
	constexpr float kPlatePaddingPx = 3.0F;
	constexpr float kLineHeightOfSize = 1.25F;
	// Plates are float sums of the camera's transform: one against a screen edge can land a
	// hair past it (1000.00006).
	constexpr float kViewportTolerancePx = 1e-3F;

	Foundation::Vec2 fakeSize(const std::string& text, float sizePx) {
		return {0.6F * sizePx * static_cast<float>(text.size()), kLineHeightOfSize * sizePx};
	}

	bool overlaps(const FeedbackBox& first, const FeedbackBox& second) {
		return first.left < second.right && second.left < first.right && first.top < second.bottom && second.top < first.bottom;
	}

	float distanceToBox(Foundation::Vec2 point, const FeedbackBox& box) {
		const float horizontal = std::max({box.left - point.x, 0.0F, point.x - box.right});
		const float vertical = std::max({box.top - point.y, 0.0F, point.y - box.bottom});
		return std::hypot(horizontal, vertical);
	}

	float distanceToSegment(Foundation::Vec2 point, Foundation::Vec2 start, Foundation::Vec2 end) {
		const Foundation::Vec2 along{end.x - start.x, end.y - start.y};
		const float			   lengthSquared = along.x * along.x + along.y * along.y;
		const float			   fraction =
			lengthSquared > 0.0F ? std::clamp(((point.x - start.x) * along.x + (point.y - start.y) * along.y) / lengthSquared, 0.0F, 1.0F) : 0.0F;
		return std::hypot(point.x - (start.x + along.x * fraction), point.y - (start.y + along.y * fraction));
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
		std::optional<FeedbackBox>	  keepClear;
		construction::RectangleSnapKind rectangleSnap = construction::RectangleSnapKind::None;
		std::string					  sizeLabel;
		int						  viewportWidth = kViewportWidth;
		int							  viewportHeight = kViewportHeight;
		float						  uiScale = 1.0F;
		// Measures every label but the reserved "999.00 m" this many characters narrower: the
		// same frame with one digit fewer.
		int narrowerByCharacters = 0;

		void SetUp() override { camera.setZoom(kZoom); }

		void addGuide(SnapGuide guide) {
			guide.distanceFromNodeMeters = std::hypot(guide.to.x - guide.from.x, guide.to.y - guide.from.y);
			snap.guides[snap.guideCount++] = guide;
		}

		Foundation::Vec2 screen(Foundation::Vec2 world) const {
			return camera.worldToScreen(world.x, world.y, viewportWidth, viewportHeight, kPixelsPerMeter);
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
				.viewportWidth = viewportWidth,
				.viewportHeight = viewportHeight,
				.uiScale = uiScale,
				.pixelsPerMeter = kPixelsPerMeter,
				.closeRadiusMeters = closeRadiusMeters,
				.canClose = canClose,
				.willClose = willClose,
				.snappedPointInvalid = snappedPointInvalid,
				.invalidColor = invalidColor,
				.keepClear = keepClear,
				.rectangleSnap = rectangleSnap,
				.sizeLabel = sizeLabel,
			};
			char reserved[32];
			std::snprintf(reserved, sizeof(reserved), "%.*f m", style.guideDistanceDecimals, 999.0);
			return world_sim::planSnapFeedback(input, [&](const std::string& text, float sizePx) {
				const Foundation::Vec2 size = fakeSize(text, sizePx);
				return text == reserved ? size : Foundation::Vec2{size.x - 0.6F * sizePx * static_cast<float>(narrowerByCharacters), size.y};
			});
		}

		static std::vector<FeedbackStroke> strokesIn(const DrawingFeedbackPlan& plan, FeedbackLayer layer) {
			std::vector<FeedbackStroke> found;
			std::copy_if(plan.strokes.begin(), plan.strokes.end(), std::back_inserter(found), [&](const FeedbackStroke& stroke) {
				return stroke.layer == layer;
			});
			return found;
		}

		static std::vector<FeedbackDot> dotsIn(const DrawingFeedbackPlan& plan, FeedbackLayer layer) {
			std::vector<FeedbackDot> found;
			std::copy_if(plan.dots.begin(), plan.dots.end(), std::back_inserter(found), [&](const FeedbackDot& dot) { return dot.layer == layer; });
			return found;
		}

		static std::vector<FeedbackRing> ringsIn(const DrawingFeedbackPlan& plan, FeedbackLayer layer) {
			std::vector<FeedbackRing> found;
			std::copy_if(plan.rings.begin(), plan.rings.end(), std::back_inserter(found), [&](const FeedbackRing& ring) {
				return ring.layer == layer;
			});
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

		// Where the segment label sits: beside the middle, on one side of the segment, or at the
		// end, in one quarter round the snapped point. Away from the middle, the end placements
		// sit within 30 px of the snapped point, and the middle ones at least 40 px from it.
		struct SegmentLabelPlacement {
			bool						atMiddle = false;
			std::tuple<int, int, int> side; // the segment's side at the middle; right and below at the end
			bool						operator==(const SegmentLabelPlacement&) const = default;
		};

		SegmentLabelPlacement segmentLabelPlacement(const DrawingFeedbackPlan& result) const {
			const bool atMiddle = distanceToBox(screen(cursor), result.labels[0].plate) >= 30.0F;
			const auto [right, below, segmentSide] = segmentLabelSide(result);
			return atMiddle ? SegmentLabelPlacement{true, {0, 0, segmentSide}} : SegmentLabelPlacement{false, {right, below, 0}};
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
				if (tip.x < 0.0F || tip.y < 0.0F || tip.x > static_cast<float>(viewportWidth) || tip.y > static_cast<float>(viewportHeight)) {
					continue;
				}
				const DrawingFeedbackPlan result = plan();
				if (result.labels.empty()) {
					continue; // the length rounds to zero
				}
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

		void expectInside(const FeedbackBox& box) {
			EXPECT_GE(box.left, -kViewportTolerancePx);
			EXPECT_GE(box.top, -kViewportTolerancePx);
			EXPECT_LE(box.right, static_cast<float>(viewportWidth) + kViewportTolerancePx);
			EXPECT_LE(box.bottom, static_cast<float>(viewportHeight) + kViewportTolerancePx);
		}
	};

	void expectColor(const Foundation::Color& actual, const assets::StyleColor& rgb, float alpha) {
		EXPECT_FLOAT_EQ(actual.r, rgb.r);
		EXPECT_FLOAT_EQ(actual.g, rgb.g);
		EXPECT_FLOAT_EQ(actual.b, rgb.b);
		EXPECT_FLOAT_EQ(actual.a, alpha);
	}

	void expectNear(Foundation::Vec2 actual, Foundation::Vec2 expected, float tolerance = 1e-3F) {
		EXPECT_NEAR(actual.x, expected.x, tolerance);
		EXPECT_NEAR(actual.y, expected.y, tolerance);
	}

	// A guide's stroke ends within this of the snapped point: it stops a pixel short of the
	// solid segment ending there.
	constexpr float kStrokeEndTolerancePx = 1.0F + 1e-3F;

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

TEST_F(DrawingFeedbackTest, CrossingOfTwoSourcesColorsEachGuideAndDotsThePointOverTheMarker) {
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

// The second label gives way back toward its node first.
TEST_F(DrawingFeedbackTest, OverlappingGuideLabelMovesBackAlongItsGuide) {
	display.guideDistances = true;
	points = {{0.0F, -3.0F}}; // the segment comes down from above, so both labels go below their guides
	cursor = {0.0F, 0.0F};
	addGuide({.from = {-8.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	addGuide({.from = {-8.0F, 0.2F}, .to = cursor, .source = GuideSource::NearbyStructure});

	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 3U); // the segment's, then each guide's
	const FeedbackLabel& first = result.labels[1];
	const FeedbackLabel& second = result.labels[2];
	EXPECT_FALSE(overlaps(first.plate, second.plate));
	// The first guide's label keeps its place centered below its guide's middle.
	const Foundation::Vec2 middle = screen({-4.0F, 0.0F});
	EXPECT_NEAR(centerOf(first.plate).x, middle.x, 0.5F + 1e-3F) << "the stroke stops a pixel short of the segment";
	EXPECT_NEAR(first.position.y, middle.y + 10.0F, 1e-3F);
	// The second has moved back toward its node by its own width and more, though the spot
	// forward was clear too.
	const float secondMiddleX = screen({-4.0F, 0.1F}).x;
	const float secondWidth = second.plate.right - second.plate.left - 2.0F * kPlatePaddingPx;
	EXPECT_LT(centerOf(second.plate).x, secondMiddleX - secondWidth);
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

// A guide label's shifts stop at its stroke's ends: on a short stroke it sits at an end, and
// when the spots along the stroke are all covered it is dropped rather than drifting off it.
TEST_F(DrawingFeedbackTest, GuideLabelShiftsStayOnItsStroke) {
	display.guideDistances = true;
	const Foundation::Vec2 node = worldAt({300.0F, 400.0F});
	cursor = worldAt({360.0F, 400.0F}); // 60 px: the middle is on the marker's box
	addGuide({.from = node, .to = cursor, .source = GuideSource::WorldAxes});

	const DrawingFeedbackPlan shifted = plan();
	ASSERT_EQ(shifted.labels.size(), 1U);
	const float center = centerOf(shifted.labels[0].plate).x;
	EXPECT_GE(center, 300.0F - 1e-3F) << "not past the node";
	EXPECT_LE(center, 360.0F + 1e-3F);

	keepClear = FeedbackBox{240.0F, 300.0F, 340.0F, 500.0F}; // over every spot short of the marker
	EXPECT_TRUE(plan().labels.empty());
}

// A stroke whose middle is off the screen: its label sits beside the middle of the part on
// screen, not moved in from off it to a corner far from the stroke.
TEST_F(DrawingFeedbackTest, GuideLabelSitsBesideThePartOfItsStrokeOnScreen) {
	display.guideDistances = true;
	const Foundation::Vec2 node{-1300.0F, -1600.0F};
	const Foundation::Vec2 tip{700.0F, 400.0F};
	cursor = worldAt(tip);
	addGuide({.from = worldAt(node), .to = cursor, .source = GuideSource::WorldAxes});

	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 1U);
	const FeedbackBox& plate = result.labels[0].plate;
	expectInside(plate);
	// The stroke enters the screen at (300, 0): the label sits by that part's middle, (500, 200).
	EXPECT_LT(distanceToBox({500.0F, 200.0F}, plate), 20.0F);
}

// The middle of a guide along the segment lies on the segment's line, where neither side
// is away from it: the label takes the fixed side (left of a vertical guide) however the
// previous point jitters across the line.
TEST_F(DrawingFeedbackTest, GuideLabelAlongTheSegmentTakesTheFixedSide) {
	display.guideDistances = true;
	cursor = {0.0F, 6.0F};
	addGuide({.from = {0.0F, -4.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	for (const float jitter : {0.0F, 1e-4F, -1e-4F}) {
		SCOPED_TRACE(::testing::Message() << "the previous point " << jitter << " m off the line");
		points = {{5.0F, -5.0F}, {jitter, 1.0F}};
		const DrawingFeedbackPlan result = plan();
		ASSERT_EQ(result.labels.size(), 2U);
		EXPECT_LT(result.labels[1].plate.right, screen({0.0F, 0.0F}).x) << "left of the guide";
	}
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

TEST_F(DrawingFeedbackTest, DuplicateGuidesDrawOneStrokeAndLabelAndEachEdgesMarker) {
	display.guideDistances = true;
	cursor = {0.0F, 3.0F};
	const SnapGuide plain{.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes};
	const SnapGuide square{.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::DrawnShape, .rightAngle = true, .rightAngleEdgeDirection = {1.0F, 0.0F}};
	SnapGuide		otherEdge = square;
	otherEdge.rightAngleEdgeDirection = {-1.0F, 0.0F};
	SnapGuide nearlyPlain = plain;
	nearlyPlain.from = {0.01F, 0.0F}; // under half a pixel away
	nearlyPlain.source = GuideSource::NearbyStructure;

	for (const auto& [name, first, second, markers] : {
			 std::tuple{"under half a pixel apart", plain, nearlyPlain, 0U},
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
		EXPECT_EQ(result.labels.size(), 1U);
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
	EXPECT_GT(shortBox.left, tip.x) << "the segment arrives from the left, so the label goes right";

	style.guideDistanceDecimals = 1;
	EXPECT_EQ(plan().labels[0].text, "1.2 m");

	style.guideDistanceDecimals = 2;
	cursor = {6.0F, 0.0F}; // 240 px: the label sits beside the middle
	const FeedbackLabel longLabel = plan().labels[0];
	EXPECT_EQ(longLabel.text, "6.00 m");
	EXPECT_GE(distanceToBox(screen(cursor), longLabel.plate), 40.0F);
	const Foundation::Vec2 middle = screen({3.0F, 0.0F});
	EXPECT_NEAR(longLabel.plate.bottom - kPlatePaddingPx, middle.y - 10.0F, 1e-3F) << "above the segment";
	EXPECT_NEAR(centerOf(longLabel.plate).x, middle.x, 1e-3F) << "centered on the middle";
}

// With no placed points' center off the line to pick a side, the label faces up the screen, or
// left when the line is within about 11 degrees of vertical (its normal's y under 0.2).
TEST_F(DrawingFeedbackTest, SegmentLabelOnANearVerticalLineTakesTheLeftAndOnOneFurtherOffTheUpSide) {
	points = {{0.0F, 0.0F}};
	for (const auto& [degreesOffVertical, expectLeft] : {std::pair{4.0F, true}, std::pair{20.0F, false}}) {
		SCOPED_TRACE(::testing::Message() << degreesOffVertical << " degrees off vertical");
		const float radians = degreesOffVertical * std::numbers::pi_v<float> / 180.0F;
		cursor = {6.0F * std::sin(radians), 6.0F * std::cos(radians)};
		const DrawingFeedbackPlan result = plan();
		ASSERT_EQ(result.labels.size(), 1U);
		const Foundation::Vec2 middle = screen({cursor.x * 0.5F, cursor.y * 0.5F});
		const float			   sideOfMiddle = centerOf(result.labels[0].plate).x - middle.x;
		EXPECT_EQ(sideOfMiddle < 0.0F, expectLeft);
	}
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

// Near a screen edge every placement's fit is judged at the reserved width too, so the label
// growing a digit can't send it to the other side and back. The first ray is the reader's
// case: at 10.00 m the plate's right edge passed 1000 and it flipped until 10.10 m.
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
				const float		radians = static_cast<float>(degrees) * std::numbers::pi_v<float> / 180.0F;
				const SideSweep sweep = sweepSegmentLabel(worldAt(firstPoint), {std::cos(radians), std::sin(radians)}, 30, 0.01F, startMeters);
				EXPECT_EQ(sweep.returns, 0);
			}
		}
	}
}

// The reviewer's ray from 20 px off the left edge: judged at its own width, the label's end
// placement went up right, down left, back up right when it gained the 10.00 m digit, down
// left again, then to the middle.
TEST_F(DrawingFeedbackTest, SegmentLabelEndPlacementNearAnEdgeKeepsItsSideAsItGainsADigit) {
	camera.setZoom(1.0F);
	const float		radians = 57.37F * std::numbers::pi_v<float> / 180.0F;
	const SideSweep sweep = sweepSegmentLabel(worldAt({20.0F, 400.0F}), {std::cos(radians), std::sin(radians)}, 1500);
	EXPECT_EQ(sweep.returns, 0);
	EXPECT_LE(sweep.changes, 2);
}

// With nothing clear (here a keep-clear box over the whole screen), the label takes the first
// end placement that fits the screen unmoved at the reserved width: arriving from the right,
// up left comes first, and 70 px from the left edge it fits at its own width but not at the
// reserved one, so up right it is, before and after the digit at 10 m.
TEST_F(DrawingFeedbackTest, SegmentLabelWithNoClearPlacementTakesTheFirstThatFitsAtTheReservedWidth) {
	camera.setZoom(1.0F);
	keepClear = FeedbackBox{0.0F, 0.0F, static_cast<float>(kViewportWidth), static_cast<float>(kViewportHeight)};
	const Foundation::Vec2 tip{70.0F, 400.0F};
	cursor = worldAt(tip);
	for (const float pixels : {72.0F, 88.0F}) { // 9.00 m and 11.00 m at 8 px to the meter
		SCOPED_TRACE(::testing::Message() << pixels << " px");
		points = {worldAt({tip.x + pixels, tip.y})};
		const DrawingFeedbackPlan result = plan();
		ASSERT_EQ(result.labels.size(), 1U);
		EXPECT_GT(result.labels[0].plate.left, tip.x) << "up right";
		EXPECT_LT(result.labels[0].plate.bottom, tip.y);
	}
}

// A segment from off the screen: its middle label is moved inside, which near the snapped
// point would put it on the marker's ticks or within 40 px of the pointer. It takes the end
// until the moved plate clears the pointer, one change of side and no flicker.
TEST_F(DrawingFeedbackTest, SegmentLabelMovedInsideTheScreenKeepsOffTheMarkerAndThePointer) {
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
	const FeedbackBox&	   plate = result.labels[0].plate;
	const Foundation::Vec2 tip = screen(cursor);
	for (const FeedbackBox& marker : markerBoxes(tip)) {
		EXPECT_FALSE(overlaps(plate, marker));
	}
	expectInside(plate);
	EXPECT_LT(plate.right, tip.x) << "at the end, up left: moved inside, the middle would sit within 40 px of the pointer";
	EXPECT_LT(plate.bottom, tip.y);
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
	const FeedbackLabel	   label = plan().labels[0];
	const Foundation::Vec2 size = fakeSize(label.text, label.sizePx);
	EXPECT_FLOAT_EQ(size.y, kLineHeightOfSize * label.sizePx);
	EXPECT_FLOAT_EQ(label.plate.left, label.position.x - kPlatePaddingPx);
	EXPECT_FLOAT_EQ(label.plate.top, label.position.y - kPlatePaddingPx);
	EXPECT_FLOAT_EQ(label.plate.right, label.position.x + size.x + kPlatePaddingPx);
	EXPECT_FLOAT_EQ(label.plate.bottom, label.position.y + size.y + kPlatePaddingPx);
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
	expectNear(guides[0].points[1], screen(points[1]), kStrokeEndTolerancePx);
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

	points = {{5.0F, -5.0F}, {-3.0F, 6.0F}}; // off the guide: the whole stroke but its last pixel
	const DrawingFeedbackPlan whole = plan();
	ASSERT_EQ(whole.labels.size(), 2U);
	EXPECT_EQ(whole.labels[1].text, "10.00 m");
	EXPECT_NEAR(centerOf(whole.labels[1].plate).y, screen({0.0F, 1.0F}).y, 0.5F + 1e-3F);

	points = {{0.0F, -4.0F}, {0.0F, -2.0F}}; // along the guide: dotted from -4 m to a pixel short of -2 m
	const DrawingFeedbackPlan shortened = plan();
	ASSERT_EQ(shortened.labels.size(), 2U);
	EXPECT_EQ(shortened.labels[1].text, "10.00 m");
	EXPECT_NEAR(centerOf(shortened.labels[1].plate).y, screen({0.0F, -3.0F}).y, 0.5F + 1e-3F);

	points = {{0.0F, -4.0F}, {0.0F, -6.0F}}; // the previous point behind the node: no stroke
	const DrawingFeedbackPlan hidden = plan();
	EXPECT_TRUE(strokesIn(hidden, FeedbackLayer::Alignment).empty());
	ASSERT_EQ(hidden.labels.size(), 1U); // the segment label alone
	EXPECT_EQ(hidden.labels[0].text, "12.00 m");
}

TEST_F(DrawingFeedbackTest, GuideOffTheSegmentKeepsItsStrokeToAPixelShortOfThePoint) {
	cursor = {0.0F, 6.0F};
	for (const auto& [name, previous, from] : {
			 std::tuple{"at a right angle", Foundation::Vec2{-3.0F, 6.0F}, Foundation::Vec2{0.0F, -4.0F}},
			 std::tuple{"collinear, the previous point past the snapped point", Foundation::Vec2{0.0F, 8.0F}, Foundation::Vec2{0.0F, -4.0F}},
		 }) {
		SCOPED_TRACE(name);
		points = {{5.0F, -5.0F}, previous};
		snap.guideCount = 0;
		addGuide({.from = from, .to = cursor, .source = GuideSource::WorldAxes});
		const auto guides = strokesIn(plan(), FeedbackLayer::Alignment);
		ASSERT_EQ(guides.size(), 1U);
		expectNear(guides[0].points[0], screen(from));
		expectNear(guides[0].points[1], screen(cursor), kStrokeEndTolerancePx);
	}
}

// One rule: a stroke draws only where it lies more than a pixel from the solid segment. A
// guide a fraction of a degree off the segment runs within a pixel of it along its last
// stretch, and stops where it comes that close.
TEST_F(DrawingFeedbackTest, GuideStrokeStopsWhereItComesWithinAPixelOfTheSolidSegment) {
	cursor = {0.0F, 6.0F};
	points = {{5.0F, -5.0F}, {0.05F, 1.0F}}; // the segment starts 2 px off the guide's line and meets it at the snapped point
	addGuide({.from = {0.0F, -4.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	const auto guides = strokesIn(plan(), FeedbackLayer::Alignment);
	ASSERT_EQ(guides.size(), 1U);
	const Foundation::Vec2 start = screen(points.back());
	const Foundation::Vec2 tip = screen(cursor);
	const Foundation::Vec2 from = guides[0].points[0];
	const Foundation::Vec2 end = guides[0].points[1];
	EXPECT_NEAR(distanceToSegment(end, start, tip), 1.0F, 1e-2F);
	EXPECT_NEAR(end.y, screen({0.0F, 3.5F}).y, 0.5F) << "halfway down the segment, where it is a pixel off";
	for (int step = 0; step <= 100; ++step) {
		const float fraction = static_cast<float>(step) / 100.0F;
		const Foundation::Vec2 along{from.x + (end.x - from.x) * fraction, from.y + (end.y - from.y) * fraction};
		EXPECT_GE(distanceToSegment(along, start, tip), 1.0F - 1e-2F) << "at " << fraction;
	}

	// The reviewer's frame (800 by 600 at zoom 0.5, here moved into the 1000 by 800 screen):
	// a guide within a pixel of the segment its whole length, which drew 55 px of dots over it.
	camera.setZoom(0.5F);
	cursor = worldAt({242.726F, 102.416F});
	points = {worldAt({236.251F, -441.807F})};
	snap.guideCount = 0;
	addGuide({.from = worldAt({242.726F, 47.751F}), .to = cursor, .source = GuideSource::NearbyStructure});
	EXPECT_TRUE(strokesIn(plan(), FeedbackLayer::Alignment).empty());
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

// The near-start rescue: the click closes from within the close radius, so the halo is
// round the first point, off the snapped one. The end placement up right would sit on the
// halo and gives way to up left.
TEST_F(DrawingFeedbackTest, SegmentLabelEndPlacementOnAnOffCenterHaloTakesTheNext) {
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
TEST_F(DrawingFeedbackTest, SegmentLabelAtTheMiddleKeepsOffAnOffCenterHalo) {
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

// A rejected edit's reason draws up right of the pointer; the label's end placement there
// would cover it, so the label takes the next.
TEST_F(DrawingFeedbackTest, SegmentLabelKeepsOffTheKeepClearBox) {
	points = {worldAt({460.0F, 400.0F})};
	cursor = worldAt({500.0F, 400.0F}); // 40 px, arriving from the left: up right comes first
	const FeedbackBox reason{514.0F, 378.0F, 514.0F + 140.0F, 378.0F + 16.0F};
	const FeedbackBox upRight = plan().labels[0].plate;
	ASSERT_TRUE(overlaps(upRight, reason)) << "without the box the label sits on the reason";

	keepClear = reason;
	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 1U);
	EXPECT_FALSE(overlaps(result.labels[0].plate, reason));
	EXPECT_LT(result.labels[0].plate.right, 500.0F) << "the next placement, up left";
}

// The wall tool's snap-target ring round a short segment's end: the label's end placement
// moves off it as off the close halo.
TEST_F(DrawingFeedbackTest, SegmentLabelKeepsOffTheWallSnapTargetRing) {
	tool = world_sim::ToolKind::Wall;
	snap.kind = SnapKind::WallEndpoint;
	points = {worldAt({460.0F, 400.0F})};
	cursor = worldAt({500.0F, 400.0F}); // 40 px: the label sits at the end

	const DrawingFeedbackPlan result = plan();
	const auto				  rings = ringsIn(result, FeedbackLayer::Target);
	ASSERT_EQ(rings.size(), 1U);
	ASSERT_EQ(result.labels.size(), 1U);
	const FeedbackBox& plate = result.labels[0].plate;
	EXPECT_FALSE(overlaps(plate, boxAround(rings[0].center, rings[0].radius + rings[0].width * 0.5F)));
	EXPECT_GT(plate.left, 500.0F) << "still up right";
	EXPECT_LT(plate.bottom, 400.0F);
	EXPECT_LT(distanceToBox(screen(cursor), plate), rings[0].radius + 30.0F) << "still beside the point it reads";
}

// A guide label whose middle spot covers only the ring's outer two pixels (outside the
// marker's ticks) moves along its guide off the whole ring.
TEST_F(DrawingFeedbackTest, GuideLabelKeepsOffTheWholeWallSnapTargetRing) {
	tool = world_sim::ToolKind::Wall;
	snap.kind = SnapKind::WallEndpoint;
	display.guideDistances = true;
	cursor = worldAt({500.0F, 400.0F});
	addGuide({.from = worldAt({500.0F, 340.0F}), .to = cursor, .source = GuideSource::WorldAxes}); // its middle label's plate ends at 379.9

	const DrawingFeedbackPlan result = plan();
	const auto				  rings = ringsIn(result, FeedbackLayer::Target);
	ASSERT_EQ(rings.size(), 1U);
	ASSERT_EQ(result.labels.size(), 1U);
	EXPECT_FALSE(overlaps(result.labels[0].plate, boxAround(rings[0].center, rings[0].radius + rings[0].width * 0.5F)));
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
	ASSERT_EQ(closing.rings.size(), 1U) << "the halo alone marks the first point: no dotted close ring under it";
	const auto& halo = closing.rings[0];
	EXPECT_FALSE(halo.dash.has_value());

	willClose = false; // a refused close draws no halo, so the dotted ring stays
	cursor = points[0];
	const DrawingFeedbackPlan refused = plan();
	ASSERT_EQ(refused.rings.size(), 1U);
	EXPECT_EQ(refused.rings[0].layer, FeedbackLayer::Alignment);
	EXPECT_TRUE(refused.rings[0].dash.has_value());
	willClose = true;
	EXPECT_EQ(halo.layer, FeedbackLayer::Marker);
	EXPECT_FLOAT_EQ(halo.radius, kTickOuterPx + 6.0F) << "the 20 px close radius halves to 10 px, inside the ticks: the floor holds";
	EXPECT_FLOAT_EQ(halo.width, 3.0F);
	EXPECT_FALSE(halo.dash.has_value());
	expectColor(halo.color, style.guideColor, style.markerOpacity);

	closeRadiusMeters = 2.0F; // 80 px on screen: the halo is half of it
	EXPECT_FLOAT_EQ(plan().rings[0].radius, 40.0F);

	style.guideColorMode = assets::GuideColorMode::BySource;
	expectColor(plan().rings[0].color, style.guideColorShape, style.markerOpacity);
}

TEST_F(DrawingFeedbackTest, NoPointsPlacedDrawsOnlyTheSnappedPointMarker) {
	cursor = {1.0F, 1.0F};
	addGuide({.from = cursor, .to = cursor, .source = GuideSource::WorldAxes}); // zero length

	const DrawingFeedbackPlan result = plan();
	EXPECT_TRUE(result.labels.empty());
	EXPECT_TRUE(result.rings.empty());
	EXPECT_TRUE(strokesIn(result, FeedbackLayer::Alignment).empty());

	ASSERT_EQ(result.dots.size(), 1U);
	const Foundation::Vec2 snappedPoint = screen(cursor);
	expectNear(result.dots[0].center, snappedPoint);
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
	expectNear(ticks[0].points[0], {snappedPoint.x - 11.0F, snappedPoint.y});
	expectNear(ticks[0].points[1], {snappedPoint.x - 16.0F, snappedPoint.y});

	style.cursorRadiusPx = 2.0F;
	EXPECT_FLOAT_EQ(plan().dots[0].radius, 4.0F);
}

TEST_F(DrawingFeedbackTest, OffBuildableGroundTheMarkerDotIsTheInvalidColor) {
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

	points = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}};
	canClose = true;
	willClose = true;
	for (const float radius : {nan, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 0.0F, -1.0F}) {
		SCOPED_TRACE(::testing::Message() << "close radius " << radius);
		closeRadiusMeters = radius;
		const DrawingFeedbackPlan result = plan();
		EXPECT_TRUE(ringsIn(result, FeedbackLayer::Alignment).empty()) << "no close ring";
		for (const FeedbackRing& ring : result.rings) {
			EXPECT_TRUE(std::isfinite(ring.radius) && ring.radius > 0.0F);
		}
	}
	closeRadiusMeters = 0.5F;

	cursor = {nan, nan};
	const DrawingFeedbackPlan badPoint = plan();
	EXPECT_TRUE(badPoint.labels.empty());
	EXPECT_TRUE(badPoint.dots.empty());
	EXPECT_TRUE(strokesIn(badPoint, FeedbackLayer::Cursor).empty());
}

// Nothing to measure while the snapped point is on the previous point; a guide distance that
// rounds to zero still draws, unsigned.
TEST_F(DrawingFeedbackTest, ALengthThatRoundsToZeroHasNoSegmentLabel) {
	points = {{0.0F, 0.0F}};
	for (const float meters : {0.0F, 0.004F}) {
		cursor = {meters, 0.0F};
		EXPECT_TRUE(plan().labels.empty()) << meters;
	}
	cursor = {0.006F, 0.0F};
	ASSERT_EQ(plan().labels.size(), 1U);
	EXPECT_EQ(plan().labels[0].text, "0.01 m");

	points.clear();
	display.guideDistances = true;
	cursor = {0.0F, 3.0F};
	addGuide({.from = {0.0F, 0.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	snap.guides[0].distanceFromNodeMeters = -0.001F;
	ASSERT_EQ(plan().labels.size(), 1U);
	EXPECT_EQ(plan().labels[0].text, "0.00 m");
}

// The reviewer's frame: the wall tool on a foundation edge 3 px below the top of the screen.
// The label has a clear placement, and it takes one off the snap-target ring.
TEST_F(DrawingFeedbackTest, SegmentLabelAgainstTheTopEdgeKeepsOffTheWallSnapTargetRing) {
	viewportWidth = 1366;
	viewportHeight = 768;
	camera.setZoom(4.373456F);
	tool = world_sim::ToolKind::Wall;
	snap.kind = SnapKind::Edge;
	cursor = worldAt({83.8177795F, 3.28006268F});
	points = {worldAt({84.871315F, 5.29141045F})};

	const DrawingFeedbackPlan result = plan();
	ASSERT_EQ(result.labels.size(), 1U);
	const auto ring = ringsIn(result, FeedbackLayer::Target);
	ASSERT_EQ(ring.size(), 1U);
	const float reach = ring[0].radius + ring[0].width * 0.5F;
	EXPECT_FALSE(overlaps(result.labels[0].plate, boxAround(ring[0].center, reach)));
	expectInside(result.labels[0].plate);
}

// The reviewer's frame: a 5 cm segment against the top edge. Its end placements' fit and
// clearance are judged at the reserved width, so the label one digit narrower takes the same one.
TEST_F(DrawingFeedbackTest, SegmentLabelEndPlacementAgainstTheTopEdgeHoldsWithADigitFewer) {
	viewportWidth = 1577;
	viewportHeight = 1246;
	camera.setZoom(0.575138092F);
	cursor = worldAt({1519.06531F, 79.6971207F});
	points = {worldAt({1518.91284F, 79.8374252F})};

	const DrawingFeedbackPlan wide = plan();
	narrowerByCharacters = 1;
	const DrawingFeedbackPlan narrow = plan();
	ASSERT_EQ(wide.labels.size(), 1U);
	ASSERT_EQ(narrow.labels.size(), 1U);
	EXPECT_EQ(segmentLabelSide(wide), segmentLabelSide(narrow));
	EXPECT_FLOAT_EQ(wide.labels[0].plate.top, narrow.labels[0].plate.top);
}

// D16's bound on flicker: with the snapped point at least 120 px and the previous point at
// least 100 px from every edge, and no halo or reason drawn, the segment label changes
// placement at most once along a ray, from the end to the middle. Rays every 15 degrees from
// a grid of previous points, with and without an earlier point.
TEST_F(DrawingFeedbackTest, SegmentLabelChangesPlacementAtMostOnceAwayFromTheEdges) {
	camera.setZoom(2.0F);
	constexpr float kSnappedMarginPx = 120.0F;
	constexpr float kPreviousMarginPx = 100.0F;
	constexpr float kStepPx = 6.0F;
	const float		width = static_cast<float>(viewportWidth);
	const float		height = static_cast<float>(viewportHeight);
	const auto		inside = [&](Foundation::Vec2 point, float margin) {
		 return point.x >= margin && point.y >= margin && point.x <= width - margin && point.y <= height - margin;
	};
	int plans = 0;
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 3; ++row) {
			const Foundation::Vec2 previous{
				kPreviousMarginPx + (width - 2.0F * kPreviousMarginPx) * static_cast<float>(column) / 3.0F,
				kPreviousMarginPx + (height - 2.0F * kPreviousMarginPx) * static_cast<float>(row) / 2.0F,
			};
			for (const bool withEarlierPoint : {false, true}) {
				for (int degrees = 0; degrees < 360; degrees += 15) {
					SCOPED_TRACE(::testing::Message() << "from " << previous.x << ", " << previous.y << " at " << degrees << " degrees"
													  << (withEarlierPoint ? ", with an earlier point" : ""));
					const float			   radians = static_cast<float>(degrees) * std::numbers::pi_v<float> / 180.0F;
					const Foundation::Vec2 direction{std::cos(radians), std::sin(radians)};
					points = {worldAt(previous)};
					if (withEarlierPoint) {
						points.insert(points.begin(), worldAt({previous.x - 300.0F * direction.y, previous.y + 300.0F * direction.x}));
					}
					std::vector<SegmentLabelPlacement> placements;
					for (float along = kStepPx;; along += kStepPx) {
						const Foundation::Vec2 tip{previous.x + direction.x * along, previous.y + direction.y * along};
						if (!inside(tip, kSnappedMarginPx)) {
							if (along > width + height) {
								break;
							}
							continue;
						}
						cursor = worldAt(tip);
						const DrawingFeedbackPlan result = plan();
						++plans;
						ASSERT_EQ(result.labels.size(), 1U);
						const SegmentLabelPlacement placement = segmentLabelPlacement(result);
						if (placements.empty() || placements.back() != placement) {
							placements.push_back(placement);
						}
					}
					EXPECT_LE(placements.size(), 2U);
					if (placements.size() == 2U) {
						EXPECT_FALSE(placements[0].atMiddle) << "from the end";
						EXPECT_TRUE(placements[1].atMiddle) << "to the middle";
					}
				}
			}
		}
	}
	EXPECT_LT(plans, 100000);
}

// Two placed points, the earlier one a hair inside the 0.5 px a placed points' center must sit
// off the segment's line to pick a side of it. Measured from screen floats, the snapped point's
// rounding carried it across that threshold from one centimeter to the next, and the label
// crossed the segment each time. The reviewer's ray (its earlier point as printed, to three
// places), then two built to sit a ten-thousandth of a pixel inside.
TEST_F(DrawingFeedbackTest, SegmentLabelSideHoldsWithTheShapesCenterAtTheEdgeOfTheDeadZone) {
	viewportWidth = 1366;
	viewportHeight = 768;
	camera.setZoom(8.0F);
	const float			   pixelsPerMeter = kPixelsPerMeter * 8.0F;
	const Foundation::Vec2 previous{470.0F, 600.0F};
	const auto			   directionAt = [](float degrees) {
		  const float radians = degrees * std::numbers::pi_v<float> / 180.0F;
		  return Foundation::Vec2{std::cos(radians), std::sin(radians)};
	};
	// 333 px back along the ray and `offset` px off it, to its left looking along it.
	const auto earlierPoint = [&](Foundation::Vec2 direction, double offset) {
		return Foundation::Vec2{
			static_cast<float>(previous.x - direction.x * 333.0 + direction.y * offset),
			static_cast<float>(previous.y - direction.y * 333.0 - direction.x * offset),
		};
	};
	constexpr double kGapPx = 1e-4;
	for (const auto& [degrees, earlier] : {
			 std::pair{327.0F, Foundation::Vec2{190.561F, 780.278F}},
			 std::pair{20.0F, earlierPoint(directionAt(20.0F), 2.0 * (0.5 - kGapPx))},
			 std::pair{200.0F, earlierPoint(directionAt(200.0F), -2.0 * (0.5 - kGapPx))},
		 }) {
		SCOPED_TRACE(::testing::Message() << degrees << " degrees");
		const Foundation::Vec2 direction = directionAt(degrees);
		points = {worldAt(earlier), worldAt(previous)};
		std::vector<SegmentLabelPlacement> placements;
		for (int centimeters = 100; centimeters <= 300; ++centimeters) {
			const float along = static_cast<float>(centimeters) * 0.01F * pixelsPerMeter;
			cursor = worldAt({previous.x + direction.x * along, previous.y + direction.y * along});
			const DrawingFeedbackPlan result = plan();
			ASSERT_EQ(result.labels.size(), 1U);
			if (const SegmentLabelPlacement placement = segmentLabelPlacement(result); placements.empty() || placements.back() != placement) {
				placements.push_back(placement);
			}
		}
		EXPECT_LE(placements.size(), 2U);
	}
}

// The interface size scales the label text and its plate only; every stroke, ring, and dot is
// in window pixels, like the preview's own lines (main's rule for text drawn in the world). A
// frame at a ui scale of s plans exactly the strokes, rings, and dots it plans at 1, and labels
// whose text size, plate padding, and plate corner are s times the ones at 1. A bigger label
// can sit elsewhere, so positions aren't compared; the placement rules hold at every scale.
TEST_F(DrawingFeedbackTest, OnlyTheLabelsFollowTheUiScale) {
	constexpr int kWidth = 1200;
	constexpr int kHeight = 960;
	// The cursor at least this far inside every edge: a frame away from the screen edges.
	constexpr float kAwayFromEdgesPx = 250.0F;
	std::mt19937	random(894);
	const auto		between = [&](float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); };
	const auto		chance = [&](float odds) { return between(0.0F, 1.0F) < odds; };
	const auto		oneOf = [&](int count) { return std::uniform_int_distribution<int>(0, count - 1)(random); };

	struct Frame {
		assets::PreviewStyle		  style;
		world_sim::ToolKind			  tool;
		construction::SnapResult	  snap;
		std::vector<Foundation::Vec2> points;
		Foundation::Vec2			  cursor;
		float						  zoom;
		float						  closeRadiusMeters;
		bool						  canClose;
		bool						  willClose;
		bool						  guideDistances;
		std::optional<FeedbackBox>	  keepClear;
	};
	std::vector<Frame> frames;
	for (int index = 0; index < 600; ++index) {
		Frame frame{};
		// The marker's radius below its 4 px floor and above it (up to 7 px: from 8 px its dot's box
		// reaches the segment label's end placements, at every scale), every line style, both
		// color modes.
		frame.style.cursorRadiusPx = std::array{2.0F, 6.0F, 7.0F}[static_cast<std::size_t>(oneOf(3))];
		frame.style.guideLineStyle = static_cast<assets::GuideLineStyle>(oneOf(3));
		frame.style.guideColorMode = chance(0.5F) ? assets::GuideColorMode::One : assets::GuideColorMode::BySource;
		frame.style.guideLabelPx = chance(0.5F) ? 11.0F : 14.0F;
		frame.tool = chance(0.5F) ? world_sim::ToolKind::Wall : world_sim::ToolKind::Foundation;
		frame.zoom = between(1.0F, 12.0F);
		// Across the screen and a little past its edges, so labels meet the edges too.
		const float halfWidthMeters = static_cast<float>(kWidth) / (2.0F * kPixelsPerMeter * frame.zoom);
		const float halfHeightMeters = static_cast<float>(kHeight) / (2.0F * kPixelsPerMeter * frame.zoom);
		const auto	onScreen = [&] {
			 return Foundation::Vec2{between(-1.1F, 1.1F) * halfWidthMeters, between(-1.1F, 1.1F) * halfHeightMeters};
		};
		frame.cursor = onScreen();
		const int pointCount = oneOf(5);
		for (int point = 0; point < pointCount; ++point) {
			frame.points.push_back(
				chance(0.3F) ? Foundation::Vec2{frame.cursor.x + between(-3.0F, 3.0F), frame.cursor.y + between(-3.0F, 3.0F)} : onScreen()
			);
		}
		frame.snap.kind = static_cast<SnapKind>(oneOf(static_cast<int>(SnapKind::GuideCrossing) + 1));
		frame.snap.point = frame.cursor;
		const int guideCount = oneOf(3);
		for (int guideIndex = 0; guideIndex < guideCount; ++guideIndex) {
			SnapGuide guide;
			guide.from = pointCount > 0 && chance(0.5F) ? frame.points[static_cast<std::size_t>(oneOf(pointCount))] : onScreen();
			// Near the pixel thresholds: a node a pixel or two off the first guide's (a
			// duplicate or not), or about a pixel from the snapped point (a stroke too short).
			const float metersPerPixel = 1.0F / (kPixelsPerMeter * frame.zoom);
			const float angleOff = between(0.0F, 2.0F * std::numbers::pi_v<float>);
			const auto	pixelsOff = [&](Foundation::Vec2 around, float pixels) {
				 return Foundation::Vec2{around.x + std::cos(angleOff) * pixels * metersPerPixel, around.y + std::sin(angleOff) * pixels * metersPerPixel};
			};
			if (guideIndex == 1 && chance(0.4F)) {
				guide.from = pixelsOff(frame.snap.guides[0].from, between(0.0F, 2.5F));
			} else if (chance(0.15F)) {
				guide.from = pixelsOff(frame.cursor, between(0.0F, 2.0F));
			}
			guide.to = frame.cursor;
			guide.source = static_cast<GuideSource>(oneOf(3));
			guide.relation = static_cast<GuideRelation>(oneOf(3));
			guide.rightAngle = chance(0.5F);
			const float angle = between(0.0F, 2.0F * std::numbers::pi_v<float>);
			guide.rightAngleEdgeDirection = {std::cos(angle), std::sin(angle)};
			guide.distanceFromNodeMeters = std::hypot(guide.to.x - guide.from.x, guide.to.y - guide.from.y);
			frame.snap.guides[frame.snap.guideCount++] = guide;
		}
		frame.closeRadiusMeters = between(0.1F, 2.0F);
		frame.canClose = chance(0.5F);
		frame.willClose = frame.canClose && chance(0.5F);
		frame.guideDistances = chance(0.8F);
		if (chance(0.3F)) {
			// Where the preview puts its rejection reason: below and right of the snapped point.
			camera.setZoom(frame.zoom);
			const Foundation::Vec2 tip = camera.worldToScreen(frame.cursor.x, frame.cursor.y, kWidth, kHeight, kPixelsPerMeter);
			const float left = chance(0.5F) ? tip.x + between(10.0F, 30.0F) : between(0.0F, static_cast<float>(kWidth));
			const float top = chance(0.5F) ? tip.y + between(10.0F, 30.0F) : between(0.0F, static_cast<float>(kHeight));
			frame.keepClear = FeedbackBox{left, top, left + between(40.0F, 200.0F), top + between(10.0F, 30.0F)};
		}
		frames.push_back(std::move(frame));
	}

	const auto planFor = [&](const Frame& frame, float frameUiScale) {
		style = frame.style;
		tool = frame.tool;
		snap = frame.snap;
		points = frame.points;
		cursor = frame.cursor;
		closeRadiusMeters = frame.closeRadiusMeters;
		canClose = frame.canClose;
		willClose = frame.willClose;
		display.guideDistances = frame.guideDistances;
		keepClear = frame.keepClear;
		viewportWidth = kWidth;
		viewportHeight = kHeight;
		camera.setZoom(frame.zoom);
		uiScale = frameUiScale;
		return plan();
	};

	const auto expectSameDash = [](const std::optional<Foundation::DashPattern>& actual, const std::optional<Foundation::DashPattern>& expected) {
		ASSERT_EQ(actual.has_value(), expected.has_value());
		if (actual) {
			EXPECT_EQ(actual->dash, expected->dash);
			EXPECT_EQ(actual->gap, expected->gap);
		}
	};
	const auto ringBox = [](const FeedbackRing& ring) {
		return boxAround(ring.center, ring.radius + ring.width * 0.5F);
	};
	const auto paddingOf = [](const FeedbackLabel& label) {
		return Foundation::Vec2{label.position.x - label.plate.left, label.position.y - label.plate.top};
	};

	std::size_t comparedLabels = 0;
	std::size_t placementChecks = 0;
	std::size_t withOneGuide = 0;
	std::size_t withTwoGuides = 0;
	std::size_t withHalo = 0;
	std::size_t withCloseRing = 0;
	std::size_t withWallRing = 0;
	std::size_t withKeepClear = 0;
	for (std::size_t index = 0; index < frames.size(); ++index) {
		const Frame&			  frame = frames[index];
		const DrawingFeedbackPlan reference = planFor(frame, 1.0F);
		withOneGuide += frame.snap.guideCount == 1 ? 1 : 0;
		withTwoGuides += frame.snap.guideCount == 2 ? 1 : 0;
		withHalo += ringsIn(reference, FeedbackLayer::Marker).empty() ? 0 : 1;
		withCloseRing += ringsIn(reference, FeedbackLayer::Alignment).empty() ? 0 : 1;
		withWallRing += ringsIn(reference, FeedbackLayer::Target).empty() ? 0 : 1;
		withKeepClear += frame.keepClear ? 1 : 0;
		for (const float scale : {0.6F, 0.8F, 1.25F, 1.5F, 2.0F, 3.0F}) {
			SCOPED_TRACE(::testing::Message() << "scale " << scale << ", frame " << index);
			const DrawingFeedbackPlan scaled = planFor(frame, scale);

			ASSERT_EQ(scaled.strokes.size(), reference.strokes.size());
			for (std::size_t item = 0; item < scaled.strokes.size(); ++item) {
				const FeedbackStroke& actual = scaled.strokes[item];
				const FeedbackStroke& expected = reference.strokes[item];
				ASSERT_EQ(actual.pointCount, expected.pointCount);
				EXPECT_EQ(actual.layer, expected.layer);
				for (std::size_t point = 0; point < actual.pointCount; ++point) {
					EXPECT_EQ(actual.points[point].x, expected.points[point].x);
					EXPECT_EQ(actual.points[point].y, expected.points[point].y);
				}
				EXPECT_EQ(actual.width, expected.width);
				expectSameDash(actual.dash, expected.dash);
			}
			ASSERT_EQ(scaled.rings.size(), reference.rings.size());
			for (std::size_t item = 0; item < scaled.rings.size(); ++item) {
				const FeedbackRing& actual = scaled.rings[item];
				const FeedbackRing& expected = reference.rings[item];
				EXPECT_EQ(actual.layer, expected.layer);
				EXPECT_EQ(actual.center.x, expected.center.x);
				EXPECT_EQ(actual.center.y, expected.center.y);
				EXPECT_EQ(actual.radius, expected.radius);
				EXPECT_EQ(actual.width, expected.width);
				expectSameDash(actual.dash, expected.dash);
			}
			ASSERT_EQ(scaled.dots.size(), reference.dots.size());
			for (std::size_t item = 0; item < scaled.dots.size(); ++item) {
				const FeedbackDot& actual = scaled.dots[item];
				const FeedbackDot& expected = reference.dots[item];
				EXPECT_EQ(actual.layer, expected.layer);
				EXPECT_EQ(actual.center.x, expected.center.x);
				EXPECT_EQ(actual.center.y, expected.center.y);
				EXPECT_EQ(actual.radius, expected.radius);
				EXPECT_EQ(actual.outlineWidth, expected.outlineWidth);
			}

			// Labels in plan order, matched by text: a bigger label can be dropped where a
			// smaller one fits, so the lists can differ in length.
			std::size_t next = 0;
			for (const FeedbackLabel& actual : scaled.labels) {
				const auto match = std::find_if(reference.labels.begin() + static_cast<std::ptrdiff_t>(next), reference.labels.end(), [&](const FeedbackLabel& label) {
					return label.text == actual.text;
				});
				if (match == reference.labels.end()) {
					continue;
				}
				next = static_cast<std::size_t>(std::distance(reference.labels.begin(), match)) + 1;
				EXPECT_EQ(actual.sizePx, match->sizePx * scale);
				EXPECT_EQ(actual.plateCornerRadius, match->plateCornerRadius * scale);
				const Foundation::Vec2 padding = paddingOf(actual);
				const Foundation::Vec2 expectedPadding = paddingOf(*match);
				EXPECT_NEAR(padding.x, expectedPadding.x * scale, 1e-3F);
				EXPECT_NEAR(padding.y, expectedPadding.y * scale, 1e-3F);
				++comparedLabels;
			}

			// The placement rules: every plate inside the viewport; away from the screen edges,
			// off the marker, the halo, the wall ring, and the keep-clear box.
			const Foundation::Vec2 tip = screen(frame.cursor);
			const bool			   awayFromEdges = tip.x >= kAwayFromEdgesPx && tip.y >= kAwayFromEdgesPx &&
										 tip.x <= static_cast<float>(kWidth) - kAwayFromEdgesPx &&
										 tip.y <= static_cast<float>(kHeight) - kAwayFromEdgesPx;
			std::vector<FeedbackBox> keepOff;
			if (awayFromEdges) {
				// The marker's ticks and dot with their outlines, at this frame's radius (floored at 4 px).
				const float radius = std::max(4.0F, frame.style.cursorRadiusPx);
				const float reach = radius + 10.0F + 3.0F;
				keepOff = {
					{tip.x - reach, tip.y - 3.0F, tip.x + reach, tip.y + 3.0F},
					{tip.x - 3.0F, tip.y - reach, tip.x + 3.0F, tip.y + reach},
					boxAround(tip, radius + 1.25F),
				};
				for (const FeedbackRing& ring : ringsIn(scaled, FeedbackLayer::Marker)) {
					keepOff.push_back(ringBox(ring));
				}
				for (const FeedbackRing& ring : ringsIn(scaled, FeedbackLayer::Target)) {
					keepOff.push_back(ringBox(ring));
				}
				if (frame.keepClear) {
					keepOff.push_back(*frame.keepClear);
				}
			}
			for (const FeedbackLabel& label : scaled.labels) {
				expectInside(label.plate);
				for (const FeedbackBox& obstacle : keepOff) {
					EXPECT_FALSE(overlaps(label.plate, obstacle)) << "label " << label.text << " on an obstacle";
				}
				placementChecks += awayFromEdges ? 1 : 0;
			}
		}
	}
	EXPECT_GT(comparedLabels, 1000U);
	EXPECT_GT(placementChecks, 300U);
	EXPECT_GT(withOneGuide, 100U);
	EXPECT_GT(withTwoGuides, 100U);
	EXPECT_GT(withHalo, 50U);
	EXPECT_GT(withCloseRing, 50U);
	EXPECT_GT(withWallRing, 50U);
	EXPECT_GT(withKeepClear, 100U);

	// A label beside a line keeps its plate 7 px off it at every scale, its text 10 px off at
	// 100%: the gap is the plate's, as at the snapped end, so a growing plate never closes on it.
	viewportWidth = kViewportWidth;
	viewportHeight = kViewportHeight;
	camera.setZoom(kZoom);
	style = assets::PreviewStyle{};
	tool = world_sim::ToolKind::Foundation;
	keepClear.reset();
	canClose = false;
	willClose = false;
	display.guideDistances = true;
	points = {{0.0F, 0.0F}};
	cursor = {0.0F, 3.0F};
	snap = {};
	addGuide({.from = {6.0F, 3.0F}, .to = cursor, .source = GuideSource::WorldAxes});
	const float lineY = screen(cursor).y;
	for (const float scale : {0.6F, 1.0F, 1.5F, 2.0F, 3.0F}) {
		SCOPED_TRACE(::testing::Message() << "scale " << scale);
		uiScale = scale;
		const DrawingFeedbackPlan beside = plan();
		const auto guideLabel = std::find_if(beside.labels.begin(), beside.labels.end(), [](const FeedbackLabel& label) {
			return label.text == "6.00 m";
		});
		ASSERT_NE(guideLabel, beside.labels.end());
		const float plateGap = std::min(std::abs(guideLabel->plate.top - lineY), std::abs(guideLabel->plate.bottom - lineY));
		EXPECT_NEAR(plateGap, 7.0F, 1e-3F);
		if (scale == 1.0F) {
			const float textTop = guideLabel->position.y;
			const float textBottom = textTop + kLineHeightOfSize * guideLabel->sizePx;
			EXPECT_NEAR(std::min(std::abs(textTop - lineY), std::abs(textBottom - lineY)), 10.0F, 1e-3F);
		}
	}
}

namespace {

	// The Rectangle tool's look, from D13's Preview.
	constexpr float kGridCrossArmPx = 5.0F;
	constexpr float kGridCrossWidthPx = 1.25F;
	constexpr float kGridCrossOpacity = 0.55F;
	constexpr float kSizeLabelPlateGapPx = 9.0F; // the plate's corner off the snapped corner, diagonally

	// The Rectangle tool with a first corner 4 m left of and 3 m below the snapped corner, which
	// sits at `snappedScreen`: a 4 m x 3 m rectangle.
	class RectangleFeedbackTest : public DrawingFeedbackTest {
	  protected:
		void SetUp() override {
			DrawingFeedbackTest::SetUp();
			tool = world_sim::ToolKind::FoundationRectangle;
			style.guideColorMode = assets::GuideColorMode::BySource;
		}

		void cornerAt(Foundation::Vec2 snappedScreen, Foundation::Vec2 towardFirstCorner = {-4.0F, -3.0F}) {
			cursor = worldAt(snappedScreen);
			points = {{cursor.x + towardFirstCorner.x, cursor.y + towardFirstCorner.y}};
			sizeLabel = construction::rectangleFromCorners(points.front(), cursor)->label;
		}

		// The two strokes of a cross centered on `center`, if the plan has them.
		static std::vector<FeedbackStroke> crossAt(const DrawingFeedbackPlan& result, Foundation::Vec2 center) {
			std::vector<FeedbackStroke> found;
			for (const FeedbackStroke& stroke : result.strokes) {
				const Foundation::Vec2 middle{
					(stroke.points[0].x + stroke.points[1].x) * 0.5F, (stroke.points[0].y + stroke.points[1].y) * 0.5F
				};
				if (stroke.pointCount == 2 && std::hypot(middle.x - center.x, middle.y - center.y) < 1e-3F) {
					found.push_back(stroke);
				}
			}
			return found;
		}

		// The plan's one label, the size label.
		FeedbackLabel plannedSizeLabel() {
			const DrawingFeedbackPlan result = plan();
			EXPECT_EQ(result.labels.size(), 1U);
			return result.labels.empty() ? FeedbackLabel{} : result.labels.front();
		}
	};

	bool lowerRightOf(const FeedbackBox& plate, Foundation::Vec2 point) {
		return plate.left >= point.x && plate.top >= point.y;
	}

	bool insideViewport(const FeedbackBox& plate) {
		return plate.left >= -kViewportTolerancePx && plate.top >= -kViewportTolerancePx &&
			   plate.right <= kViewportWidth + kViewportTolerancePx && plate.bottom <= kViewportHeight + kViewportTolerancePx;
	}

} // namespace

TEST_F(RectangleFeedbackTest, TheGridCrossIsPlannedExactlyWhenTheGridPlacedTheCorner) {
	const Foundation::Vec2 corner{500.0F, 400.0F};
	cornerAt(corner);
	for (const auto kind :
		 {construction::RectangleSnapKind::None, construction::RectangleSnapKind::Vertex, construction::RectangleSnapKind::Edge}) {
		rectangleSnap = kind;
		EXPECT_TRUE(crossAt(plan(), corner).empty()) << static_cast<int>(kind);
	}
	rectangleSnap = construction::RectangleSnapKind::Grid;
	for (const float scale : {0.6F, 1.0F, 2.0F}) {
		SCOPED_TRACE(::testing::Message() << "scale " << scale);
		uiScale = scale;
		const std::vector<FeedbackStroke> cross = crossAt(plan(), corner);
		ASSERT_EQ(cross.size(), 2U);
		for (const FeedbackStroke& arm : cross) {
			const float halfLength = std::hypot(arm.points[1].x - arm.points[0].x, arm.points[1].y - arm.points[0].y) * 0.5F;
			EXPECT_NEAR(halfLength, kGridCrossArmPx, 1e-3F) << "the arms stay in window pixels";
			EXPECT_EQ(arm.width, kGridCrossWidthPx);
			EXPECT_EQ(arm.layer, FeedbackLayer::Alignment) << "under the rectangle, as the prototype draws it";
			EXPECT_FALSE(arm.dash.has_value());
			EXPECT_EQ(arm.color.r, style.guideColorWorld.r);
			EXPECT_EQ(arm.color.g, style.guideColorWorld.g);
			EXPECT_EQ(arm.color.b, style.guideColorWorld.b);
			EXPECT_EQ(arm.color.a, kGridCrossOpacity);
		}
		EXPECT_NE(cross[0].points[0].x == cross[0].points[1].x, cross[1].points[0].x == cross[1].points[1].x) << "one across, one up";
	}
	points.clear();
	EXPECT_EQ(crossAt(plan(), corner).size(), 2U) << "the first corner's grid point too";
}

// The snapped-point marker on the snapped corner, whatever placed it, with the first corner placed
// and before it, over the cross and the rings; its dot red where the press or the rectangle is refused.
TEST_F(RectangleFeedbackTest, TheSnappedPointMarkerSitsOnTheCornerOverTheCrossAndTheRings) {
	const Foundation::Vec2 corner{500.0F, 400.0F};
	cornerAt(corner);
	for (const bool firstCornerPlaced : {true, false}) {
		if (!firstCornerPlaced) {
			points.clear();
			sizeLabel.clear();
		}
		for (const auto kind :
			 {construction::RectangleSnapKind::None,
			  construction::RectangleSnapKind::Vertex,
			  construction::RectangleSnapKind::Edge,
			  construction::RectangleSnapKind::Grid}) {
			rectangleSnap = kind;
			for (const bool refused : {false, true}) {
				SCOPED_TRACE(::testing::Message() << "kind " << static_cast<int>(kind) << ", first corner " << firstCornerPlaced << ", refused " << refused);
				snappedPointInvalid = refused;
				const DrawingFeedbackPlan result = plan();
				const auto				  dots = dotsIn(result, FeedbackLayer::Cursor);
				ASSERT_EQ(dots.size(), 1U);
				expectNear(dots[0].center, corner);
				EXPECT_FLOAT_EQ(dots[0].fill.r, refused ? invalidColor.r : 1.0F);
				EXPECT_FLOAT_EQ(dots[0].fill.g, refused ? invalidColor.g : 1.0F);
				EXPECT_EQ(strokesIn(result, FeedbackLayer::Cursor).size(), 4U);
				EXPECT_EQ(strokesIn(result, FeedbackLayer::CursorOutline).size(), 4U);
				for (const FeedbackRing& ring : result.rings) {
					EXPECT_LT(ring.layer, FeedbackLayer::CursorOutline) << "the ring under the marker";
				}
				const std::vector<FeedbackStroke> cross = crossAt(result, corner);
				EXPECT_EQ(cross.size(), kind == construction::RectangleSnapKind::Grid ? 2U : 0U);
				for (const FeedbackStroke& arm : cross) {
					EXPECT_LT(arm.layer, FeedbackLayer::CursorOutline) << "the cross under the marker";
				}
			}
		}
	}
}

// The prototype's rings: amber 8 px round an existing corner, cyan 7 px on an existing edge.
TEST_F(RectangleFeedbackTest, AnExistingCornerOrEdgeIsRingedAndNothingElseIs) {
	const Foundation::Vec2 corner{500.0F, 400.0F};
	cornerAt(corner);
	rectangleSnap = construction::RectangleSnapKind::Vertex;
	std::vector<FeedbackRing> rings = plan().rings;
	ASSERT_EQ(rings.size(), 1U);
	EXPECT_EQ(rings[0].layer, FeedbackLayer::Marker) << "under the marker, as the prototype draws it";
	EXPECT_NEAR(rings[0].center.x, corner.x, 1e-3F);
	EXPECT_NEAR(rings[0].center.y, corner.y, 1e-3F);
	EXPECT_EQ(rings[0].radius, 8.0F);
	EXPECT_EQ(rings[0].width, 2.0F);
	EXPECT_EQ(rings[0].color.r, style.snapVertexColor.r);
	EXPECT_EQ(rings[0].color.b, style.snapVertexColor.b);

	rectangleSnap = construction::RectangleSnapKind::Edge;
	rings = plan().rings;
	ASSERT_EQ(rings.size(), 1U);
	EXPECT_EQ(rings[0].radius, 7.0F);
	EXPECT_EQ(rings[0].color.r, style.snapEdgeColor.r);
	EXPECT_EQ(rings[0].color.b, style.snapEdgeColor.b);

	for (const auto kind : {construction::RectangleSnapKind::None, construction::RectangleSnapKind::Grid}) {
		rectangleSnap = kind;
		EXPECT_TRUE(plan().rings.empty()) << static_cast<int>(kind);
	}
}

// Beside the snapped corner, where the pointer sits: up right, its plate 9 px off diagonally,
// whichever way the rectangle runs from its first corner, and never down right, where the
// pointer's arrow hangs. No rectangle, no label.
TEST_F(RectangleFeedbackTest, TheSizeLabelSitsUpRightOfTheCursorWhicheverWayTheRectangleRuns) {
	const Foundation::Vec2 corner{500.0F, 400.0F};
	for (const Foundation::Vec2 towardFirstCorner : {Foundation::Vec2{-4.0F, -3.0F}, {4.0F, -3.0F}, {4.0F, 3.0F}, {-4.0F, 3.0F}}) {
		SCOPED_TRACE(::testing::Message() << towardFirstCorner.x << ", " << towardFirstCorner.y);
		cornerAt(corner, towardFirstCorner);
		for (const auto kind : {construction::RectangleSnapKind::Grid, construction::RectangleSnapKind::None}) {
			rectangleSnap = kind;
			const FeedbackLabel label = plannedSizeLabel();
			EXPECT_EQ(label.text, "4 m x 3 m");
			EXPECT_NEAR(label.plate.left - corner.x, kSizeLabelPlateGapPx, 1e-3F);
			EXPECT_NEAR(corner.y - label.plate.bottom, kSizeLabelPlateGapPx, 1e-3F);
			EXPECT_FALSE(lowerRightOf(label.plate, corner));
		}
	}
	sizeLabel.clear(); // the corners span no rectangle
	EXPECT_TRUE(plan().labels.empty());
}

// Near each screen edge the label takes the first of up right, up left, and down left whose plate
// fits, and stays on the screen and off the pointer's lower right.
TEST_F(RectangleFeedbackTest, TheSizeLabelStaysOnScreenNearEachEdge) {
	rectangleSnap = construction::RectangleSnapKind::Grid;
	const struct {
		const char*		 where;
		Foundation::Vec2 corner;
		int				 sideX; // the plate's side of the corner: 1 right, -1 left
		int				 sideY; // 1 below, -1 above
	} cases[] = {
		{"right edge", {990.0F, 400.0F}, -1, -1},
		{"top edge", {500.0F, 10.0F}, -1, 1},
		{"top-right corner", {990.0F, 10.0F}, -1, 1},
		{"bottom edge", {500.0F, 795.0F}, 1, -1},
		{"left edge", {5.0F, 400.0F}, 1, -1},
		{"bottom-right corner", {990.0F, 795.0F}, -1, -1},
	};
	for (const auto& place : cases) {
		SCOPED_TRACE(place.where);
		cornerAt(place.corner);
		const FeedbackLabel label = plannedSizeLabel();
		EXPECT_TRUE(insideViewport(label.plate));
		EXPECT_FALSE(lowerRightOf(label.plate, place.corner));
		const Foundation::Vec2 middle = centerOf(label.plate);
		EXPECT_EQ(signOf(middle.x - place.corner.x), place.sideX);
		EXPECT_EQ(signOf(middle.y - place.corner.y), place.sideY);
	}
}

// The label's text and plate follow the interface size and its 9 px gap does not; the cross and
// the ring stay in window pixels.
TEST_F(RectangleFeedbackTest, TheSizeLabelFollowsTheInterfaceSizeAndNothingElseDoes) {
	const Foundation::Vec2 corner{500.0F, 400.0F};
	cornerAt(corner);
	rectangleSnap = construction::RectangleSnapKind::Grid;
	const DrawingFeedbackPlan atOne = plan();
	ASSERT_EQ(atOne.labels.size(), 1U);
	const float sizeAtOne = atOne.labels[0].sizePx;
	for (const float scale : {0.6F, 1.2F, 2.0F}) {
		SCOPED_TRACE(::testing::Message() << "scale " << scale);
		uiScale = scale;
		const DrawingFeedbackPlan scaled = plan();
		ASSERT_EQ(scaled.labels.size(), 1U);
		const FeedbackLabel& label = scaled.labels[0];
		EXPECT_FLOAT_EQ(label.sizePx, sizeAtOne * scale);
		EXPECT_NEAR(label.plate.bottom - label.plate.top, (kLineHeightOfSize * label.sizePx) + 2.0F * kPlatePaddingPx * scale, 1e-3F);
		EXPECT_NEAR(label.plate.left - corner.x, kSizeLabelPlateGapPx, 1e-3F);
		EXPECT_NEAR(corner.y - label.plate.bottom, kSizeLabelPlateGapPx, 1e-3F);
		ASSERT_EQ(scaled.strokes.size(), atOne.strokes.size());
		for (std::size_t index = 0; index < atOne.strokes.size(); ++index) {
			EXPECT_EQ(scaled.strokes[index].width, atOne.strokes[index].width);
			for (std::size_t point = 0; point < atOne.strokes[index].pointCount; ++point) {
				EXPECT_EQ(scaled.strokes[index].points[point].x, atOne.strokes[index].points[point].x);
				EXPECT_EQ(scaled.strokes[index].points[point].y, atOne.strokes[index].points[point].y);
			}
		}
	}
	rectangleSnap = construction::RectangleSnapKind::Vertex;
	uiScale = 2.0F;
	ASSERT_EQ(plan().rings.size(), 1U);
	EXPECT_EQ(plan().rings[0].radius, 8.0F);
}
