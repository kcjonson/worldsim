#include "DrawingFeedback.h"

#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <primitives/Primitives.h>
#include <world/camera/WorldCamera.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <span>
#include <tuple>

namespace world_sim {

	namespace {

		using construction::GuideRelation;
		using construction::GuideSource;
		using construction::SnapGuide;
		using construction::SnapKind;

		// Sizes from architecture D16, "Guide rendering look".
		constexpr float kMarkerLineWidthPx = 1.5F;
		constexpr float kCloseRingOpacityOfGuide = 0.7F;
		constexpr float kCloseRingWidthPx = 1.0F;
		constexpr float kCloseHaloWidthPx = 3.0F;
		constexpr float kCursorMinimumRadiusPx = 4.0F;
		constexpr float kCursorOutlineWidthPx = 2.5F;
		constexpr float kCursorTickGapPx = 5.0F;	// from the dot's edge to its tick: 11 px at the default radius
		constexpr float kCursorTickLengthPx = 5.0F; // to 16 px at the default radius
		constexpr float kCursorTickWidthPx = 2.0F;
		constexpr float kCursorTickOutlineWidthPx = 6.0F;
		constexpr float kSegmentLabelPx = 12.0F;
		constexpr float kSegmentLabelOffsetPx = 12.0F; // the label's near corner, diagonally off the snapped point
		constexpr float kLabelGapPx = 10.0F;		   // a guide label off its guide, a long segment's label off the segment
		constexpr float kGuideLabelMinimumGuidePx = 40.0F;
		constexpr float kGuideLabelShiftPaddingPx = 8.0F;
		constexpr int	kGuideLabelShiftAttempts = 6;

		// From the prototype's draw code.
		constexpr float kLabelBoxPaddingPx = 2.0F;
		constexpr float kLabelHaloSpreadPx = 1.75F;
		constexpr float kLabelHaloBlurPx = 0.75F;
		const Foundation::Color kLabelHaloColor{10.0F / 255.0F, 12.0F / 255.0F, 16.0F / 255.0F, 0.85F};
		const Foundation::Color kSegmentLabelColor{0xE3 / 255.0F, 0xE7 / 255.0F, 0xEE / 255.0F, 1.0F};
		const Foundation::Color kCursorFillColor{1.0F, 1.0F, 1.0F, 1.0F};
		const Foundation::Color kCursorOutlineColor{0.0F, 0.0F, 0.0F, 1.0F};

		// The operating system's pointer hangs down and right of its hot spot, about this far
		// at the desktop's scaling. A segment label placed at the segment's middle has to stay
		// at least this far from the snapped point, which the pointer sits on or near.
		constexpr float kSegmentLabelCursorClearancePx = 40.0F;

		// A strip shorter than this on screen draws nothing.
		constexpr float kMinimumStrokePx = 0.5F;

		// The dotted ring is a closed polyline with a segment about this long.
		constexpr float kRingSegmentPx = 3.0F;
		constexpr int	kRingMinimumSegments = 24;
		constexpr int	kRingMaximumSegments = 360;

		static_assert(construction::kMaxSnapGuides == 2, "guide labels rank guides[0] above guides[1]");

		struct Box {
			float left;
			float top;
			float right;
			float bottom;
		};

		bool overlaps(const Box& first, const Box& second) {
			return first.left < second.right && second.left < first.right && first.top < second.bottom && second.top < first.bottom;
		}

		float distanceToBox(Foundation::Vec2 point, const Box& box) {
			const float dx = std::max({box.left - point.x, 0.0F, point.x - box.right});
			const float dy = std::max({box.top - point.y, 0.0F, point.y - box.bottom});
			return std::hypot(dx, dy);
		}

		Box labelBox(Foundation::Vec2 topLeft, float width, float height) {
			return {
				topLeft.x - kLabelBoxPaddingPx,
				topLeft.y - kLabelBoxPaddingPx,
				topLeft.x + width + kLabelBoxPaddingPx,
				topLeft.y + height + kLabelBoxPaddingPx,
			};
		}

		// Top-left of a label whose near edge sits at `anchor`, extending along `side`.
		Foundation::Vec2 labelBeside(Foundation::Vec2 anchor, Foundation::Vec2 side, float width, float height) {
			return {side.x < 0.0F ? anchor.x - width : anchor.x, side.y < 0.0F ? anchor.y - height : anchor.y};
		}

		float length(Foundation::Vec2 v) {
			return std::hypot(v.x, v.y);
		}

		float dot(Foundation::Vec2 a, Foundation::Vec2 b) {
			return a.x * b.x + a.y * b.y;
		}

		Foundation::Color withOpacity(const assets::StyleColor& color, float opacity) {
			return {color.r, color.g, color.b, opacity};
		}

		const assets::StyleColor& guideColor(const assets::PreviewStyle& style, GuideSource source) {
			if (style.guideColorMode == assets::GuideColorMode::One) {
				return style.guideColor;
			}
			switch (source) {
				case GuideSource::DrawnShape:
					return style.guideColorShape;
				case GuideSource::NearbyStructure:
					return style.guideColorNearby;
				case GuideSource::WorldAxes:
					return style.guideColorWorld;
			}
			return style.guideColor;
		}

		std::optional<Foundation::DashPattern> alignmentDash(const assets::PreviewStyle& style) {
			switch (style.guideLineStyle) {
				case assets::GuideLineStyle::Solid:
					return std::nullopt;
				case assets::GuideLineStyle::Dashed:
					return Foundation::DashPattern{.dash = style.guideDashPx, .gap = style.guideGapPx, .phase = 0.0F, .cap = Foundation::DashCap::Butt};
				case assets::GuideLineStyle::Dotted:
					return Foundation::DashPattern{.dash = style.guideDashPx, .gap = style.guideGapPx, .phase = 0.0F, .cap = Foundation::DashCap::Round};
			}
			return std::nullopt;
		}

		std::string metres(float meters, int decimals) {
			char text[32];
			std::snprintf(text, sizeof(text), "%.*f m", std::clamp(decimals, 0, 6), static_cast<double>(meters));
			return text;
		}

		FeedbackStroke segmentStroke(Foundation::Vec2 start, Foundation::Vec2 end, Foundation::Color color, float width, FeedbackLayer layer) {
			FeedbackStroke stroke{.color = color, .width = width, .layer = layer};
			stroke.points[0] = start;
			stroke.points[1] = end;
			stroke.pointCount = 2;
			return stroke;
		}

		class Planner {
		  public:
			Planner(const DrawingFeedbackInput& input, const MeasureTextWidth& measure)
				: input(input),
				  style(input.style),
				  measure(measure),
				  screenPixelsPerMeter(input.pixelsPerMeter * input.camera.zoom()) {}

			DrawingFeedbackPlan plan() {
				planCloseRing();
				for (std::size_t i = 0; i < input.snap.guideCount; ++i) {
					planGuide(input.snap.guides[i]);
				}
				planCrossingDot();
				planCloseHalo();
				planSegmentLabel();
				if (input.display.guideDistances) {
					for (std::size_t i = 0; i < input.snap.guideCount; ++i) {
						planGuideLabel(input.snap.guides[i]);
					}
				}
				planCursor();
				return std::move(result);
			}

		  private:
			const DrawingFeedbackInput& input;
			const assets::PreviewStyle& style;
			const MeasureTextWidth&		measure;
			float						screenPixelsPerMeter;
			DrawingFeedbackPlan			result;
			std::vector<Box>			placedLabels;

			Foundation::Vec2 toScreen(Foundation::Vec2 world) const {
				return input.camera.worldToScreen(world.x, world.y, input.viewportWidth, input.viewportHeight, input.pixelsPerMeter);
			}

			void planCloseRing() {
				if (!input.canClose || input.points.empty()) {
					return;
				}
				result.rings.push_back({
					.center = toScreen(input.points.front()),
					.radius = input.closeRadiusMeters * screenPixelsPerMeter,
					.color = withOpacity(style.originHaloColor, style.guideOpacity * kCloseRingOpacityOfGuide),
					.width = kCloseRingWidthPx,
					.dash = alignmentDash(style),
					.layer = FeedbackLayer::Alignment,
				});
			}

			void planGuide(const SnapGuide& guide) {
				const Foundation::Vec2 from = toScreen(guide.from);
				const Foundation::Vec2 to = toScreen(guide.to);
				const Foundation::Vec2 along{to.x - from.x, to.y - from.y};
				const float			   alongLength = length(along);
				if (alongLength < kMinimumStrokePx) {
					return;
				}
				const assets::StyleColor& color = guideColor(style, guide.source);

				FeedbackStroke stroke = segmentStroke(from, to, withOpacity(color, style.guideOpacity), style.guideWidth, FeedbackLayer::Alignment);
				stroke.dash = alignmentDash(style);
				result.strokes.push_back(stroke);

				if (!guide.rightAngle) {
					return;
				}
				const Foundation::Vec2 edgeTip =
					toScreen({guide.from.x + guide.rightAngleEdgeDirection.x, guide.from.y + guide.rightAngleEdgeDirection.y});
				const Foundation::Vec2 edge{edgeTip.x - from.x, edgeTip.y - from.y};
				const float			   edgeLength = length(edge);
				if (edgeLength <= 0.0F) {
					return;
				}
				const float			   size = style.rightAngleMarkerPx;
				const Foundation::Vec2 edgeLeg{edge.x / edgeLength * size, edge.y / edgeLength * size};
				const Foundation::Vec2 guideLeg{along.x / alongLength * size, along.y / alongLength * size};
				FeedbackStroke		   marker{
							.color = withOpacity(color, style.markerOpacity), .width = kMarkerLineWidthPx, .layer = FeedbackLayer::Marker
				};
				marker.points[0] = {from.x + edgeLeg.x, from.y + edgeLeg.y};
				marker.points[1] = {from.x + edgeLeg.x + guideLeg.x, from.y + edgeLeg.y + guideLeg.y};
				marker.points[2] = {from.x + guideLeg.x, from.y + guideLeg.y};
				marker.pointCount = 3;
				result.strokes.push_back(marker);
			}

			void planCrossingDot() {
				if (input.snap.kind != SnapKind::GuideCrossing) {
					return;
				}
				const GuideSource source = input.snap.guideCount > 0 ? input.snap.guides[0].source : GuideSource::DrawnShape;
				result.dots.push_back({
					.center = toScreen(input.snap.point),
					.radius = style.guideCrossingDotPx,
					.fill = withOpacity(guideColor(style, source), style.markerOpacity),
					.layer = FeedbackLayer::Marker,
				});
			}

			void planCloseHalo() {
				if (!input.willClose || input.points.empty()) {
					return;
				}
				result.rings.push_back({
					.center = toScreen(input.points.front()),
					.radius = std::max(style.originHaloMinRadiusPx, input.closeRadiusMeters * screenPixelsPerMeter * 0.5F),
					.color = withOpacity(guideColor(style, GuideSource::DrawnShape), style.markerOpacity),
					.width = kCloseHaloWidthPx,
					.layer = FeedbackLayer::Marker,
				});
			}

			// A long segment's label sits beside its middle, off the side away from the shape;
			// otherwise it sits at the snapped end, in the quarter round it that points most
			// away from the segment, never the lower right one, where the pointer hangs.
			void planSegmentLabel() {
				if (input.points.empty()) {
					return;
				}
				const Foundation::Vec2 last = toScreen(input.points.back());
				const Foundation::Vec2 tip = toScreen(input.cursor);
				const std::string	   text = metres(
					   std::hypot(input.cursor.x - input.points.back().x, input.cursor.y - input.points.back().y), style.guideDistanceDecimals
				   );
				const float			   width = measure(text, kSegmentLabelPx);
				const Foundation::Vec2 segment{tip.x - last.x, tip.y - last.y};
				const float			   segmentLength = length(segment);

				if (segmentLength > 0.0F) {
					const Foundation::Vec2 middle{(last.x + tip.x) * 0.5F, (last.y + tip.y) * 0.5F};
					const Foundation::Vec2 side = awayFromShape(middle, {-segment.y / segmentLength, segment.x / segmentLength});
					const Foundation::Vec2 topLeft =
						labelBeside({middle.x + side.x * kLabelGapPx, middle.y + side.y * kLabelGapPx}, side, width, kSegmentLabelPx);
					if (distanceToBox(tip, labelBox(topLeft, width, kSegmentLabelPx)) >= kSegmentLabelCursorClearancePx) {
						addLabel(text, topLeft, kSegmentLabelPx, width, kSegmentLabelColor);
						return;
					}
				}

				static const std::array<Foundation::Vec2, 3> kQuarters{{{1.0F, -1.0F}, {-1.0F, -1.0F}, {-1.0F, 1.0F}}};
				const Foundation::Vec2							 towardSegment = segmentLength > 0.0F ? Foundation::Vec2{-segment.x, -segment.y} : Foundation::Vec2{0.0F, 0.0F};
				Foundation::Vec2								 quarter = kQuarters[0];
				for (const Foundation::Vec2& candidate : kQuarters) {
					if (dot(candidate, towardSegment) < dot(quarter, towardSegment)) {
						quarter = candidate;
					}
				}
				const Foundation::Vec2 topLeft{
					quarter.x > 0.0F ? tip.x + kSegmentLabelOffsetPx : tip.x - kSegmentLabelOffsetPx - width,
					quarter.y < 0.0F ? tip.y - kSegmentLabelOffsetPx - kSegmentLabelPx : tip.y + kSegmentLabelOffsetPx,
				};
				addLabel(text, topLeft, kSegmentLabelPx, width, kSegmentLabelColor);
			}

			// `normal` or its reverse, whichever points away from the placed points' centre;
			// with no centre off the line, the one facing up the screen (then left).
			Foundation::Vec2 awayFromShape(Foundation::Vec2 middle, Foundation::Vec2 normal) const {
				if (input.points.size() >= 2) {
					Foundation::Vec2 centre{0.0F, 0.0F};
					for (const Foundation::Vec2& point : input.points) {
						const Foundation::Vec2 screen = toScreen(point);
						centre.x += screen.x;
						centre.y += screen.y;
					}
					const float count = static_cast<float>(input.points.size());
					const float side = dot({centre.x / count - middle.x, centre.y / count - middle.y}, normal);
					if (std::abs(side) > kMinimumStrokePx) {
						return side > 0.0F ? Foundation::Vec2{-normal.x, -normal.y} : normal;
					}
				}
				const bool facesDown = normal.y > 0.0F || (normal.y == 0.0F && normal.x > 0.0F);
				return facesDown ? Foundation::Vec2{-normal.x, -normal.y} : normal;
			}

			// D12's guide distance label: a guide's distance from its own node, beside its
			// middle, off the side away from the segment, moved along the guide when it would
			// cover a label placed before it. A guide through the previous point is the
			// segment itself, whose length the segment label already shows.
			void planGuideLabel(const SnapGuide& guide) {
				if (guide.relation == GuideRelation::AngleStep || guide.throughPreviousPoint) {
					return;
				}
				const Foundation::Vec2 from = toScreen(guide.from);
				const Foundation::Vec2 to = toScreen(guide.to);
				const float			   guideLength = length({to.x - from.x, to.y - from.y});
				if (guideLength < kGuideLabelMinimumGuidePx) {
					return;
				}
				const Foundation::Vec2 along{(to.x - from.x) / guideLength, (to.y - from.y) / guideLength};
				const Foundation::Vec2 middle{(from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F};
				const Foundation::Vec2 segmentMiddle =
					input.points.empty() ? to : Foundation::Vec2{(toScreen(input.points.back()).x + to.x) * 0.5F, (toScreen(input.points.back()).y + to.y) * 0.5F};
				Foundation::Vec2 side{-along.y, along.x};
				if (dot({segmentMiddle.x - middle.x, segmentMiddle.y - middle.y}, side) > 0.0F) {
					side = {-side.x, -side.y};
				}

				const std::string text = metres(guide.distanceFromNodeMeters, style.guideDistanceDecimals);
				const float		  sizePx = style.guideLabelPx;
				const float		  width = measure(text, sizePx);
				for (int attempt = 0; attempt <= kGuideLabelShiftAttempts; ++attempt) {
					const float			   steps = static_cast<float>((attempt + 1) / 2);
					const float			   shift = (attempt % 2 == 0 ? steps : -steps) * (width + kGuideLabelShiftPaddingPx);
					const Foundation::Vec2 anchor{
						middle.x + along.x * shift + side.x * kLabelGapPx,
						middle.y + along.y * shift + side.y * kLabelGapPx,
					};
					const Foundation::Vec2 topLeft = labelBeside(anchor, side, width, sizePx);
					const Box			   box = labelBox(topLeft, width, sizePx);
					const bool covers = std::any_of(placedLabels.begin(), placedLabels.end(), [&](const Box& other) { return overlaps(box, other); });
					if (!covers) {
						addLabel(text, topLeft, sizePx, width, withOpacity(guideColor(style, guide.source), style.markerOpacity));
						return;
					}
				}
			}

			void addLabel(const std::string& text, Foundation::Vec2 topLeft, float sizePx, float width, Foundation::Color color) {
				placedLabels.push_back(labelBox(topLeft, width, sizePx));
				result.labels.push_back({.text = text, .position = topLeft, .sizePx = sizePx, .width = width, .color = color});
			}

			void planCursor() {
				const Foundation::Vec2 at = toScreen(input.cursor);
				const float			   radius = std::max(kCursorMinimumRadiusPx, style.cursorRadiusPx);
				const float			   inner = radius + kCursorTickGapPx;
				const float			   outer = inner + kCursorTickLengthPx;
				static const std::array<Foundation::Vec2, 4> kTickDirections{{{-1.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {0.0F, 1.0F}}};
				for (const auto& [layer, color, width] : {
						 std::tuple{FeedbackLayer::CursorOutline, kCursorOutlineColor, kCursorTickOutlineWidthPx},
						 std::tuple{FeedbackLayer::Cursor, kCursorFillColor, kCursorTickWidthPx},
					 }) {
					for (const Foundation::Vec2& direction : kTickDirections) {
						result.strokes.push_back(segmentStroke(
							{at.x + direction.x * inner, at.y + direction.y * inner}, {at.x + direction.x * outer, at.y + direction.y * outer}, color, width, layer
						));
					}
				}
				result.dots.push_back({
					.center = at,
					.radius = radius,
					.fill = kCursorFillColor,
					.outline = kCursorOutlineColor,
					.outlineWidth = kCursorOutlineWidthPx,
					.layer = FeedbackLayer::Cursor,
				});
			}
		};

		// Each tool's feedback keeps its own layer keys and draw ids: the wall preview draws
		// its band and centerlines on keys above the foundation preview's, and its feedback
		// goes above them.
		struct ToolLayers {
			std::int32_t baseKey;
			const char*	 alignmentId;
			const char*	 markerId;
			const char*	 labelId;
			const char*	 cursorId;
		};

		ToolLayers toolLayers(ToolKind tool) {
			if (tool == ToolKind::Wall) {
				return {911, "wall_guide", "wall_guide_marker", "wall_guide_label", "wall_snap_cursor"};
			}
			return {904, "drawing_guide", "drawing_guide_marker", "drawing_guide_label", "drawing_snap_cursor"};
		}

		std::int32_t layerKey(const ToolLayers& layers, FeedbackLayer layer) {
			return layers.baseKey + static_cast<std::int32_t>(layer);
		}

		const char* layerId(const ToolLayers& layers, FeedbackLayer layer) {
			switch (layer) {
				case FeedbackLayer::Alignment:
					return layers.alignmentId;
				case FeedbackLayer::Marker:
					return layers.markerId;
				case FeedbackLayer::Label:
					return layers.labelId;
				case FeedbackLayer::CursorOutline:
				case FeedbackLayer::Cursor:
					return layers.cursorId;
			}
			return layers.alignmentId;
		}

		void drawRing(const FeedbackRing& ring, const ToolLayers& layers) {
			if (ring.radius <= 0.0F) {
				return;
			}
			if (!ring.dash) {
				Renderer::Primitives::drawCircle({
					.center = ring.center,
					.radius = ring.radius,
					.style = {.fill = {0.0F, 0.0F, 0.0F, 0.0F}, .border = Foundation::BorderStyle{.color = ring.color, .width = ring.width}},
					.id = layerId(layers, ring.layer),
					.key = layerKey(layers, ring.layer),
				});
				return;
			}
			// A circle's border takes no dash, so a dashed ring is a closed polyline round it.
			const float circumference = 2.0F * std::numbers::pi_v<float> * ring.radius;
			const int	segments =
				std::clamp(static_cast<int>(std::ceil(circumference / kRingSegmentPx)), kRingMinimumSegments, kRingMaximumSegments);
			std::vector<Foundation::Vec2> outline;
			outline.reserve(static_cast<std::size_t>(segments));
			for (int i = 0; i < segments; ++i) {
				const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
				outline.push_back({ring.center.x + ring.radius * std::cos(angle), ring.center.y + ring.radius * std::sin(angle)});
			}
			Renderer::Primitives::drawPolyline({
				.points = outline,
				.width = ring.width,
				.color = ring.color,
				.closed = true,
				.dash = ring.dash,
				.id = layerId(layers, ring.layer),
				.key = layerKey(layers, ring.layer),
			});
		}

	} // namespace

	DrawingFeedbackPlan planSnapFeedback(const DrawingFeedbackInput& input, const MeasureTextWidth& measure) {
		return Planner(input, measure).plan();
	}

	void drawSnapFeedback(const DrawingFeedbackInput& input) {
		Renderer::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		const MeasureTextWidth	measure = [fontRenderer](const std::string& text, float sizePx) {
			 return fontRenderer != nullptr ? fontRenderer->MeasureText(text, sizePx / 16.0F).x : 0.0F;
		};
		const DrawingFeedbackPlan plan = planSnapFeedback(input, measure);
		const ToolLayers		  layers = toolLayers(input.tool);

		for (const FeedbackStroke& stroke : plan.strokes) {
			Renderer::Primitives::drawPolyline({
				.points = std::span<const Foundation::Vec2>(stroke.points.data(), stroke.pointCount),
				.width = stroke.width,
				.color = stroke.color,
				.join = Renderer::Primitives::LineJoin::Miter,
				.dash = stroke.dash,
				.id = layerId(layers, stroke.layer),
				.key = layerKey(layers, stroke.layer),
			});
		}
		for (const FeedbackRing& ring : plan.rings) {
			drawRing(ring, layers);
		}
		for (const FeedbackDot& dot : plan.dots) {
			Foundation::CircleStyle style{.fill = dot.fill};
			if (dot.outlineWidth > 0.0F) {
				style.border = Foundation::BorderStyle{.color = dot.outline, .width = dot.outlineWidth};
			}
			Renderer::Primitives::drawCircle({
				.center = dot.center,
				.radius = dot.radius,
				.style = style,
				.id = layerId(layers, dot.layer),
				.key = layerKey(layers, dot.layer),
			});
		}
		for (const FeedbackLabel& label : plan.labels) {
			Renderer::Primitives::drawText({
				.text = label.text,
				.position = label.position,
				.scale = label.sizePx / 16.0F,
				.color = label.color,
				.shadow = Foundation::BoxShadow{.color = kLabelHaloColor, .blur = kLabelHaloBlurPx, .spread = kLabelHaloSpreadPx},
				.id = layerId(layers, FeedbackLayer::Label),
				.key = layerKey(layers, FeedbackLayer::Label),
			});
		}
	}

} // namespace world_sim
