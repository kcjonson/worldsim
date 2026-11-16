#include "DrawingFeedback.h"

#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <graphics/Rect.h>
#include <primitives/Primitives.h>
#include <world/camera/WorldCamera.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numbers>
#include <span>
#include <tuple>
#include <utility>

namespace world_sim {

	namespace {

		using construction::GuideRelation;
		using construction::GuideSource;
		using construction::kMaxSnapGuides;
		using construction::SnapGuide;
		using construction::SnapKind;

		// Sizes from architecture D16, "Guide rendering look".
		constexpr float kMarkerLineWidthPx = 1.5F;
		constexpr float kCloseRingWidthPx = 1.5F;
		constexpr float kCloseHaloWidthPx = 3.0F;
		constexpr float kCloseHaloTickClearancePx = 6.0F; // the halo's floor: this far outside the marker's ticks
		constexpr float kSnapTargetTickClearancePx = 4.0F; // the wall snap-target ring: this far outside the ticks
		constexpr float kSnapTargetRingWidthPx = 2.0F;
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
		constexpr float kLabelPlatePaddingPx = 3.0F;
		constexpr float kLabelPlateCornerPx = 3.0F;
		// The prototype palette's dark.
		const Foundation::Color kLabelPlateColor{10.0F / 255.0F, 12.0F / 255.0F, 16.0F / 255.0F, 0.7F};
		const Foundation::Color kSegmentLabelColor{0xE3 / 255.0F, 0xE7 / 255.0F, 0xEE / 255.0F, 1.0F};
		const Foundation::Color kCursorFillColor{1.0F, 1.0F, 1.0F, 1.0F};
		const Foundation::Color kCursorOutlineColor{0.0F, 0.0F, 0.0F, 1.0F};

		// The renderer's text scale of 1 is this font size.
		constexpr float kFontBaseSizePx = 16.0F;

		// The operating system's pointer hangs down and right of its hot spot, about this far
		// at the desktop's scaling. A segment label placed at the segment's middle has to stay
		// at least this far from the snapped point, which the pointer sits on or near.
		constexpr float kSegmentLabelCursorClearancePx = 40.0F;

		// Whether the segment label fits at the middle is judged with the width of this length's
		// label (or the real one, when wider), so the label growing a digit at 10 m can't send
		// it back to the end: along a ray the choice changes once.
		constexpr float kSegmentLabelReserveMeters = 999.0F;

		// Placement choices compare unit dot products, whose range is [-1, 1]; a difference
		// within a tenth of that is a tie, broken in a fixed order, so float noise in a
		// direction the snap made exact (45 degrees, an axis) can't flip a label's side.
		constexpr float kSideTieTolerance = 0.2F;

		// The shape's center has to sit this far off the segment's line to pick a side.
		constexpr float kShapeSideMinimumPx = 0.5F;

		// Two guides whose ends both lie within this of each other's draw one stroke and one
		// label; their right-angle markers draw once for each edge direction among them.
		constexpr float kDuplicateGuidePx = 1.0F;
		constexpr float kSameEdgeDirection = 1e-3F; // between unit vectors

		// A guide whose line passes within this of the previous point lies on the segment.
		constexpr float kOnSegmentLinePx = 1.0F;

		// A right-angle marker needs its edge and guide at least this far from parallel
		// (the sine of the angle between them).
		constexpr float kRightAngleMinimumSine = 0.05F;

		// A strip shorter than this on screen draws nothing.
		constexpr float kMinimumStrokePx = 0.5F;

		// The dotted ring is a closed polyline with a segment about this long.
		constexpr float kRingSegmentPx = 3.0F;
		constexpr int	kRingMinimumSegments = 24;
		constexpr int	kRingMaximumSegments = 360;

		// Bounds on what one frame plans: each guide's stroke and marker, the marker's eight
		// ticks; the close ring, halo, and snap-target ring; the marker's and crossing dots;
		// the segment's and each guide's label.
		constexpr std::size_t kMaxStrokes = kMaxSnapGuides * 2 + 8;
		constexpr std::size_t kMaxRings = 3;
		constexpr std::size_t kMaxDots = 2;
		constexpr std::size_t kMaxLabels = 1 + kMaxSnapGuides;

		static_assert(kMaxSnapGuides == 2, "guide labels rank guides[0] above guides[1]");

		using Box = FeedbackBox;

		bool overlaps(const Box& first, const Box& second) {
			return first.left < second.right && second.left < first.right && first.top < second.bottom && second.top < first.bottom;
		}

		float distanceToBox(Foundation::Vec2 point, const Box& box) {
			const float dx = std::max({box.left - point.x, 0.0F, point.x - box.right});
			const float dy = std::max({box.top - point.y, 0.0F, point.y - box.bottom});
			return std::hypot(dx, dy);
		}

		Box boxAround(Foundation::Vec2 center, float halfSize) {
			return {center.x - halfSize, center.y - halfSize, center.x + halfSize, center.y + halfSize};
		}

		Box plateBox(Foundation::Vec2 topLeft, Foundation::Vec2 size) {
			return {
				topLeft.x - kLabelPlatePaddingPx,
				topLeft.y - kLabelPlatePaddingPx,
				topLeft.x + size.x + kLabelPlatePaddingPx,
				topLeft.y + size.y + kLabelPlatePaddingPx,
			};
		}

		// Top-left of a label beside `anchor` on `side`: its near edge at the anchor along an
		// axis `side` leans on, centered on the anchor along one it hardly leans on.
		Foundation::Vec2 labelBeside(Foundation::Vec2 anchor, Foundation::Vec2 side, Foundation::Vec2 size) {
			const auto place = [](float at, float lean, float extent) {
				if (lean > kSideTieTolerance) {
					return at;
				}
				if (lean < -kSideTieTolerance) {
					return at - extent;
				}
				return at - extent * 0.5F;
			};
			return {place(anchor.x, side.x, size.x), place(anchor.y, side.y, size.y)};
		}

		bool isFinite(Foundation::Vec2 v) {
			return std::isfinite(v.x) && std::isfinite(v.y);
		}

		float length(Foundation::Vec2 v) {
			return std::hypot(v.x, v.y);
		}

		float dot(Foundation::Vec2 a, Foundation::Vec2 b) {
			return a.x * b.x + a.y * b.y;
		}

		bool closeTo(Foundation::Vec2 a, Foundation::Vec2 b, float within) {
			return length({a.x - b.x, a.y - b.y}) <= within;
		}

		Foundation::Color withOpacity(const assets::StyleColor& color, float opacity) {
			return {color.r, color.g, color.b, opacity};
		}

		Foundation::Color colorOf(const assets::StyleColor& color) {
			return {color.r, color.g, color.b, color.a};
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

		std::string lengthText(float meters, int decimals) {
			const int places = std::clamp(decimals, 0, 6);
			double	  value = static_cast<double>(meters);
			// What rounds to zero prints as zero, never "-0.00".
			if (std::abs(value) < 0.5 * std::pow(10.0, -places)) {
				value = 0.0;
			}
			char text[32];
			std::snprintf(text, sizeof(text), "%.*f m", places, value);
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
			Planner(const DrawingFeedbackInput& input, const MeasureTextSize& measure)
				: input(input),
				  style(input.style),
				  measure(measure),
				  screenPixelsPerMeter(input.pixelsPerMeter * input.camera.zoom()),
				  markerRadius(std::max(kCursorMinimumRadiusPx, style.cursorRadiusPx)),
				  tickOuterRadius(markerRadius + kCursorTickGapPx + kCursorTickLengthPx),
				  snappedPoint(toScreen(input.cursor)) {}

			DrawingFeedbackPlan plan() {
				result.strokes.reserve(kMaxStrokes);
				result.rings.reserve(kMaxRings);
				result.dots.reserve(kMaxDots);
				result.labels.reserve(kMaxLabels);
				placedLabels.reserve(kMaxLabels + 2);

				chooseGuides();
				planCloseRing();
				for (std::size_t i = 0; i < input.snap.guideCount; ++i) {
					if (guideDrawn[i]) {
						planGuideStroke(input.snap.guides[i], guideFrom[i], guideTo[i]);
					}
					if (guideFinite[i] && ownsRightAngleMarker(i)) {
						planRightAngleMarker(input.snap.guides[i], guideFrom[i], guideTo[i]);
					}
				}
				planCloseHalo();
				seedLabelObstacles();
				planSegmentLabel();
				if (input.display.guideDistances) {
					for (std::size_t i = 0; i < input.snap.guideCount; ++i) {
						if (guideDrawn[i]) {
							planGuideLabel(input.snap.guides[i], guideFrom[i], guideTo[i]);
						}
					}
				}
				planCursor();
				planCrossingDot();
				planSnapTarget();
				return std::move(result);
			}

		  private:
			const DrawingFeedbackInput&				 input;
			const assets::PreviewStyle&				 style;
			const MeasureTextSize&					 measure;
			float									 screenPixelsPerMeter;
			float									 markerRadius;
			float									 tickOuterRadius;
			Foundation::Vec2						 snappedPoint; // screen pixels
			std::array<Foundation::Vec2, kMaxSnapGuides> guideFrom{};
			std::array<Foundation::Vec2, kMaxSnapGuides> guideTo{};
			std::array<bool, kMaxSnapGuides>		 guideFinite{};
			std::array<bool, kMaxSnapGuides>		 guideDrawn{}; // finite and not a duplicate: its stroke and label draw
			DrawingFeedbackPlan						 result;
			std::vector<Box>						 placedLabels;

			Foundation::Vec2 toScreen(Foundation::Vec2 world) const {
				return input.camera.worldToScreen(world.x, world.y, input.viewportWidth, input.viewportHeight, input.pixelsPerMeter);
			}

			bool sameEnds(std::size_t first, std::size_t second) const {
				return closeTo(guideFrom[first], guideFrom[second], kDuplicateGuidePx) && closeTo(guideTo[first], guideTo[second], kDuplicateGuidePx);
			}

			// A guide's stroke and label draw when both its ends are finite and it is not the
			// same as one before it.
			void chooseGuides() {
				for (std::size_t i = 0; i < input.snap.guideCount; ++i) {
					guideFrom[i] = toScreen(input.snap.guides[i].from);
					guideTo[i] = toScreen(input.snap.guides[i].to);
					guideFinite[i] = isFinite(guideFrom[i]) && isFinite(guideTo[i]);
					guideDrawn[i] = guideFinite[i];
					for (std::size_t earlier = 0; earlier < i && guideDrawn[i]; ++earlier) {
						if (guideFinite[earlier] && sameEnds(i, earlier)) {
							guideDrawn[i] = false;
						}
					}
				}
			}

			// A duplicate keeps its own right-angle marker unless one before it marks the same edge.
			bool ownsRightAngleMarker(std::size_t i) const {
				const SnapGuide& guide = input.snap.guides[i];
				if (!guide.rightAngle) {
					return false;
				}
				for (std::size_t earlier = 0; earlier < i; ++earlier) {
					const SnapGuide& other = input.snap.guides[earlier];
					if (guideFinite[earlier] && other.rightAngle && sameEnds(i, earlier) &&
						closeTo(guide.rightAngleEdgeDirection, other.rightAngleEdgeDirection, kSameEdgeDirection)) {
						return false;
					}
				}
				return true;
			}

			float closeHaloRadius() const {
				return std::max(tickOuterRadius + kCloseHaloTickClearancePx, input.closeRadiusMeters * screenPixelsPerMeter * 0.5F);
			}

			void planCloseRing() {
				if (!input.canClose || input.points.empty()) {
					return;
				}
				const Foundation::Vec2 center = toScreen(input.points.front());
				if (!isFinite(center)) {
					return;
				}
				result.rings.push_back({
					.center = center,
					.radius = input.closeRadiusMeters * screenPixelsPerMeter,
					.color = colorOf(style.originHaloColor),
					.width = kCloseRingWidthPx,
					.dash = alignmentDash(style),
					.layer = FeedbackLayer::Alignment,
				});
			}

			// A guide whose line passes through the previous point runs along the segment being
			// drawn from there to `to`, and the segment's solid line already shows that part: it
			// draws only from its node up to the previous point, which is nothing when the node
			// is the previous point. Dots over the solid line would bead it (teal beads on red
			// when it is invalid).
			void planGuideStroke(const SnapGuide& guide, Foundation::Vec2 from, Foundation::Vec2 to) {
				const Foundation::Vec2 along{to.x - from.x, to.y - from.y};
				const float			   alongLength = length(along);
				if (alongLength < kMinimumStrokePx) {
					return;
				}
				const Foundation::Vec2 unit{along.x / alongLength, along.y / alongLength};
				float				   visibleLength = alongLength;
				if (!input.points.empty()) {
					const Foundation::Vec2 previous = toScreen(input.points.back());
					const Foundation::Vec2 offset{previous.x - from.x, previous.y - from.y};
					const float			   offLine = std::abs(unit.x * offset.y - unit.y * offset.x);
					const float			   at = dot(unit, offset);
					// The segment runs from the previous point to `to`: it covers the guide from
					// there on, all of it when the previous point is behind the node, none of it
					// when the previous point is past `to`.
					if (isFinite(previous) && offLine <= kOnSegmentLinePx) {
						visibleLength = std::clamp(at, 0.0F, alongLength);
					}
				}
				if (visibleLength < kMinimumStrokePx) {
					return;
				}
				FeedbackStroke stroke = segmentStroke(
					from,
					{from.x + unit.x * visibleLength, from.y + unit.y * visibleLength},
					withOpacity(guideColor(style, guide.source), style.guideOpacity),
					style.guideWidth,
					FeedbackLayer::Alignment
				);
				stroke.dash = alignmentDash(style);
				result.strokes.push_back(stroke);
			}

			void planRightAngleMarker(const SnapGuide& guide, Foundation::Vec2 from, Foundation::Vec2 to) {
				const Foundation::Vec2 along{to.x - from.x, to.y - from.y};
				const float			   alongLength = length(along);
				if (alongLength < kMinimumStrokePx) {
					return;
				}
				const assets::StyleColor& color = guideColor(style, guide.source);
				const Foundation::Vec2	  edgeTip =
					toScreen({guide.from.x + guide.rightAngleEdgeDirection.x, guide.from.y + guide.rightAngleEdgeDirection.y});
				const Foundation::Vec2 edge{edgeTip.x - from.x, edgeTip.y - from.y};
				const float			   edgeLength = length(edge);
				if (!std::isfinite(edgeLength) || edgeLength <= 0.0F) {
					return;
				}
				const Foundation::Vec2 edgeUnit{edge.x / edgeLength, edge.y / edgeLength};
				const Foundation::Vec2 guideUnit{along.x / alongLength, along.y / alongLength};
				if (std::abs(edgeUnit.x * guideUnit.y - edgeUnit.y * guideUnit.x) < kRightAngleMinimumSine) {
					return;
				}
				const float			   size = style.rightAngleMarkerPx;
				const Foundation::Vec2 edgeLeg{edgeUnit.x * size, edgeUnit.y * size};
				const Foundation::Vec2 guideLeg{guideUnit.x * size, guideUnit.y * size};
				FeedbackStroke		   marker{
							.color = withOpacity(color, style.markerOpacity), .width = kMarkerLineWidthPx, .layer = FeedbackLayer::Marker
				};
				marker.points[0] = {from.x + edgeLeg.x, from.y + edgeLeg.y};
				marker.points[1] = {from.x + edgeLeg.x + guideLeg.x, from.y + edgeLeg.y + guideLeg.y};
				marker.points[2] = {from.x + guideLeg.x, from.y + guideLeg.y};
				marker.pointCount = 3;
				result.strokes.push_back(marker);
			}

			// Shown whenever the next click closes: on an Origin snap and on the near-start rescue.
			void planCloseHalo() {
				if (!input.willClose || input.points.empty()) {
					return;
				}
				const Foundation::Vec2 center = toScreen(input.points.front());
				if (!isFinite(center)) {
					return;
				}
				result.rings.push_back({
					.center = center,
					.radius = closeHaloRadius(),
					.color = withOpacity(guideColor(style, GuideSource::DrawnShape), style.markerOpacity),
					.width = kCloseHaloWidthPx,
					.layer = FeedbackLayer::Marker,
				});
			}

			// Guide labels keep clear of the snapped-point marker out to its ticks, and of the
			// close halo while the click will close.
			void seedLabelObstacles() {
				if (isFinite(snappedPoint)) {
					placedLabels.push_back(boxAround(snappedPoint, tickOuterRadius + kCursorTickOutlineWidthPx * 0.5F));
				}
				if (const std::optional<Box> halo = closeHaloBox()) {
					placedLabels.push_back(*halo);
				}
			}

			// The close halo's box, while it is drawn.
			std::optional<Box> closeHaloBox() const {
				if (!input.willClose || input.points.empty()) {
					return std::nullopt;
				}
				const Foundation::Vec2 center = toScreen(input.points.front());
				if (!isFinite(center)) {
					return std::nullopt;
				}
				return boxAround(center, closeHaloRadius() + kCloseHaloWidthPx * 0.5F);
			}

			// What the segment label keeps off: the snapped-point marker's dot and ticks, each
			// with its outline, and the close halo while it is drawn. The ticks are a cross, not
			// a square, so the label can sit in a quarter between them.
			std::vector<Box> segmentLabelObstacles() const {
				const float		 reach = tickOuterRadius + kCursorTickOutlineWidthPx * 0.5F;
				const float		 halfWidth = kCursorTickOutlineWidthPx * 0.5F;
				const Foundation::Vec2 at = snappedPoint;
				std::vector<Box> obstacles{
					{at.x - reach, at.y - halfWidth, at.x + reach, at.y + halfWidth},
					{at.x - halfWidth, at.y - reach, at.x + halfWidth, at.y + reach},
					boxAround(at, markerRadius + kCursorOutlineWidthPx * 0.5F),
				};
				if (const std::optional<Box> halo = closeHaloBox()) {
					obstacles.push_back(*halo);
				}
				return obstacles;
			}

			bool insideViewport(const Box& box) const {
				return box.left >= 0.0F && box.top >= 0.0F && box.right <= static_cast<float>(input.viewportWidth) &&
					   box.bottom <= static_cast<float>(input.viewportHeight);
			}

			Foundation::Vec2 moveInside(Foundation::Vec2 topLeft, Foundation::Vec2 size) const {
				const Box  box = plateBox(topLeft, size);
				const auto shift = [](float low, float high, float limit) {
					if (high > limit) {
						return std::max(limit - high, -low);
					}
					return low < 0.0F ? -low : 0.0F;
				};
				return {
					topLeft.x + shift(box.left, box.right, static_cast<float>(input.viewportWidth)),
					topLeft.y + shift(box.top, box.bottom, static_cast<float>(input.viewportHeight)),
				};
			}

			// The first candidate whose plate is inside the viewport; failing all, the first one
			// moved just inside.
			Foundation::Vec2 fitInViewport(std::span<const Foundation::Vec2> candidates, Foundation::Vec2 size) const {
				for (const Foundation::Vec2& candidate : candidates) {
					if (insideViewport(plateBox(candidate, size))) {
						return candidate;
					}
				}
				return moveInside(candidates.front(), size);
			}

			// The first candidate, in order, whose plate is inside the viewport and accepted;
			// failing all, the first one that is accepted once moved just inside. The check
			// comes after the move, so a move can't push a plate back onto what it keeps off.
			template <typename Accept>
			std::optional<Foundation::Vec2>
			firstClear(std::span<const Foundation::Vec2> candidates, Foundation::Vec2 size, const Accept& accept) const {
				for (const Foundation::Vec2& candidate : candidates) {
					const Box plate = plateBox(candidate, size);
					if (insideViewport(plate) && accept(plate)) {
						return candidate;
					}
				}
				for (const Foundation::Vec2& candidate : candidates) {
					const Foundation::Vec2 moved = moveInside(candidate, size);
					if (accept(plateBox(moved, size))) {
						return moved;
					}
				}
				return std::nullopt;
			}

			// Beside the segment's middle, off the side away from the shape, when that keeps it
			// clear of the pointer; otherwise at the snapped end, in the quarter round it that
			// points most along the segment, never the lower right one, where the pointer hangs.
			// Either way it keeps off the snapped-point marker and the close halo, taking the
			// next placement in order when one would cover them; with no clear placement it
			// takes the first one at the end.
			void planSegmentLabel() {
				if (input.points.empty() || !isFinite(snappedPoint)) {
					return;
				}
				const Foundation::Vec2 last = toScreen(input.points.back());
				const Foundation::Vec2 tip = snappedPoint;
				const float meters = std::hypot(input.cursor.x - input.points.back().x, input.cursor.y - input.points.back().y);
				if (!isFinite(last) || !std::isfinite(meters)) {
					return;
				}
				const std::string	   text = lengthText(meters, style.guideDistanceDecimals);
				const Foundation::Vec2 size = measure(text, kSegmentLabelPx);
				const Foundation::Vec2 segment{tip.x - last.x, tip.y - last.y};
				const float			   segmentLength = length(segment);
				const std::vector<Box> obstacles = segmentLabelObstacles();
				const auto			   clearOfObstacles = [&](const Box& plate) {
					  return std::none_of(obstacles.begin(), obstacles.end(), [&](const Box& obstacle) { return overlaps(plate, obstacle); });
				};

				if (segmentLength > 0.0F) {
					const Foundation::Vec2 middle{(last.x + tip.x) * 0.5F, (last.y + tip.y) * 0.5F};
					const Foundation::Vec2 side = awayFromShape(middle, {-segment.y / segmentLength, segment.x / segmentLength});
					const Foundation::Vec2 anchor{middle.x + side.x * kLabelGapPx, middle.y + side.y * kLabelGapPx};
					const Foundation::Vec2 reserve{
						std::max(size.x, measure(lengthText(kSegmentLabelReserveMeters, style.guideDistanceDecimals), kSegmentLabelPx).x), size.y
					};
					if (distanceToBox(tip, plateBox(labelBeside(anchor, side, reserve), reserve)) >= kSegmentLabelCursorClearancePx) {
						const std::array<Foundation::Vec2, 2> candidates{
							labelBeside(anchor, side, size),
							labelBeside({middle.x - side.x * kLabelGapPx, middle.y - side.y * kLabelGapPx}, {-side.x, -side.y}, size),
						};
						const std::optional<Foundation::Vec2> atMiddle = firstClear(candidates, size, [&](const Box& plate) {
							return distanceToBox(tip, plate) >= kSegmentLabelCursorClearancePx && clearOfObstacles(plate);
						});
						if (atMiddle) {
							addLabel(text, *atMiddle, kSegmentLabelPx, size, kSegmentLabelColor);
							return;
						}
					}
				}

				// In order of preference: up right, up left, down left.
				static const std::array<Foundation::Vec2, 3> kQuarters{{{1.0F, -1.0F}, {-1.0F, -1.0F}, {-1.0F, 1.0F}}};
				const Foundation::Vec2 along = segmentLength > 0.0F ? Foundation::Vec2{segment.x / segmentLength, segment.y / segmentLength}
																	: Foundation::Vec2{0.0F, 0.0F};
				std::array<float, 3> scores{};
				float				 best = -std::numeric_limits<float>::max();
				for (std::size_t i = 0; i < kQuarters.size(); ++i) {
					scores[i] = dot(kQuarters[i], along) / std::numbers::sqrt2_v<float>;
					best = std::max(best, scores[i]);
				}
				Foundation::Vec2 quarter = kQuarters[0];
				for (std::size_t i = 0; i < kQuarters.size(); ++i) {
					if (scores[i] >= best - kSideTieTolerance) {
						quarter = kQuarters[i];
						break;
					}
				}

				// Diagonally off the snapped point, or off the close halo's box when the halo is
				// drawn round the snapped point (the click closes on the first point).
				Box end{tip.x, tip.y, tip.x, tip.y};
				if (const std::optional<Box> halo = closeHaloBox();
					halo && tip.x >= halo->left && tip.x <= halo->right && tip.y >= halo->top && tip.y <= halo->bottom) {
					end = *halo;
				}
				const auto inQuarter = [&](Foundation::Vec2 q) -> Foundation::Vec2 {
					return {
						q.x > 0.0F ? end.right + kSegmentLabelOffsetPx : end.left - kSegmentLabelOffsetPx - size.x,
						q.y < 0.0F ? end.top - kSegmentLabelOffsetPx - size.y : end.bottom + kSegmentLabelOffsetPx,
					};
				};
				// Off the screen, the same quarter mirrored across the snapped point, never into
				// the lower right.
				std::array<Foundation::Vec2, 4> candidates{};
				std::size_t						count = 0;
				for (const Foundation::Vec2 mirror : {Foundation::Vec2{1.0F, 1.0F}, Foundation::Vec2{-1.0F, 1.0F}, Foundation::Vec2{1.0F, -1.0F}, Foundation::Vec2{-1.0F, -1.0F}}) {
					const Foundation::Vec2 q{quarter.x * mirror.x, quarter.y * mirror.y};
					if (q.x > 0.0F && q.y > 0.0F) {
						continue;
					}
					candidates[count++] = inQuarter(q);
				}
				const std::span<const Foundation::Vec2> atEnd{candidates.data(), count};
				addLabel(
					text, firstClear(atEnd, size, clearOfObstacles).value_or(fitInViewport(atEnd, size)), kSegmentLabelPx, size, kSegmentLabelColor
				);
			}

			// `normal` or its reverse, whichever points away from the placed points' center;
			// with no center off the line, the one facing up the screen, or left when the
			// segment is near vertical.
			Foundation::Vec2 awayFromShape(Foundation::Vec2 middle, Foundation::Vec2 normal) const {
				if (input.points.size() >= 2) {
					Foundation::Vec2 center{0.0F, 0.0F};
					for (const Foundation::Vec2& point : input.points) {
						const Foundation::Vec2 screen = toScreen(point);
						center.x += screen.x;
						center.y += screen.y;
					}
					const float count = static_cast<float>(input.points.size());
					const float side = dot({center.x / count - middle.x, center.y / count - middle.y}, normal);
					if (std::abs(side) > kShapeSideMinimumPx) {
						return side > 0.0F ? Foundation::Vec2{-normal.x, -normal.y} : normal;
					}
				}
				const Foundation::Vec2 reversed{-normal.x, -normal.y};
				if (std::abs(normal.y) > kSideTieTolerance) {
					return normal.y < 0.0F ? normal : reversed;
				}
				return normal.x <= 0.0F ? normal : reversed;
			}

			// D12's guide distance label: a guide's distance from its own node, beside its
			// middle, off the side away from the segment, moved along the guide when it would
			// cover a label placed before it, the marker, or the close halo. A guide through
			// the previous point is the segment itself, whose length the segment label shows.
			void planGuideLabel(const SnapGuide& guide, Foundation::Vec2 from, Foundation::Vec2 to) {
				if (guide.relation == GuideRelation::AngleStep || guide.throughPreviousPoint || !std::isfinite(guide.distanceFromNodeMeters)) {
					return;
				}
				const float guideLength = length({to.x - from.x, to.y - from.y});
				if (guideLength < kGuideLabelMinimumGuidePx) {
					return;
				}
				const Foundation::Vec2 along{(to.x - from.x) / guideLength, (to.y - from.y) / guideLength};
				const Foundation::Vec2 middle{(from.x + to.x) * 0.5F, (from.y + to.y) * 0.5F};
				Foundation::Vec2	   segmentMiddle = to;
				if (!input.points.empty()) {
					const Foundation::Vec2 last = toScreen(input.points.back());
					if (isFinite(last)) {
						segmentMiddle = {(last.x + to.x) * 0.5F, (last.y + to.y) * 0.5F};
					}
				}
				Foundation::Vec2 side{-along.y, along.x};
				if (dot({segmentMiddle.x - middle.x, segmentMiddle.y - middle.y}, side) > 0.0F) {
					side = {-side.x, -side.y};
				}

				const std::string	   text = lengthText(guide.distanceFromNodeMeters, style.guideDistanceDecimals);
				const float			   sizePx = style.guideLabelPx;
				const Foundation::Vec2 size = measure(text, sizePx);
				for (int attempt = 0; attempt <= kGuideLabelShiftAttempts; ++attempt) {
					const float			   steps = static_cast<float>((attempt + 1) / 2);
					const float			   shift = (attempt % 2 == 0 ? steps : -steps) * (size.x + kGuideLabelShiftPaddingPx);
					const Foundation::Vec2 onGuide{middle.x + along.x * shift, middle.y + along.y * shift};
					const std::array<Foundation::Vec2, 2> candidates{
						labelBeside({onGuide.x + side.x * kLabelGapPx, onGuide.y + side.y * kLabelGapPx}, side, size),
						labelBeside({onGuide.x - side.x * kLabelGapPx, onGuide.y - side.y * kLabelGapPx}, {-side.x, -side.y}, size),
					};
					const Foundation::Vec2 topLeft = fitInViewport(candidates, size);
					const Box			   box = plateBox(topLeft, size);
					const bool covers = std::any_of(placedLabels.begin(), placedLabels.end(), [&](const Box& other) { return overlaps(box, other); });
					if (!covers) {
						addLabel(text, topLeft, sizePx, size, withOpacity(guideColor(style, guide.source), 1.0F));
						return;
					}
				}
			}

			void addLabel(const std::string& text, Foundation::Vec2 topLeft, float sizePx, Foundation::Vec2 size, Foundation::Color color) {
				const Box plate = plateBox(topLeft, size);
				placedLabels.push_back(plate);
				result.labels.push_back(
					{.text = text, .position = topLeft, .sizePx = sizePx, .width = size.x, .height = size.y, .plate = plate, .color = color}
				);
			}

			// Off buildable ground the dot takes the invalid colour; the outline and ticks stay.
			void planCursor() {
				if (!isFinite(snappedPoint)) {
					return;
				}
				const Foundation::Vec2 at = snappedPoint;
				const float			   inner = markerRadius + kCursorTickGapPx;
				static const std::array<Foundation::Vec2, 4> kTickDirections{{{-1.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {0.0F, 1.0F}}};
				for (const auto& [layer, color, width] : {
						 std::tuple{FeedbackLayer::CursorOutline, kCursorOutlineColor, kCursorTickOutlineWidthPx},
						 std::tuple{FeedbackLayer::Cursor, kCursorFillColor, kCursorTickWidthPx},
					 }) {
					for (const Foundation::Vec2& direction : kTickDirections) {
						result.strokes.push_back(segmentStroke(
							{at.x + direction.x * inner, at.y + direction.y * inner},
							{at.x + direction.x * tickOuterRadius, at.y + direction.y * tickOuterRadius},
							color,
							width,
							layer
						));
					}
				}
				result.dots.push_back({
					.center = at,
					.radius = markerRadius,
					.fill = input.snappedPointInvalid ? input.invalidColor : kCursorFillColor,
					.outline = kCursorOutlineColor,
					.outlineWidth = kCursorOutlineWidthPx,
					.layer = FeedbackLayer::Cursor,
				});
			}

			// Over the marker's dot, inside it, so the marker reads as a target.
			void planCrossingDot() {
				if (input.snap.kind != SnapKind::GuideCrossing || !isFinite(snappedPoint)) {
					return;
				}
				const GuideSource source = input.snap.guideCount > 0 ? input.snap.guides[0].source : GuideSource::DrawnShape;
				result.dots.push_back({
					.center = snappedPoint,
					.radius = style.guideCrossingDotPx,
					.fill = withOpacity(guideColor(style, source), style.markerOpacity),
					.layer = FeedbackLayer::Target,
				});
			}

			// The wall tool's snap-target ring, outside the marker's ticks: amber on a vertex
			// (a wall endpoint or a foundation corner), cyan on a segment or edge.
			void planSnapTarget() {
				if (input.tool != ToolKind::Wall || !isFinite(snappedPoint)) {
					return;
				}
				const SnapKind kind = input.snap.kind;
				const bool	   onVertex = kind == SnapKind::WallEndpoint || kind == SnapKind::Vertex;
				const bool	   onSegment = kind == SnapKind::WallSegment || kind == SnapKind::Edge;
				if (!onVertex && !onSegment) {
					return;
				}
				result.rings.push_back({
					.center = snappedPoint,
					.radius = tickOuterRadius + kSnapTargetTickClearancePx,
					.color = colorOf(onVertex ? style.snapVertexColor : style.snapEdgeColor),
					.width = kSnapTargetRingWidthPx,
					.layer = FeedbackLayer::Target,
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
				case FeedbackLayer::Target:
					return layers.markerId;
				case FeedbackLayer::LabelPlate:
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

	DrawingFeedbackPlan planSnapFeedback(const DrawingFeedbackInput& input, const MeasureTextSize& measure) {
		return Planner(input, measure).plan();
	}

	void drawSnapFeedback(const DrawingFeedbackInput& input) {
		Renderer::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		const MeasureTextSize	measure = [fontRenderer](const std::string& text, float sizePx) -> Foundation::Vec2 {
			  if (fontRenderer == nullptr) {
				  return {0.0F, 0.0F};
			  }
			  const auto size = fontRenderer->MeasureText(text, sizePx / kFontBaseSizePx);
			  return {size.x, size.y};
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
			Renderer::Primitives::drawRect({
				.bounds = Foundation::Rect{label.plate.left, label.plate.top, label.plate.right - label.plate.left, label.plate.bottom - label.plate.top},
				.style = {.fill = kLabelPlateColor, .border = Foundation::BorderStyle{.width = 0.0F, .cornerRadius = kLabelPlateCornerPx}},
				.id = layerId(layers, FeedbackLayer::LabelPlate),
				.key = layerKey(layers, FeedbackLayer::LabelPlate),
			});
			Renderer::Primitives::drawText({
				.text = label.text,
				.position = label.position,
				.scale = label.sizePx / kFontBaseSizePx,
				.color = label.color,
				.id = layerId(layers, FeedbackLayer::Label),
				.key = layerKey(layers, FeedbackLayer::Label),
			});
		}
	}

} // namespace world_sim
