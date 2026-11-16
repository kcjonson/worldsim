#include "DrawingFeedback.h"

#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <world/camera/WorldCamera.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

	using construction::GuideRelation;
	using construction::GuideSource;
	using construction::SnapGuide;
	using construction::SnapKind;
	using world_sim::DrawingFeedbackPlan;
	using world_sim::FeedbackLabel;
	using world_sim::FeedbackLayer;
	using world_sim::FeedbackStroke;

	constexpr float kPixelsPerMeter = 8.0F;
	constexpr float kZoom = 5.0F; // 40 px to the metre, the prototype's scale
	constexpr float kScreenPixelsPerMeter = kPixelsPerMeter * kZoom;

	float fakeWidth(const std::string& text, float sizePx) {
		return 0.6F * sizePx * static_cast<float>(text.size());
	}

	struct Box {
		float left, top, right, bottom;
	};

	Box boxOf(const FeedbackLabel& label) {
		return {label.position.x, label.position.y, label.position.x + label.width, label.position.y + label.sizePx};
	}

	bool overlaps(const Box& a, const Box& b) {
		return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
	}

	float distanceToBox(Foundation::Vec2 point, const Box& box) {
		const float dx = std::max({box.left - point.x, 0.0F, point.x - box.right});
		const float dy = std::max({box.top - point.y, 0.0F, point.y - box.bottom});
		return std::hypot(dx, dy);
	}

	class DrawingFeedbackTest : public ::testing::Test {
	  protected:
		construction::SnapResult	  snap;
		std::vector<Foundation::Vec2> points;
		Foundation::Vec2			  cursor{0.0F, 0.0F};
		world_sim::DrawingSettings	  display;
		assets::PreviewStyle		  style;
		world::WorldCamera			  camera;
		float						  closeRadiusMeters = 0.5F;
		bool						  canClose = false;
		bool						  willClose = false;

		void SetUp() override { camera.setZoom(kZoom); }

		void addGuide(SnapGuide guide) {
			guide.distanceFromNodeMeters = std::hypot(guide.to.x - guide.from.x, guide.to.y - guide.from.y);
			snap.guides[snap.guideCount++] = guide;
		}

		Foundation::Vec2 screen(Foundation::Vec2 world) const { return camera.worldToScreen(world.x, world.y, 1000, 800, kPixelsPerMeter); }

		DrawingFeedbackPlan plan() {
			snap.point = cursor;
			const world_sim::DrawingFeedbackInput input{
				.tool = world_sim::ToolKind::Foundation,
				.snap = snap,
				.points = points,
				.cursor = cursor,
				.display = display,
				.style = style,
				.camera = camera,
				.viewportWidth = 1000,
				.viewportHeight = 800,
				.pixelsPerMeter = kPixelsPerMeter,
				.closeRadiusMeters = closeRadiusMeters,
				.canClose = canClose,
				.willClose = willClose,
			};
			return world_sim::planSnapFeedback(input, fakeWidth);
		}

		static std::vector<FeedbackStroke> strokesIn(const DrawingFeedbackPlan& plan, FeedbackLayer layer) {
			std::vector<FeedbackStroke> found;
			std::copy_if(plan.strokes.begin(), plan.strokes.end(), std::back_inserter(found), [&](const FeedbackStroke& s) { return s.layer == layer; });
			return found;
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

TEST_F(DrawingFeedbackTest, CrossingOfTwoSourcesColoursEachGuideAndDotsThePoint) {
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

	const auto crossing = std::find_if(result.dots.begin(), result.dots.end(), [](const auto& dot) { return dot.layer == FeedbackLayer::Marker; });
	ASSERT_NE(crossing, result.dots.end());
	expectNear(crossing->center, screen(cursor));
	EXPECT_FLOAT_EQ(crossing->radius, style.guideCrossingDotPx);
	expectColor(crossing->fill, style.guideColorNearby, style.markerOpacity);

	ASSERT_EQ(result.labels.size(), 2U);
	EXPECT_EQ(result.labels[0].text, "4.00 m");
	expectColor(result.labels[0].color, style.guideColorNearby, style.markerOpacity);
	EXPECT_EQ(result.labels[1].text, "3.00 m");
	expectColor(result.labels[1].color, style.guideColorWorld, style.markerOpacity);
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
	EXPECT_FALSE(overlaps(boxOf(first), boxOf(second)));
	// The first guide's label keeps its place beside its guide's middle, below it.
	const Foundation::Vec2 middle = screen({-2.0F, 0.0F});
	EXPECT_NEAR(first.position.x, middle.x, 1e-3F);
	EXPECT_NEAR(first.position.y, middle.y + 10.0F, 1e-3F);
	// The second has moved along its guide by at least its own width.
	const float secondMiddleX = screen({-2.0F, 0.1F}).x;
	EXPECT_GT(std::abs(second.position.x - secondMiddleX), second.width);
}

TEST_F(DrawingFeedbackTest, SegmentLabelReadsTheLengthClearOfThePointer) {
	points = {{0.0F, 0.0F}};
	cursor = {1.234F, 0.0F}; // about 49 px: too short for a label at its middle
	const DrawingFeedbackPlan shortSegment = plan();
	ASSERT_EQ(shortSegment.labels.size(), 1U);
	EXPECT_EQ(shortSegment.labels[0].text, "1.23 m");
	const Foundation::Vec2 tip = screen(cursor);
	const Box			   shortBox = boxOf(shortSegment.labels[0]);
	EXPECT_GE(distanceToBox(tip, shortBox), 12.0F);
	EXPECT_FALSE(shortBox.right > tip.x && shortBox.bottom > tip.y) << "the label sits under the pointer";
	EXPECT_GT(shortBox.left, tip.x) << "the segment arrives from the left, so the label goes right";

	style.guideDistanceDecimals = 1;
	EXPECT_EQ(plan().labels[0].text, "1.2 m");

	style.guideDistanceDecimals = 2;
	cursor = {6.0F, 0.0F}; // 240 px: the label sits beside the middle
	const FeedbackLabel longLabel = plan().labels[0];
	EXPECT_EQ(longLabel.text, "6.00 m");
	EXPECT_GE(distanceToBox(screen(cursor), boxOf(longLabel)), 40.0F);
	const Foundation::Vec2 middle = screen({3.0F, 0.0F});
	EXPECT_NEAR(longLabel.position.y + longLabel.sizePx, middle.y - 10.0F, 1e-3F) << "above the segment";
}

TEST_F(DrawingFeedbackTest, AngleStepRayDrawsAStrokeWithoutMarkerOrLabel) {
	display.guideDistances = true;
	points = {{0.0F, 0.0F}, {4.0F, 0.0F}};
	cursor = {4.0F, 3.0F};
	snap.kind = SnapKind::Angle;
	addGuide({.from = points[1], .to = cursor, .source = GuideSource::DrawnShape, .relation = GuideRelation::AngleStep, .throughPreviousPoint = true});

	const DrawingFeedbackPlan result = plan();
	const auto				  guides = strokesIn(result, FeedbackLayer::Alignment);
	ASSERT_EQ(guides.size(), 1U);
	expectNear(guides[0].points[0], screen(points[1]));
	ASSERT_TRUE(guides[0].dash.has_value());
	EXPECT_TRUE(strokesIn(result, FeedbackLayer::Marker).empty());
	ASSERT_EQ(result.labels.size(), 1U);
	EXPECT_EQ(result.labels[0].text, "3.00 m");
}

TEST_F(DrawingFeedbackTest, CloseRingWhileTheShapeCanCloseAndHaloWhenTheClickCloses) {
	points = {{0.0F, 0.0F}, {4.0F, 0.0F}, {4.0F, 3.0F}};
	cursor = {1.0F, 2.0F};
	EXPECT_TRUE(plan().rings.empty());

	canClose = true;
	const DrawingFeedbackPlan open = plan();
	ASSERT_EQ(open.rings.size(), 1U);
	EXPECT_EQ(open.rings[0].layer, FeedbackLayer::Alignment);
	expectNear(open.rings[0].center, screen(points[0]));
	EXPECT_FLOAT_EQ(open.rings[0].radius, closeRadiusMeters * kScreenPixelsPerMeter);
	EXPECT_FLOAT_EQ(open.rings[0].width, 1.0F);
	EXPECT_TRUE(open.rings[0].dash.has_value());
	expectColor(open.rings[0].color, style.originHaloColor, 0.7F * style.guideOpacity);

	willClose = true;
	cursor = points[0];
	const DrawingFeedbackPlan closing = plan();
	ASSERT_EQ(closing.rings.size(), 2U);
	const auto& halo = closing.rings[1];
	EXPECT_EQ(halo.layer, FeedbackLayer::Marker);
	EXPECT_FLOAT_EQ(halo.radius, std::max(style.originHaloMinRadiusPx, closeRadiusMeters * kScreenPixelsPerMeter * 0.5F));
	EXPECT_FLOAT_EQ(halo.width, 3.0F);
	EXPECT_FALSE(halo.dash.has_value());
	expectColor(halo.color, style.guideColor, style.markerOpacity);

	closeRadiusMeters = 0.1F; // 4 px on screen: the halo keeps its floor
	EXPECT_FLOAT_EQ(plan().rings[1].radius, style.originHaloMinRadiusPx);
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
