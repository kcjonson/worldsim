#include "DrawingFeedback.h"

#include "DrawingSystem.h"

#include <assets/ConstructionRegistry.h>
#include <construction/CornerAngles.h>
#include <graphics/Rect.h>
#include <primitives/Primitives.h>
#include <theme/Fonts.h>
#include <world/camera/WorldCamera.h>

#include <algorithm>
#include <cassert>
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
		using construction::LengthSnapKind;
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
		// The Rectangle tool's cross on the grid point that placed its corner, and its ring on an
		// existing corner or edge, from the prototype.
		constexpr float kGridCrossArmPx = 5.0F;
		constexpr float kGridCrossWidthPx = 1.25F;
		constexpr float kGridCrossOpacity = 0.55F;
		constexpr float kRectangleCornerRingRadiusPx = 8.0F;
		constexpr float kRectangleEdgeRingRadiusPx = 7.0F;
		// Common lengths: a tick across the segment at the snapped end, and on each edge of the
		// matched group a faint band and the drafting double tick at its middle.
		constexpr float kLengthEndTickHalfPx = 8.0F;
		constexpr float kLengthEndTickWidthPx = 2.0F;
		constexpr float kMatchedEdgeBandWidthPx = 6.0F;
		constexpr float kMatchedEdgeBandOpacity = 0.22F;
		constexpr float kMatchedEdgeTickHalfPx = 6.0F;
		constexpr float kMatchedEdgeTickWidthPx = 1.75F;
		constexpr float kMatchedEdgeTickSpacingPx = 4.0F;
		// The angle readout's arc round a corner that is not square. Its degrees sit beyond the
		// arc on the corner's bisector, the whole plate at least this far beyond the arc along
		// the bisector; like the other labels' gaps, it is the plate's, so it holds as the plate
		// grows with the interface size.
		constexpr float kAngleArcRadiusPx = 18.0F;
		constexpr float kAngleArcWidthPx = 1.5F;
		constexpr float kAngleLabelPlateGapPx = 3.0F;
		// A degrees label whose first spot on a side is taken moves out along the bisector in
		// these steps, at most this far, so it still reads as its corner's.
		constexpr float kDegreesLabelShiftStepPx = 4.0F;
		constexpr int	kDegreesLabelShiftSteps = 12; // 48 px
		// A plate placed exactly at a bound can land this far inside it in float sums.
		constexpr float kPlateRoundingPx = 1e-3F;
		// The closing corner reads within this many close radii of the first point.
		constexpr float kClosingReachInCloseRadii = 2.0F;
		constexpr float kSegmentLabelPx = 12.0F;
		// At the snapped end, the segment label's plate sits this far diagonally off the snapped
		// point (its text 12 px off at an interface size of 100%). The gap is the plate's, so it
		// holds as the plate grows with the interface size and the plate stays off the marker.
		constexpr float kSegmentPlateOffsetPx = 9.0F;
		// A guide label's plate off its guide, and a long segment's label's plate off the segment
		// (the text 10 px off at an interface size of 100%). Like the end label's, the gap is
		// the plate's, so it holds as the plate grows with the interface size.
		constexpr float kLabelPlateGapPx = 7.0F;
		constexpr float kGuideLabelMinimumStrokePx = 40.0F;
		constexpr float kGuideLabelShiftPaddingPx = 8.0F;
		constexpr int	kGuideLabelShiftAttempts = 6;
		constexpr float kLabelPlatePaddingPx = 3.0F;
		constexpr float kLabelPlateCornerPx = 3.0F;
		// The prototype palette's dark.
		const Foundation::Color kLabelPlateColor{10.0F / 255.0F, 12.0F / 255.0F, 16.0F / 255.0F, 0.7F};
		const Foundation::Color kSegmentLabelColor{0xE3 / 255.0F, 0xE7 / 255.0F, 0xEE / 255.0F, 1.0F};
		const Foundation::Color kCursorFillColor{1.0F, 1.0F, 1.0F, 1.0F};
		const Foundation::Color kCursorOutlineColor{0.0F, 0.0F, 0.0F, 1.0F};

		// The operating system's pointer hangs down and right of its hot spot, about this far
		// at the desktop's scaling. A segment label placed at the segment's middle has to stay
		// at least this far from the snapped point, which the pointer sits on or near.
		constexpr float kSegmentLabelCursorClearancePx = 40.0F;

		// Every segment-label placement's fit and clearance is judged with the width of this
		// length's label (or the real one, when wider), so the label growing a digit at 10 m or
		// 100 m can't change where it sits. The label draws at its own width inside that box.
		constexpr float kSegmentLabelReserveMeters = 999.0F;

		// A length label reads this after its length when the length matches a kept common
		// length: the length step caught it, or it lies within the merge distance of one.
		// Lengths are compared to the nearest millimeter, inclusive at the merge distance, which
		// agrees with the engine's grouping to within a millimeter.
		constexpr const char* kMatchedLengthSuffix = " =";
		constexpr double	  kMillimetersPerMeter = 1000.0;

		constexpr const char* kDegreeSign = "\xC2\xB0"; // U+00B0 in UTF-8

		// A label beside an anchor centers on it along an axis its side leans on by less than
		// this (a unit vector's component: under about 11 degrees off the other axis), so a side
		// the snap made exact (an axis) can't jitter between flush and centered.
		constexpr float kLabelLeanMinimum = 0.2F;

		// The end quarter's scores are unit dot products in [-1, 1]; one within this of the best
		// is a tie, broken in a fixed order, so float noise in a direction the snap made exact
		// (45 degrees) can't flip the quarter.
		constexpr float kQuarterTieTolerance = 0.2F;

		// The fixed side faces up the screen, unless the line it is beside is within about 11
		// degrees of vertical (its unit normal's y under this), where it faces left.
		constexpr float kFacingUpMinimumNormalY = 0.2F;

		// A point has to sit this far off a line to pick a side of it.
		constexpr float kSideMinimumPx = 0.5F;

		// Two guides whose ends both lie within this of each other's draw one stroke and one
		// label; their right-angle markers draw once for each edge direction among them.
		constexpr float kDuplicateGuidePx = 1.0F;
		constexpr float kSameEdgeDirection = 1e-3F; // between unit vectors

		// A guide's stroke stops where it comes within this of the solid segment being drawn.
		constexpr float kSolidSegmentClearancePx = 1.0F;

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
		// ticks, each read corner's right-angle marker; the close ring, halo, and snap-target
		// ring; the marker's and crossing dots; the segment's, each guide's, and each read
		// corner's label. The common-length marks add theirs, and the read corners their arcs.
		constexpr std::size_t kMaxReadCorners = std::tuple_size_v<decltype(construction::CornerAngles::corners)>;
		constexpr std::size_t kMaxStrokes = kMaxSnapGuides * 2 + 8 + kMaxReadCorners;
		constexpr std::size_t kMaxRings = 3;
		constexpr std::size_t kMaxDots = 2;
		constexpr std::size_t kMaxLabels = 1 + kMaxSnapGuides + kMaxReadCorners;
		// The end tick, and each matched edge's band and two ticks.
		constexpr std::size_t kEndTickStrokes = 1;
		constexpr std::size_t kStrokesPerMatchedEdge = 3;
		// What the guide labels keep off besides each other: the marker, the close halo, the
		// snap-target ring, and the keep-clear box.
		constexpr std::size_t kMaxLabelObstacles = 4;

		// The segment label's end placements: three quarters round the snapped point.
		constexpr std::size_t kEndPlacementCount = 3;

		static_assert(kMaxSnapGuides == 2, "guide labels rank guides[0] above guides[1]");

		// Every size the feedback draws or plans with, in window pixels. The label text and its
		// plate (padding, corner) are interface text, so they take the ui scale and follow the
		// Interface size (a label's text offset from what it labels grows only by the plate's
		// padding, keeping the plate's own gap fixed); everything else (strokes, markers, rings, clearances, thresholds)
		// stays in window pixels like the preview's own lines and dots. That is main's rule for
		// text drawn in the world (docs/design/ui/ui-scale-setting.md, "World text"). lookAt is
		// the one place the scale is applied; nothing else reads a pixel constant.
		struct Look {
			float markerLineWidthPx;
			float closeRingWidthPx;
			float closeHaloWidthPx;
			float closeHaloTickClearancePx;
			float snapTargetTickClearancePx;
			float snapTargetRingWidthPx;
			float cursorRadiusPx;
			float cursorOutlineWidthPx;
			float cursorTickGapPx;
			float cursorTickLengthPx;
			float cursorTickWidthPx;
			float cursorTickOutlineWidthPx;
			float gridCrossArmPx;
			float gridCrossWidthPx;
			float rectangleCornerRingRadiusPx;
			float rectangleEdgeRingRadiusPx;
			float segmentLabelPx;
			float segmentLabelOffsetPx;
			float segmentLabelCursorClearancePx;
			float guideLabelPx;
			float labelGapPx;
			float guideLabelMinimumStrokePx;
			float guideLabelShiftPaddingPx;
			float labelPlatePaddingPx;
			float labelPlateCornerPx;
			float sideMinimumPx;
			float duplicateGuidePx;
			float solidSegmentClearancePx;
			float minimumStrokePx;
			float ringSegmentPx;
			float guideWidthPx;
			float guideDashPx;
			float guideGapPx;
			float rightAngleMarkerPx;
			float guideCrossingDotPx;
			float lengthEndTickHalfPx;
			float lengthEndTickWidthPx;
			float matchedEdgeBandWidthPx;
			float matchedEdgeTickHalfPx;
			float matchedEdgeTickWidthPx;
			float matchedEdgeTickSpacingPx;
			float angleArcRadiusPx;
			float angleArcWidthPx;
			float angleLabelGapPx;
			float degreesLabelShiftStepPx;
		};

		Look lookAt(const assets::PreviewStyle& style, float uiScale) {
			return {
				.markerLineWidthPx = kMarkerLineWidthPx,
				.closeRingWidthPx = kCloseRingWidthPx,
				.closeHaloWidthPx = kCloseHaloWidthPx,
				.closeHaloTickClearancePx = kCloseHaloTickClearancePx,
				.snapTargetTickClearancePx = kSnapTargetTickClearancePx,
				.snapTargetRingWidthPx = kSnapTargetRingWidthPx,
				.cursorRadiusPx = std::max(kCursorMinimumRadiusPx, style.cursorRadiusPx),
				.cursorOutlineWidthPx = kCursorOutlineWidthPx,
				.cursorTickGapPx = kCursorTickGapPx,
				.cursorTickLengthPx = kCursorTickLengthPx,
				.cursorTickWidthPx = kCursorTickWidthPx,
				.cursorTickOutlineWidthPx = kCursorTickOutlineWidthPx,
				.gridCrossArmPx = kGridCrossArmPx,
				.gridCrossWidthPx = kGridCrossWidthPx,
				.rectangleCornerRingRadiusPx = kRectangleCornerRingRadiusPx,
				.rectangleEdgeRingRadiusPx = kRectangleEdgeRingRadiusPx,
				.segmentLabelPx = kSegmentLabelPx * uiScale,
				.segmentLabelOffsetPx = kSegmentPlateOffsetPx + kLabelPlatePaddingPx * uiScale,
				.segmentLabelCursorClearancePx = kSegmentLabelCursorClearancePx,
				.guideLabelPx = style.guideLabelPx * uiScale,
				.labelGapPx = kLabelPlateGapPx + kLabelPlatePaddingPx * uiScale,
				.guideLabelMinimumStrokePx = kGuideLabelMinimumStrokePx,
				.guideLabelShiftPaddingPx = kGuideLabelShiftPaddingPx,
				.labelPlatePaddingPx = kLabelPlatePaddingPx * uiScale,
				.labelPlateCornerPx = kLabelPlateCornerPx * uiScale,
				.sideMinimumPx = kSideMinimumPx,
				.duplicateGuidePx = kDuplicateGuidePx,
				.solidSegmentClearancePx = kSolidSegmentClearancePx,
				.minimumStrokePx = kMinimumStrokePx,
				.ringSegmentPx = kRingSegmentPx,
				.guideWidthPx = style.guideWidth,
				.guideDashPx = style.guideDashPx,
				.guideGapPx = style.guideGapPx,
				.rightAngleMarkerPx = style.rightAngleMarkerPx,
				.guideCrossingDotPx = style.guideCrossingDotPx,
				.lengthEndTickHalfPx = kLengthEndTickHalfPx,
				.lengthEndTickWidthPx = kLengthEndTickWidthPx,
				.matchedEdgeBandWidthPx = kMatchedEdgeBandWidthPx,
				.matchedEdgeTickHalfPx = kMatchedEdgeTickHalfPx,
				.matchedEdgeTickWidthPx = kMatchedEdgeTickWidthPx,
				.matchedEdgeTickSpacingPx = kMatchedEdgeTickSpacingPx,
				.angleArcRadiusPx = kAngleArcRadiusPx,
				.angleArcWidthPx = kAngleArcWidthPx,
				.angleLabelGapPx = kAngleLabelPlateGapPx,
				.degreesLabelShiftStepPx = kDegreesLabelShiftStepPx,
			};
		}

		// Where a label draws, and where the box its fit is judged by sits: the same, or a
		// wider box holding the label, anchored the same way.
		struct Placement {
			Foundation::Vec2 topLeft;
			Foundation::Vec2 judgedTopLeft;
		};

		// A ring's center and radius, screen pixels.
		struct Circle {
			Foundation::Vec2 center;
			float			 radius = 0.0F;
		};

		// One guide in screen pixels. Its stroke ends at `strokeEnd` when it draws one, and
		// its label draws only with the stroke.
		struct GuideOnScreen {
			Foundation::Vec2				from;
			Foundation::Vec2				to;
			bool							finite = false;
			std::optional<Foundation::Vec2> strokeEnd;
		};

		bool overlaps(const FeedbackBox& first, const FeedbackBox& second) {
			return first.left < second.right && second.left < first.right && first.top < second.bottom && second.top < first.bottom;
		}

		bool contains(const FeedbackBox& box, Foundation::Vec2 point) {
			return point.x >= box.left && point.x <= box.right && point.y >= box.top && point.y <= box.bottom;
		}

		// Whether any of the segment from `start` to `end` lies in `box`.
		bool segmentMeetsBox(Foundation::Vec2 start, Foundation::Vec2 end, const FeedbackBox& box) {
			const Foundation::Vec2 delta{end.x - start.x, end.y - start.y};
			float				   enter = 0.0F;
			float				   leave = 1.0F;
			// Keeps the t where `rate * t <= room`.
			const auto clip = [&](float rate, float room) {
				if (rate == 0.0F) {
					return room >= 0.0F;
				}
				const float bound = room / rate;
				if (rate < 0.0F) {
					enter = std::max(enter, bound);
				} else {
					leave = std::min(leave, bound);
				}
				return enter <= leave;
			};
			return clip(-delta.x, start.x - box.left) && clip(delta.x, box.right - start.x) && clip(-delta.y, start.y - box.top) &&
				   clip(delta.y, box.bottom - start.y);
		}

		FeedbackBox unionOf(const FeedbackBox& first, const FeedbackBox& second) {
			return {
				std::min(first.left, second.left),
				std::min(first.top, second.top),
				std::max(first.right, second.right),
				std::max(first.bottom, second.bottom),
			};
		}

		float distanceToBox(Foundation::Vec2 point, const FeedbackBox& box) {
			const float horizontal = std::max({box.left - point.x, 0.0F, point.x - box.right});
			const float vertical = std::max({box.top - point.y, 0.0F, point.y - box.bottom});
			return std::hypot(horizontal, vertical);
		}

		FeedbackBox boxAround(Foundation::Vec2 center, float halfSize) {
			return {center.x - halfSize, center.y - halfSize, center.x + halfSize, center.y + halfSize};
		}

		// A ring's box, its stroke included.
		FeedbackBox ringBox(const Circle& circle, float strokeWidth) {
			return boxAround(circle.center, circle.radius + strokeWidth * 0.5F);
		}

		FeedbackBox plateBox(Foundation::Vec2 topLeft, Foundation::Vec2 size, float padding) {
			return {
				topLeft.x - padding,
				topLeft.y - padding,
				topLeft.x + size.x + padding,
				topLeft.y + size.y + padding,
			};
		}

		// Top-left of a label beside `anchor` on `side`: its near edge at the anchor along an
		// axis `side` leans on, centered on the anchor along one it hardly leans on.
		Foundation::Vec2 labelBeside(Foundation::Vec2 anchor, Foundation::Vec2 side, Foundation::Vec2 size) {
			const auto place = [](float anchorCoordinate, float lean, float extent) {
				if (lean > kLabelLeanMinimum) {
					return anchorCoordinate;
				}
				if (lean < -kLabelLeanMinimum) {
					return anchorCoordinate - extent;
				}
				return anchorCoordinate - extent * 0.5F;
			};
			return {place(anchor.x, side.x, size.x), place(anchor.y, side.y, size.y)};
		}

		bool isFinite(Foundation::Vec2 point) {
			return std::isfinite(point.x) && std::isfinite(point.y);
		}

		float length(Foundation::Vec2 vector) {
			return std::hypot(vector.x, vector.y);
		}

		float dot(Foundation::Vec2 first, Foundation::Vec2 second) {
			return first.x * second.x + first.y * second.y;
		}

		bool closeTo(Foundation::Vec2 first, Foundation::Vec2 second, float within) {
			return length({first.x - second.x, first.y - second.y}) <= within;
		}

		// `normal` or its reverse, whichever faces up the screen, or left when the line it is
		// normal to is near vertical: the fixed side when nothing else picks one.
		Foundation::Vec2 facingUpOrLeft(Foundation::Vec2 normal) {
			const Foundation::Vec2 reversed{-normal.x, -normal.y};
			if (std::abs(normal.y) > kFacingUpMinimumNormalY) {
				return normal.y < 0.0F ? normal : reversed;
			}
			return normal.x <= 0.0F ? normal : reversed;
		}

		// The stretch of the line `origin + unit * t` within `reach` of the segment from
		// `start` to `end`, as the interval of t, if any. The points within reach of a
		// segment are a capsule, two discs joined by a band, so the stretch is one interval:
		// the span of its parts in each.
		std::optional<std::pair<float, float>>
		stretchWithinReach(Foundation::Vec2 origin, Foundation::Vec2 unit, Foundation::Vec2 start, Foundation::Vec2 end, float reach) {
			constexpr float kUnbounded = std::numeric_limits<float>::max();
			float			low = kUnbounded;
			float			high = -kUnbounded;
			const auto		include = [&](float first, float second) {
				 low = std::min(low, first);
				 high = std::max(high, second);
			};
			for (const Foundation::Vec2 center : {start, end}) {
				const Foundation::Vec2 toCenter{center.x - origin.x, center.y - origin.y};
				const float			   alongLine = dot(unit, toCenter);
				const float			   offLine = unit.x * toCenter.y - unit.y * toCenter.x;
				const float			   halfChordSquared = reach * reach - offLine * offLine;
				if (halfChordSquared >= 0.0F) {
					const float halfChord = std::sqrt(halfChordSquared);
					include(alongLine - halfChord, alongLine + halfChord);
				}
			}
			const Foundation::Vec2 segment{end.x - start.x, end.y - start.y};
			const float			   segmentLength = length(segment);
			if (segmentLength > 0.0F) {
				const Foundation::Vec2 axis{segment.x / segmentLength, segment.y / segmentLength};
				const Foundation::Vec2 normal{-axis.y, axis.x};
				const Foundation::Vec2 offset{origin.x - start.x, origin.y - start.y};
				float				   bandLow = -kUnbounded;
				float				   bandHigh = kUnbounded;
				// Keeps the t where `base + rate * t` lies in [minimum, maximum].
				const auto clip = [&](float base, float rate, float minimum, float maximum) {
					if (rate == 0.0F) {
						if (base < minimum || base > maximum) {
							bandLow = kUnbounded;
							bandHigh = -kUnbounded;
						}
						return;
					}
					const float first = (minimum - base) / rate;
					const float second = (maximum - base) / rate;
					bandLow = std::max(bandLow, std::min(first, second));
					bandHigh = std::min(bandHigh, std::max(first, second));
				};
				clip(dot(offset, axis), dot(unit, axis), 0.0F, segmentLength);
				clip(dot(offset, normal), dot(unit, normal), -reach, reach);
				if (bandLow <= bandHigh) {
					include(bandLow, bandHigh);
				}
			}
			if (low > high) {
				return std::nullopt;
			}
			return std::pair{low, high};
		}

		std::int64_t millimetersOf(double meters) {
			return std::llround(meters * kMillimetersPerMeter);
		}

		bool roundsToZero(double meters, int places) {
			return std::abs(meters) < 0.5 * std::pow(10.0, -places);
		}

		int clampedDecimals(int decimals) {
			return std::clamp(decimals, 0, 6);
		}

		std::string lengthText(float meters, int decimals) {
			const int places = clampedDecimals(decimals);
			double	  value = static_cast<double>(meters);
			// What rounds to zero prints as zero, never "-0.00".
			if (roundsToZero(value, places)) {
				value = 0.0;
			}
			char text[32];
			std::snprintf(text, sizeof(text), "%.*f m", places, value);
			return text;
		}

		std::string lengthLabelText(float meters, int decimals, bool matched) {
			std::string text = lengthText(meters, decimals);
			if (matched) {
				text += kMatchedLengthSuffix;
			}
			return text;
		}

		std::string degreesText(float degrees, int decimals) {
			char text[32];
			std::snprintf(text, sizeof(text), "%.*f%s", clampedDecimals(decimals), static_cast<double>(degrees), kDegreeSign);
			return text;
		}

		// Half a unit in the readout's last place: a corner that close to 90 degrees reads square.
		float squareToleranceDegrees(int decimals) {
			return static_cast<float>(0.5 * std::pow(10.0, -clampedDecimals(decimals)));
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

		// The common-length marks take the one guide color, or by source their own.
		const assets::StyleColor& lengthMarkColor(const assets::PreviewStyle& style) {
			return style.guideColorMode == assets::GuideColorMode::One ? style.guideColor : style.guideColorLength;
		}

		std::optional<Foundation::DashPattern> alignmentDash(const assets::PreviewStyle& style, const Look& look) {
			switch (style.guideLineStyle) {
				case assets::GuideLineStyle::Solid:
					return std::nullopt;
				case assets::GuideLineStyle::Dashed:
					return Foundation::DashPattern{.dash = look.guideDashPx, .gap = look.guideGapPx, .phase = 0.0F, .cap = Foundation::DashCap::Butt};
				case assets::GuideLineStyle::Dotted:
					return Foundation::DashPattern{.dash = look.guideDashPx, .gap = look.guideGapPx, .phase = 0.0F, .cap = Foundation::DashCap::Round};
			}
			return std::nullopt;
		}

		FeedbackStroke segmentStroke(Foundation::Vec2 start, Foundation::Vec2 end, Foundation::Color color, float width, FeedbackLayer layer) {
			FeedbackStroke stroke{.color = color, .width = width, .layer = layer};
			stroke.points[0] = start;
			stroke.points[1] = end;
			stroke.pointCount = 2;
			return stroke;
		}

		// The segment label's text, its size, and what it keeps off. `reserve` is the box its
		// fit is judged by: the reserved width and its own height.
		struct SegmentLabelLayout {
			std::string				 text;
			FeedbackTextWeight		 weight = FeedbackTextWeight::Regular;
			Foundation::Color		 color;
			Foundation::Vec2		 last; // the previous point, screen pixels
			Foundation::Vec2		 size;
			Foundation::Vec2		 reserve;
			std::vector<FeedbackBox> obstacles;

			bool clearOfObstacles(const FeedbackBox& plate) const {
				return std::none_of(obstacles.begin(), obstacles.end(), [&](const FeedbackBox& obstacle) { return overlaps(plate, obstacle); });
			}
		};

		class Planner {
		  public:
			Planner(const DrawingFeedbackInput& input, const MeasureTextSize& measure)
				: input(input),
				  style(input.style),
				  measure(measure),
				  look(lookAt(input.style, input.uiScale)),
				  screenPixelsPerMeter(input.pixelsPerMeter * input.camera.zoom()),
				  markerRadius(look.cursorRadiusPx),
				  tickOuterRadius(markerRadius + look.cursorTickGapPx + look.cursorTickLengthPx),
				  snappedPoint(toScreen(input.cursor)),
				  halo(closeHalo()),
				  snapTarget(snapTargetRing()) {}

			DrawingFeedbackPlan plan() {
				const construction::CornerAngles corners = readCorners();
				result.strokes.reserve(kMaxStrokes + commonLengthMarkStrokes());
				result.rings.reserve(kMaxRings);
				result.arcs.reserve(corners.count);
				result.dots.reserve(kMaxDots);
				result.labels.reserve(kMaxLabels);
				placedLabels.reserve(kMaxLabels + kMaxLabelObstacles);

				chooseGuides();
				planMatchedEdgeBands();
				planCloseRing();
				for (std::size_t index = 0; index < input.snap.guideCount; ++index) {
					const GuideOnScreen& guide = guides[index];
					if (guide.strokeEnd) {
						planGuideStroke(input.snap.guides[index], guide.from, *guide.strokeEnd);
					}
					if (guide.finite && ownsRightAngleMarker(index)) {
						planRightAngleMarker(input.snap.guides[index], guide.from, guide.to);
					}
				}
				planCloseHalo();
				planGridCross();
				planCommonLengthTicks();
				std::array<std::optional<CornerOnScreen>, kMaxReadCorners> labelledCorners = planCornerMarks(corners);
				seedLabelObstacles();
				if (rectangleTool()) {
					planSizeLabel();
				} else {
					planSegmentLabel();
				}
				if (input.display.guideDistances) {
					for (std::size_t index = 0; index < input.snap.guideCount; ++index) {
						if (guides[index].strokeEnd) {
							planGuideLabel(input.snap.guides[index], guides[index].from, *guides[index].strokeEnd);
						}
					}
				}
				planCornerReadouts(labelledCorners);
				planCursor();
				planCrossingDot();
				planSnapTarget();
				return std::move(result);
			}

		  private:
			struct SnapTarget {
				Circle			  circle;
				Foundation::Color color;
				FeedbackLayer	  layer;
			};

			// A corner the angle readout marks with an arc and its degrees, on screen: its vertex,
			// the path points either side of it, and the arc it draws.
			struct CornerOnScreen {
				Foundation::Vec2 vertex;
				Foundation::Vec2 previous;
				Foundation::Vec2 next;
				float			 bisectorRadians = 0.0F; // screen space, y down
				float			 degrees = 0.0F;
				FeedbackArc		 arc;
			};

			const DrawingFeedbackInput&					 input;
			const assets::PreviewStyle&					 style;
			const MeasureTextSize&						 measure;
			const Look									 look;
			float										 screenPixelsPerMeter;
			float										 markerRadius;
			float										 tickOuterRadius;
			Foundation::Vec2							 snappedPoint; // screen pixels
			std::optional<Circle>						 halo;
			std::optional<SnapTarget>					 snapTarget;
			std::array<GuideOnScreen, kMaxSnapGuides>	 guides{};
			std::array<Foundation::Vec2, kMaxSnapGuides> rightAngleMarkerCorners{}; // where the guides' right-angle markers drew
			std::size_t									 rightAngleMarkerCount = 0;
			DrawingFeedbackPlan							 result;
			std::vector<FeedbackBox>					 placedLabels;

			Foundation::Vec2 toScreen(Foundation::Vec2 world) const {
				return input.camera.worldToScreen(world.x, world.y, input.viewportWidth, input.viewportHeight, input.pixelsPerMeter);
			}

			std::optional<Foundation::Vec2> firstPointOnScreen() const {
				if (input.points.empty()) {
					return std::nullopt;
				}
				const Foundation::Vec2 first = toScreen(input.points.front());
				if (!isFinite(first)) {
					return std::nullopt;
				}
				return first;
			}

			// The close halo, while the click will close and commit: round the first point,
			// half the close radius on screen, never inside the marker's ticks. The one source
			// for the ring, its box, and the end placements that sit off it.
			std::optional<Circle> closeHalo() const {
				if (!input.willClose) {
					return std::nullopt;
				}
				const std::optional<Foundation::Vec2> center = firstPointOnScreen();
				const float radius = std::max(tickOuterRadius + look.closeHaloTickClearancePx, input.closeRadiusMeters * screenPixelsPerMeter * 0.5F);
				if (!center || !std::isfinite(radius) || radius <= 0.0F) {
					return std::nullopt;
				}
				return Circle{*center, radius};
			}

			bool rectangleTool() const { return input.tool == ToolKind::FoundationRectangle; }

			// The snap-target ring: amber on a vertex (a wall endpoint or a foundation corner),
			// cyan on a segment or edge. The wall tool's sits over the marker, outside its ticks;
			// the Rectangle tool's, the prototype's, sits closer in and under the marker.
			std::optional<SnapTarget> snapTargetRing() const {
				if (!isFinite(snappedPoint)) {
					return std::nullopt;
				}
				if (rectangleTool()) {
					const construction::RectangleSnapKind kind = input.rectangleSnap;
					if (kind != construction::RectangleSnapKind::Vertex && kind != construction::RectangleSnapKind::Edge) {
						return std::nullopt;
					}
					const bool onCorner = kind == construction::RectangleSnapKind::Vertex;
					return SnapTarget{
						.circle = {snappedPoint, onCorner ? look.rectangleCornerRingRadiusPx : look.rectangleEdgeRingRadiusPx},
						.color = colorOf(onCorner ? style.snapVertexColor : style.snapEdgeColor),
						.layer = FeedbackLayer::Marker,
					};
				}
				if (input.tool != ToolKind::Wall) {
					return std::nullopt;
				}
				const SnapKind kind = input.snap.kind;
				const bool	   onVertex = kind == SnapKind::WallEndpoint || kind == SnapKind::Vertex;
				const bool	   onSegment = kind == SnapKind::WallSegment || kind == SnapKind::Edge;
				if (!onVertex && !onSegment) {
					return std::nullopt;
				}
				return SnapTarget{
					.circle = {snappedPoint, tickOuterRadius + look.snapTargetTickClearancePx},
					.color = colorOf(onVertex ? style.snapVertexColor : style.snapEdgeColor),
					.layer = FeedbackLayer::Target,
				};
			}

			bool sameEnds(std::size_t first, std::size_t second) const {
				return closeTo(guides[first].from, guides[second].from, look.duplicateGuidePx) &&
					   closeTo(guides[first].to, guides[second].to, look.duplicateGuidePx);
			}

			// A guide's stroke and label draw when both its ends are finite, it is not the same
			// as one before it, and some of it lies off the solid segment.
			void chooseGuides() {
				for (std::size_t index = 0; index < input.snap.guideCount; ++index) {
					GuideOnScreen& guide = guides[index];
					guide.from = toScreen(input.snap.guides[index].from);
					guide.to = toScreen(input.snap.guides[index].to);
					guide.finite = isFinite(guide.from) && isFinite(guide.to);
					bool drawn = guide.finite;
					for (std::size_t earlier = 0; earlier < index && drawn; ++earlier) {
						if (guides[earlier].finite && sameEnds(index, earlier)) {
							drawn = false;
						}
					}
					if (drawn) {
						guide.strokeEnd = visibleStrokeEnd(guide.from, guide.to);
					}
				}
			}

			// A duplicate keeps its own right-angle marker unless one before it marks the same edge.
			bool ownsRightAngleMarker(std::size_t index) const {
				const SnapGuide& guide = input.snap.guides[index];
				if (!guide.rightAngle) {
					return false;
				}
				for (std::size_t earlier = 0; earlier < index; ++earlier) {
					const SnapGuide& other = input.snap.guides[earlier];
					if (guides[earlier].finite && other.rightAngle && sameEnds(index, earlier) &&
						closeTo(guide.rightAngleEdgeDirection, other.rightAngleEdgeDirection, kSameEdgeDirection)) {
						return false;
					}
				}
				return true;
			}

			// Only while the halo is not drawn: once the click will close, the halo alone marks
			// the first point.
			void planCloseRing() {
				if (!input.canClose || halo) {
					return;
				}
				const std::optional<Foundation::Vec2> center = firstPointOnScreen();
				const float							  radius = input.closeRadiusMeters * screenPixelsPerMeter;
				if (!center || !std::isfinite(radius) || radius <= 0.0F) {
					return;
				}
				result.rings.push_back({
					.center = *center,
					.radius = radius,
					.color = colorOf(style.originHaloColor),
					.width = look.closeRingWidthPx,
					.dash = alignmentDash(style, look),
					.layer = FeedbackLayer::Alignment,
				});
			}

			// Where the stroke of a guide from `from` to `to` ends, if it draws one: it runs from
			// its node to where it first comes within a pixel of the solid segment being drawn
			// (the previous point to the snapped point). Since the guide ends at the snapped
			// point, the rest of it is all that close, and dots there would bead the solid line
			// (teal beads on red when it is invalid). Nothing draws when that is shorter than
			// half a pixel.
			std::optional<Foundation::Vec2> visibleStrokeEnd(Foundation::Vec2 from, Foundation::Vec2 to) const {
				const Foundation::Vec2 along{to.x - from.x, to.y - from.y};
				const float			   alongLength = length(along);
				if (alongLength < look.minimumStrokePx) {
					return std::nullopt;
				}
				const Foundation::Vec2 unit{along.x / alongLength, along.y / alongLength};
				float				   visibleLength = alongLength;
				if (!input.points.empty() && isFinite(snappedPoint)) {
					const Foundation::Vec2 previous = toScreen(input.points.back());
					if (isFinite(previous)) {
						const auto closeStretch = stretchWithinReach(from, unit, previous, snappedPoint, look.solidSegmentClearancePx);
						if (closeStretch && closeStretch->second >= 0.0F && closeStretch->first <= alongLength) {
							visibleLength = std::clamp(closeStretch->first, 0.0F, alongLength);
						}
					}
				}
				if (visibleLength < look.minimumStrokePx) {
					return std::nullopt;
				}
				return Foundation::Vec2{from.x + unit.x * visibleLength, from.y + unit.y * visibleLength};
			}

			void planGuideStroke(const SnapGuide& guide, Foundation::Vec2 from, Foundation::Vec2 strokeEnd) {
				FeedbackStroke stroke = segmentStroke(
					from, strokeEnd, withOpacity(guideColor(style, guide.source), style.guideOpacity), look.guideWidthPx, FeedbackLayer::Alignment
				);
				stroke.dash = alignmentDash(style, look);
				result.strokes.push_back(stroke);
			}

			void planRightAngleMarker(const SnapGuide& guide, Foundation::Vec2 from, Foundation::Vec2 to) {
				const Foundation::Vec2 along{to.x - from.x, to.y - from.y};
				const float			   alongLength = length(along);
				if (alongLength < look.minimumStrokePx) {
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
				result.strokes.push_back(rightAngleMarker(from, edgeUnit, guideUnit, withOpacity(color, style.markerOpacity)));
				rightAngleMarkerCorners[rightAngleMarkerCount++] = from;
			}

			// The right-angle marker at `corner`: two legs along the unit directions `first` and
			// `second`, joined by a line parallel to each.
			FeedbackStroke rightAngleMarker(Foundation::Vec2 corner, Foundation::Vec2 first, Foundation::Vec2 second, Foundation::Color color) const {
				const float			   size = look.rightAngleMarkerPx;
				const Foundation::Vec2 firstLeg{first.x * size, first.y * size};
				const Foundation::Vec2 secondLeg{second.x * size, second.y * size};
				FeedbackStroke		   marker{.color = color, .width = look.markerLineWidthPx, .layer = FeedbackLayer::Marker};
				marker.points[0] = {corner.x + firstLeg.x, corner.y + firstLeg.y};
				marker.points[1] = {corner.x + firstLeg.x + secondLeg.x, corner.y + firstLeg.y + secondLeg.y};
				marker.points[2] = {corner.x + secondLeg.x, corner.y + secondLeg.y};
				marker.pointCount = 3;
				return marker;
			}

			bool guideMarksRightAngleAt(Foundation::Vec2 corner) const {
				return std::any_of(rightAngleMarkerCorners.begin(), rightAngleMarkerCorners.begin() + static_cast<std::ptrdiff_t>(rightAngleMarkerCount), [&](Foundation::Vec2 marked) {
					return closeTo(marked, corner, look.duplicateGuidePx);
				});
			}

			void planCloseHalo() {
				if (!halo) {
					return;
				}
				result.rings.push_back({
					.center = halo->center,
					.radius = halo->radius,
					.color = withOpacity(guideColor(style, GuideSource::DrawnShape), style.markerOpacity),
					.width = look.closeHaloWidthPx,
					.layer = FeedbackLayer::Marker,
				});
			}

			std::optional<FeedbackBox> haloBox() const {
				if (!halo) {
					return std::nullopt;
				}
				return ringBox(*halo, look.closeHaloWidthPx);
			}

			std::optional<FeedbackBox> snapTargetBox() const {
				if (!snapTarget) {
					return std::nullopt;
				}
				return ringBox(snapTarget->circle, look.snapTargetRingWidthPx);
			}

			// What every label keeps off besides the marker: the close halo, the wall tool's
			// snap-target ring, and the input's keep-clear box.
			void addCommonObstacles(std::vector<FeedbackBox>& obstacles) const {
				for (const std::optional<FeedbackBox>& box : {haloBox(), snapTargetBox(), input.keepClear}) {
					if (box) {
						obstacles.push_back(*box);
					}
				}
			}

			// The Rectangle tool's cross on the grid point that placed its corner, in the world
			// guide color: an alignment, under the rectangle, as the prototype draws it.
			void planGridCross() {
				if (!rectangleTool() || input.rectangleSnap != construction::RectangleSnapKind::Grid || !isFinite(snappedPoint)) {
					return;
				}
				const Foundation::Color color = withOpacity(guideColor(style, GuideSource::WorldAxes), kGridCrossOpacity);
				const float				arm = look.gridCrossArmPx;
				result.strokes.push_back(segmentStroke(
					{snappedPoint.x - arm, snappedPoint.y}, {snappedPoint.x + arm, snappedPoint.y}, color, look.gridCrossWidthPx, FeedbackLayer::Alignment
				));
				result.strokes.push_back(segmentStroke(
					{snappedPoint.x, snappedPoint.y - arm}, {snappedPoint.x, snappedPoint.y + arm}, color, look.gridCrossWidthPx, FeedbackLayer::Alignment
				));
			}

			std::size_t commonLengthMarkStrokes() const {
				if (input.snap.lengthSnap != LengthSnapKind::CommonLength) {
					return 0;
				}
				return kEndTickStrokes + matchedEdgeCount() * kStrokesPerMatchedEdge;
			}

			std::size_t matchedEdgeCount() const { return std::min(input.snap.matchedEdgeCount, input.snap.matchedEdges.size()); }

			// A length matches when it lies within the merge distance of a kept common length.
			bool matchesKeptLength(float meters) const {
				const std::size_t  count = std::min(input.snap.commonLengthCount, input.snap.commonLengths.size());
				const std::int64_t within = millimetersOf(input.commonLengthMergeMeters);
				const std::int64_t length = millimetersOf(meters);
				return std::any_of(input.snap.commonLengths.begin(), input.snap.commonLengths.begin() + static_cast<std::ptrdiff_t>(count), [&](float kept) {
					return std::llabs(length - millimetersOf(kept)) <= within;
				});
			}

			// A matched edge on screen, with its direction there.
			struct EdgeOnScreen {
				Foundation::Vec2 start;
				Foundation::Vec2 end;
				Foundation::Vec2 along;
			};

			// The matched group's edge at `index` on screen, when it is long enough there to give a
			// direction: a shorter one draws no marks.
			std::optional<EdgeOnScreen> matchedEdgeOnScreen(std::size_t index) const {
				const construction::LengthEdge&		  edge = input.snap.matchedEdges[index];
				const Foundation::Vec2				  start = toScreen(edge.start);
				const Foundation::Vec2				  end = toScreen(edge.end);
				const std::optional<Foundation::Vec2> along = unitBetween(start, end);
				if (!along) {
					return std::nullopt;
				}
				return EdgeOnScreen{start, end, *along};
			}

			// When the length step caught a common length, a faint band along every edge of the
			// matched group, an alignment's highlight: planned before the guides, so it draws
			// under them.
			void planMatchedEdgeBands() {
				if (input.snap.lengthSnap != LengthSnapKind::CommonLength) {
					return;
				}
				const Foundation::Color bandColor = withOpacity(lengthMarkColor(style), kMatchedEdgeBandOpacity);
				for (std::size_t index = 0; index < matchedEdgeCount(); ++index) {
					if (const std::optional<EdgeOnScreen> edge = matchedEdgeOnScreen(index)) {
						result.strokes.push_back(segmentStroke(edge->start, edge->end, bandColor, look.matchedEdgeBandWidthPx, FeedbackLayer::Alignment));
					}
				}
			}

			// When the length step caught a common length: a tick across the segment at the snapped
			// end, and on every edge of the matched group a double tick at its middle. Facts, small
			// and solid, in the common-length color.
			void planCommonLengthTicks() {
				if (input.snap.lengthSnap != LengthSnapKind::CommonLength) {
					return;
				}
				const Foundation::Color tickColor = withOpacity(lengthMarkColor(style), style.markerOpacity);
				for (std::size_t index = 0; index < matchedEdgeCount(); ++index) {
					const std::optional<EdgeOnScreen> edge = matchedEdgeOnScreen(index);
					if (!edge) {
						continue;
					}
					const Foundation::Vec2 middle{(edge->start.x + edge->end.x) * 0.5F, (edge->start.y + edge->end.y) * 0.5F};
					const Foundation::Vec2 along = edge->along;
					for (const float side : {-0.5F, 0.5F}) {
						const float offset = side * look.matchedEdgeTickSpacingPx;
						planTickAcross({middle.x + along.x * offset, middle.y + along.y * offset}, along, look.matchedEdgeTickHalfPx, tickColor, look.matchedEdgeTickWidthPx);
					}
				}
				if (input.points.empty()) {
					return;
				}
				if (const std::optional<Foundation::Vec2> along = unitBetween(toScreen(input.points.back()), snappedPoint)) {
					planTickAcross(snappedPoint, *along, look.lengthEndTickHalfPx, tickColor, look.lengthEndTickWidthPx);
				}
			}

			// The unit vector from `start` to `end` on screen, when both are finite and they lie
			// far enough apart to give one.
			std::optional<Foundation::Vec2> unitBetween(Foundation::Vec2 start, Foundation::Vec2 end) const {
				if (!isFinite(start) || !isFinite(end)) {
					return std::nullopt;
				}
				const Foundation::Vec2 delta{end.x - start.x, end.y - start.y};
				const float			   deltaLength = length(delta);
				if (deltaLength < look.minimumStrokePx) {
					return std::nullopt;
				}
				return Foundation::Vec2{delta.x / deltaLength, delta.y / deltaLength};
			}

			// A marker tick across the line running along `along` at `center`, `halfLength` each side.
			void planTickAcross(Foundation::Vec2 center, Foundation::Vec2 along, float halfLength, Foundation::Color color, float width) {
				const Foundation::Vec2 across{-along.y * halfLength, along.x * halfLength};
				result.strokes.push_back(
					segmentStroke({center.x - across.x, center.y - across.y}, {center.x + across.x, center.y + across.y}, color, width, FeedbackLayer::Marker)
				);
			}

			// The angle readout's tool, if the tool on reads corners: the Freeform tool and Add or
			// Subtract read a polygon, the wall tool an open chain until it closes.
			std::optional<construction::AngleReadoutTool> angleReadoutTool() const {
				switch (input.tool) {
					case ToolKind::Foundation:
					case ToolKind::FoundationEdit:
						return construction::AngleReadoutTool::Foundation;
					case ToolKind::Wall:
						return construction::AngleReadoutTool::Wall;
					case ToolKind::FoundationRectangle:
					case ToolKind::Opening:
						return std::nullopt;
				}
				return std::nullopt;
			}

			// The corners the angle readout shows: with the helper on, not under Alt, and not in the
			// Rectangle tool. The path is the placed points and the snapped point, or the placed
			// points alone when the click closes, the closing reach twice the close radius the snap
			// was given.
			construction::CornerAngles readCorners() const {
				const std::optional<construction::AngleReadoutTool> readoutTool = angleReadoutTool();
				if (!input.display.angleReadout || input.freeform || !readoutTool) {
					return {};
				}
				return construction::cornerAngles({
					.points = input.points,
					.cursor = input.cursor,
					.closes = input.clickCloses,
					.tool = *readoutTool,
					.closingReachMeters = kClosingReachInCloseRadii * input.closeRadiusMeters,
					.squareToleranceDegrees = squareToleranceDegrees(style.angleReadoutDecimals),
				});
			}

			// The angle readout's path: the placed points, then the snapped point unless the click
			// closes.
			std::size_t readoutPathCount() const { return input.points.size() + (input.clickCloses ? 0U : 1U); }

			// The readout path's point at `index`, round the path, on screen.
			Foundation::Vec2 readoutPathPointOnScreen(std::size_t index) const {
				const std::size_t wrapped = index % readoutPathCount();
				return toScreen(wrapped < input.points.size() ? input.points[wrapped] : input.cursor);
			}

			// Each read corner: a square one the right-angle marker along its two edges, unless a
			// guide's already marks that vertex; any other an arc from the edge toward the previous
			// vertex to the edge toward the next, unless an edge is shorter on screen than the arc's
			// radius, where an arc would read as noise. Returns the corners that would draw an arc,
			// which draws only with its degrees.
			std::array<std::optional<CornerOnScreen>, kMaxReadCorners> planCornerMarks(const construction::CornerAngles& corners) {
				std::array<std::optional<CornerOnScreen>, kMaxReadCorners> labelledCorners{};
				const Foundation::Color color = withOpacity(guideColor(style, GuideSource::DrawnShape), style.markerOpacity);
				for (std::size_t index = 0; index < std::min(corners.count, corners.corners.size()); ++index) {
					const construction::CornerAngle& corner = corners.corners[index];
					const Foundation::Vec2			 vertex = toScreen(corner.vertex);
					const std::optional<Foundation::Vec2> toPrevious = unitBetween(vertex, toScreen(corner.vertex + corner.toPrevious));
					const std::optional<Foundation::Vec2> toNext = unitBetween(vertex, toScreen(corner.vertex + corner.toNext));
					if (!toPrevious || !toNext) {
						continue;
					}
					if (corner.square) {
						if (!guideMarksRightAngleAt(vertex)) {
							result.strokes.push_back(rightAngleMarker(vertex, *toPrevious, *toNext, color));
						}
						continue;
					}
					const Foundation::Vec2 previous = readoutPathPointOnScreen(corner.vertexIndex + readoutPathCount() - 1);
					const Foundation::Vec2 next = readoutPathPointOnScreen(corner.vertexIndex + 1);
					const float			   shorterEdge = std::min(length({previous.x - vertex.x, previous.y - vertex.y}), length({next.x - vertex.x, next.y - vertex.y}));
					if (!(shorterEdge >= look.angleArcRadiusPx)) {
						continue;
					}
					// The world to screen scale's handedness turns the sweep with it.
					const float startRadians = std::atan2(toPrevious->y, toPrevious->x);
					const float sweepRadians = corner.sweepRadians * screenHandedness(corner.vertex);
					labelledCorners[index] = CornerOnScreen{
						.vertex = vertex,
						.previous = previous,
						.next = next,
						.bisectorRadians = startRadians + sweepRadians * 0.5F,
						.degrees = corner.degrees,
						.arc =
							{
								.center = vertex,
								.radius = look.angleArcRadiusPx,
								.startRadians = startRadians,
								.sweepRadians = sweepRadians,
								.color = color,
								.width = look.angleArcWidthPx,
								.layer = FeedbackLayer::Marker,
							},
					};
				}
				return labelledCorners;
			}

			// 1 when the world's x and y axes keep their turn on screen, -1 when the screen
			// mirrors them.
			float screenHandedness(Foundation::Vec2 around) const {
				const Foundation::Vec2 origin = toScreen(around);
				const Foundation::Vec2 alongX = toScreen({around.x + 1.0F, around.y});
				const Foundation::Vec2 alongY = toScreen({around.x, around.y + 1.0F});
				const float turn = (alongX.x - origin.x) * (alongY.y - origin.y) - (alongX.y - origin.y) * (alongY.x - origin.x);
				return turn < 0.0F ? -1.0F : 1.0F;
			}

			// Each corner's arc and degrees, in corner order, drawn together or not at all: a bare
			// arc reads as broken. A corner whose degrees find no clear spot is dropped, and the
			// corners after it no longer keep off its arc.
			void planCornerReadouts(std::array<std::optional<CornerOnScreen>, kMaxReadCorners>& labelledCorners) {
				for (std::optional<CornerOnScreen>& corner : labelledCorners) {
					if (corner && !planDegreesLabel(*corner, labelledCorners)) {
						corner.reset();
					}
				}
				for (const std::optional<CornerOnScreen>& corner : labelledCorners) {
					if (corner) {
						result.arcs.push_back(corner->arc);
					}
				}
			}

			// A corner's degrees beyond its arc, on one side of the corner, else on the other: the
			// inside (along the bisector) leads unless the corner's own lines cross its first spot,
			// as they do in a narrow corner. Along a pointer's ray that test changes at most once,
			// so the label changes side only when every spot on its side is taken. False, with
			// nothing placed, when neither side has a clear spot or the corner itself is off the
			// screen.
			bool planDegreesLabel(const CornerOnScreen& corner, const std::array<std::optional<CornerOnScreen>, kMaxReadCorners>& labelledCorners) {
				if (!insideViewport({corner.vertex.x, corner.vertex.y, corner.vertex.x, corner.vertex.y})) {
					return false;
				}
				const std::string	   text = degreesText(corner.degrees, style.angleReadoutDecimals);
				const float			   sizePx = look.guideLabelPx;
				const Foundation::Vec2 size = measure(text, sizePx, FeedbackTextWeight::Regular);
				const Foundation::Vec2 inside{std::cos(corner.bisectorRadians), std::sin(corner.bisectorRadians)};
				const Foundation::Vec2 outside{-inside.x, -inside.y};
				const bool insideLeads = !onCornerLines(plateBox(degreesSpot(corner, inside, size, 0), size, look.labelPlatePaddingPx), corner);
				for (const Foundation::Vec2 direction : insideLeads ? std::array{inside, outside} : std::array{outside, inside}) {
					for (int step = 0; step <= kDegreesLabelShiftSteps; ++step) {
						const Foundation::Vec2 topLeft = degreesSpot(corner, direction, size, step);
						if (degreesPlateClear(plateBox(topLeft, size, look.labelPlatePaddingPx), corner, labelledCorners)) {
							addLabel(text, topLeft, sizePx, size, withOpacity(guideColor(style, GuideSource::DrawnShape), 1.0F), FeedbackTextWeight::Regular);
							return true;
						}
					}
				}
				return false;
			}

			// The top-left of a degrees plate `step` spots out along `direction` from its vertex: the
			// first spot's center as far out as puts the plate's nearest point the arc's radius and a
			// gap out along `direction` (the plate's support distance there), so no corner of the
			// plate reaches the arc, each later one a shift step further, at most a bound beyond the
			// first so the label still reads as its corner's; each moved just inside the screen.
			Foundation::Vec2 degreesSpot(const CornerOnScreen& corner, Foundation::Vec2 direction, Foundation::Vec2 size, int step) const {
				const float plateSupport = std::abs(direction.x) * (size.x * 0.5F + look.labelPlatePaddingPx) +
										   std::abs(direction.y) * (size.y * 0.5F + look.labelPlatePaddingPx);
				const float reach = look.angleArcRadiusPx + look.angleLabelGapPx + plateSupport + static_cast<float>(step) * look.degreesLabelShiftStepPx;
				const Foundation::Vec2 center{corner.vertex.x + direction.x * reach, corner.vertex.y + direction.y * reach};
				return moveInside({center.x - size.x * 0.5F, center.y - size.y * 0.5F}, size);
			}

			// Whether a degrees plate keeps off the labels placed before it and their obstacles,
			// every labelled corner's arc and the gap round it, and its corner's lines.
			bool degreesPlateClear(
				const FeedbackBox& plate, const CornerOnScreen& corner, const std::array<std::optional<CornerOnScreen>, kMaxReadCorners>& labelledCorners
			) const {
				if (std::any_of(placedLabels.begin(), placedLabels.end(), [&](const FeedbackBox& other) { return overlaps(plate, other); })) {
					return false;
				}
				const float arcReach = look.angleArcRadiusPx + look.angleLabelGapPx - kPlateRoundingPx;
				for (const std::optional<CornerOnScreen>& labelled : labelledCorners) {
					if (labelled && distanceToBox(labelled->vertex, plate) < arcReach) {
						return false;
					}
				}
				return !onCornerLines(plate, corner);
			}

			// Whether a plate lies on its corner's two edges or the segment being drawn.
			bool onCornerLines(const FeedbackBox& plate, const CornerOnScreen& corner) const {
				if (segmentMeetsBox(corner.vertex, corner.previous, plate) || segmentMeetsBox(corner.vertex, corner.next, plate)) {
					return true;
				}
				return !input.points.empty() && segmentMeetsBox(toScreen(input.points.back()), snappedPoint, plate);
			}

			// Guide labels keep clear of the snapped-point marker out to its ticks, and of the
			// common obstacles.
			void seedLabelObstacles() {
				if (isFinite(snappedPoint)) {
					placedLabels.push_back(boxAround(snappedPoint, tickOuterRadius + look.cursorTickOutlineWidthPx * 0.5F));
				}
				addCommonObstacles(placedLabels);
			}

			// What the segment label keeps off: the snapped-point marker's dot and ticks, each
			// with its outline, and the common obstacles. The ticks are a cross, not a square,
			// so the label can sit in a quarter between them.
			std::vector<FeedbackBox> segmentLabelObstacles() const {
				const float				 reach = tickOuterRadius + look.cursorTickOutlineWidthPx * 0.5F;
				const float				 halfWidth = look.cursorTickOutlineWidthPx * 0.5F;
				const Foundation::Vec2	 center = snappedPoint;
				std::vector<FeedbackBox> obstacles{
					{center.x - reach, center.y - halfWidth, center.x + reach, center.y + halfWidth},
					{center.x - halfWidth, center.y - reach, center.x + halfWidth, center.y + reach},
					boxAround(center, markerRadius + look.cursorOutlineWidthPx * 0.5F),
				};
				addCommonObstacles(obstacles);
				return obstacles;
			}

			bool insideViewport(const FeedbackBox& box) const {
				return box.left >= 0.0F && box.top >= 0.0F && box.right <= static_cast<float>(input.viewportWidth) &&
					   box.bottom <= static_cast<float>(input.viewportHeight);
			}

			Foundation::Vec2 moveInside(Foundation::Vec2 topLeft, Foundation::Vec2 size) const {
				const FeedbackBox box = plateBox(topLeft, size, look.labelPlatePaddingPx);
				const auto		  shift = [](float low, float high, float limit) {
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

			// The label's top-left when its judged box moves from `placement.judgedTopLeft` to
			// `judgedTopLeft`: the label moves with it.
			static Foundation::Vec2 movedWith(const Placement& placement, Foundation::Vec2 judgedTopLeft) {
				return {
					placement.topLeft.x + judgedTopLeft.x - placement.judgedTopLeft.x,
					placement.topLeft.y + judgedTopLeft.y - placement.judgedTopLeft.y,
				};
			}

			// The first placement whose judged plate is inside the viewport, unmoved; failing
			// all, the first one moved just inside.
			Foundation::Vec2 firstOnScreen(std::span<const Placement> placements, Foundation::Vec2 judgedSize) const {
				for (const Placement& placement : placements) {
					if (insideViewport(plateBox(placement.judgedTopLeft, judgedSize, look.labelPlatePaddingPx))) {
						return placement.topLeft;
					}
				}
				return movedWith(placements.front(), moveInside(placements.front().judgedTopLeft, judgedSize));
			}

			// The first placement, in order, whose judged plate is inside the viewport and
			// accepted; failing all, the first one that is accepted once its judged plate is moved
			// just inside, the label moving with it. The check comes after the move, so a move
			// can't push a plate back onto what it keeps off. Returns the label's top-left.
			template <typename Accept>
			std::optional<Foundation::Vec2>
			firstClear(std::span<const Placement> placements, Foundation::Vec2 judgedSize, const Accept& accept) const {
				for (const Placement& placement : placements) {
					const FeedbackBox judged = plateBox(placement.judgedTopLeft, judgedSize, look.labelPlatePaddingPx);
					if (insideViewport(judged) && accept(judged)) {
						return placement.topLeft;
					}
				}
				for (const Placement& placement : placements) {
					const Foundation::Vec2 moved = moveInside(placement.judgedTopLeft, judgedSize);
					if (accept(plateBox(moved, judgedSize, look.labelPlatePaddingPx))) {
						return movedWith(placement, moved);
					}
				}
				return std::nullopt;
			}

			// The segment label: at the segment's middle when a placement there is clear,
			// otherwise at the snapped end. Every placement is judged at the reserved width.
			// With no clear placement at either, it takes the first end placement that fits
			// the screen unmoved, even over what it keeps off, and moves the first one inside
			// only when none fits. Nothing is labelled while the length rounds to zero. A length
			// that matches a kept common length reads " =", bold in the accent color; while Common
			// lengths is on, the reserved box holds that matched form too, so the " =" coming or
			// going changes neither whether the label fits a placement nor which one it takes.
			void planSegmentLabel() {
				if (input.points.empty() || !isFinite(snappedPoint)) {
					return;
				}
				const Foundation::Vec2 last = toScreen(input.points.back());
				const float meters = std::hypot(input.cursor.x - input.points.back().x, input.cursor.y - input.points.back().y);
				if (!isFinite(last) || !std::isfinite(meters) || roundsToZero(meters, clampedDecimals(style.guideDistanceDecimals))) {
					return;
				}
				const bool matched = input.snap.lengthSnap == LengthSnapKind::CommonLength || matchesKeptLength(meters);
				SegmentLabelLayout layout{
					.text = lengthLabelText(meters, style.guideDistanceDecimals, matched),
					.weight = matched ? FeedbackTextWeight::Bold : FeedbackTextWeight::Regular,
					.color = matched ? input.accentColor : kSegmentLabelColor,
					.last = last,
					.obstacles = segmentLabelObstacles(),
				};
				layout.size = measure(layout.text, look.segmentLabelPx, layout.weight);
				layout.reserve = {
					std::max(
						layout.size.x,
						measure(lengthText(kSegmentLabelReserveMeters, style.guideDistanceDecimals), look.segmentLabelPx, FeedbackTextWeight::Regular).x
					),
					layout.size.y,
				};
				if (input.display.commonLengths) {
					const Foundation::Vec2 matchedReserve =
						measure(lengthLabelText(kSegmentLabelReserveMeters, style.guideDistanceDecimals, true), look.segmentLabelPx, FeedbackTextWeight::Bold);
					layout.reserve = {std::max(layout.reserve.x, matchedReserve.x), std::max(layout.reserve.y, matchedReserve.y)};
				}

				if (const std::optional<Foundation::Vec2> atMiddle = middlePlacement(layout)) {
					addLabel(layout.text, *atMiddle, look.segmentLabelPx, layout.size, layout.color, layout.weight);
					return;
				}
				const std::array<Placement, kEndPlacementCount> atEnd = endPlacements(layout);
				const auto clear = [&](const FeedbackBox& plate) { return layout.clearOfObstacles(plate); };
				addLabel(
					layout.text,
					firstClear(atEnd, layout.reserve, clear).value_or(firstOnScreen(atEnd, layout.reserve)),
					look.segmentLabelPx,
					layout.size,
					layout.color,
					layout.weight
				);
			}

			// The Rectangle tool's size label ("4 m x 3 m"), beside the snapped corner, where the
			// pointer sits, as the segment label's end placements are: up right, else up left, else
			// down left, the first whose plate fits the screen; never down right, where the pointer
			// hangs, unless only a move inside fits it. Nothing while the two corners span no
			// rectangle.
			void planSizeLabel() {
				if (input.sizeLabel.empty() || !isFinite(snappedPoint)) {
					return;
				}
				const Foundation::Vec2 size = measure(input.sizeLabel, look.segmentLabelPx, FeedbackTextWeight::Regular);
				const FeedbackBox	   anchor = endAnchorBox();
				std::array<Placement, kEndPlacementCount> placements{};
				for (std::size_t index = 0; index < kEndPlacementCount; ++index) {
					const Foundation::Vec2 topLeft = inQuarter(anchor, kUpRightFirst[index], size);
					placements[index] = {topLeft, topLeft};
				}
				addLabel(input.sizeLabel, firstOnScreen(placements, size), look.segmentLabelPx, size, kSegmentLabelColor, FeedbackTextWeight::Regular);
			}

			// Beside the segment's middle, 10 px off it on the side away from the shape, then
			// the other side; only when the preferred side keeps it 40 px from the snapped
			// point, where the pointer sits, and only a placement that is still that far once on
			// the screen and keeps off its obstacles.
			std::optional<Foundation::Vec2> middlePlacement(const SegmentLabelLayout& layout) const {
				const Foundation::Vec2 tip = snappedPoint;
				const Foundation::Vec2 segment{tip.x - layout.last.x, tip.y - layout.last.y};
				const float			   segmentLength = length(segment);
				if (!(segmentLength > 0.0F)) {
					return std::nullopt;
				}
				const Foundation::Vec2 middle{(layout.last.x + tip.x) * 0.5F, (layout.last.y + tip.y) * 0.5F};
				const Foundation::Vec2 side = awayFromShape({-segment.y / segmentLength, segment.x / segmentLength});
				const Foundation::Vec2 anchor{middle.x + side.x * look.labelGapPx, middle.y + side.y * look.labelGapPx};
				const FeedbackBox preferredPlate = plateBox(labelBeside(anchor, side, layout.reserve), layout.reserve, look.labelPlatePaddingPx);
				if (distanceToBox(tip, preferredPlate) < look.segmentLabelCursorClearancePx) {
					return std::nullopt;
				}
				const Foundation::Vec2		   otherSide{-side.x, -side.y};
				const Foundation::Vec2		   otherAnchor{middle.x + otherSide.x * look.labelGapPx, middle.y + otherSide.y * look.labelGapPx};
				const std::array<Placement, 2> placements{{
					{labelBeside(anchor, side, layout.size), labelBeside(anchor, side, layout.reserve)},
					{labelBeside(otherAnchor, otherSide, layout.size), labelBeside(otherAnchor, otherSide, layout.reserve)},
				}};
				return firstClear(placements, layout.reserve, [&](const FeedbackBox& plate) {
					return distanceToBox(tip, plate) >= look.segmentLabelCursorClearancePx && layout.clearOfObstacles(plate);
				});
			}

			// The quarters round the snapped point an end placement can take: up right, up left,
			// and down left, never down right, where the pointer hangs.
			static constexpr Foundation::Vec2 kUpRight{1.0F, -1.0F};
			static constexpr Foundation::Vec2 kUpLeft{-1.0F, -1.0F};
			static constexpr Foundation::Vec2 kDownLeft{-1.0F, 1.0F};

			// The end placements starting up right; the size label always takes them in this order.
			static constexpr std::array<Foundation::Vec2, kEndPlacementCount> kUpRightFirst{kUpRight, kUpLeft, kDownLeft};

			// For each quarter, in kUpRight, kUpLeft, kDownLeft order, the end placements to try:
			// that quarter, then it flipped left to right, then top to bottom, then both, leaving
			// out whichever of those is down right.
			static constexpr std::array<std::array<Foundation::Vec2, kEndPlacementCount>, 3> kEndPlacementOrders{{
				kUpRightFirst,
				{kUpLeft, kUpRight, kDownLeft},
				{kDownLeft, kUpLeft, kUpRight},
			}};

			// Of up right, up left, and down left, the index of the quarter that points most along
			// the segment; near-ties go to the first in that order.
			static std::size_t endQuarter(Foundation::Vec2 along) {
				static constexpr std::array<Foundation::Vec2, 3> kQuarters{kUpRight, kUpLeft, kDownLeft};
				std::array<float, 3>							 scores{};
				float											 best = -std::numeric_limits<float>::max();
				for (std::size_t index = 0; index < kQuarters.size(); ++index) {
					scores[index] = dot(kQuarters[index], along) / std::numbers::sqrt2_v<float>;
					best = std::max(best, scores[index]);
				}
				for (std::size_t index = 0; index < kQuarters.size(); ++index) {
					if (scores[index] >= best - kQuarterTieTolerance) {
						return index;
					}
				}
				return 0;
			}

			// The box the end placements sit diagonally off: the snapped point, grown to the
			// close halo's box when the halo is round it and to the snap-target ring's when that
			// is drawn.
			FeedbackBox endAnchorBox() const {
				FeedbackBox end{snappedPoint.x, snappedPoint.y, snappedPoint.x, snappedPoint.y};
				if (const std::optional<FeedbackBox> box = haloBox(); box && contains(*box, snappedPoint)) {
					end = unionOf(end, *box);
				}
				if (const std::optional<FeedbackBox> box = snapTargetBox()) {
					end = unionOf(end, *box);
				}
				return end;
			}

			// The end placements in order (kEndPlacementOrders): the quarter that points most
			// along the segment, then that quarter flipped left to right, top to bottom, and both,
			// skipping the lower right. Each label is anchored at its near corner; its judged box,
			// the reserved width, is anchored at the same corner.
			std::array<Placement, kEndPlacementCount> endPlacements(const SegmentLabelLayout& layout) const {
				const Foundation::Vec2 segment{snappedPoint.x - layout.last.x, snappedPoint.y - layout.last.y};
				const float			   segmentLength = length(segment);
				const Foundation::Vec2 along = segmentLength > 0.0F ? Foundation::Vec2{segment.x / segmentLength, segment.y / segmentLength}
																	: Foundation::Vec2{0.0F, 0.0F};
				const FeedbackBox	   end = endAnchorBox();
				const std::array<Foundation::Vec2, kEndPlacementCount>& order = kEndPlacementOrders[endQuarter(along)];
				std::array<Placement, kEndPlacementCount>				placements{};
				for (std::size_t index = 0; index < kEndPlacementCount; ++index) {
					placements[index] = {inQuarter(end, order[index], layout.size), inQuarter(end, order[index], layout.reserve)};
				}
				return placements;
			}

			// The top-left of a box `size` diagonally off `anchor` in the quarter `corner` points
			// to, anchored by its corner nearest the anchor.
			Foundation::Vec2 inQuarter(const FeedbackBox& anchor, Foundation::Vec2 corner, Foundation::Vec2 size) const {
				return {
					corner.x > 0.0F ? anchor.right + look.segmentLabelOffsetPx : anchor.left - look.segmentLabelOffsetPx - size.x,
					corner.y < 0.0F ? anchor.top - look.segmentLabelOffsetPx - size.y : anchor.bottom + look.segmentLabelOffsetPx,
				};
			}

			// `normal` (the segment's, on screen) or its reverse, whichever points away from the
			// placed points' center; with no center off the line, the fixed side. The center's
			// offset is measured from the previous point in world meters and doubles: along a ray
			// from the previous point it then moves only with the ray's direction, which the
			// snapped point's rounding barely turns, so a center near look.sideMinimumPx off the
			// line stays on one side of that threshold. Measured from screen floats and the segment's
			// middle, it wandered by a ten-thousandth of a pixel from one centimeter to the next and
			// flipped the label across the segment each time. The world to screen scale is
			// positive on both axes, so the world normal points the same way as `normal`.
			Foundation::Vec2 awayFromShape(Foundation::Vec2 normal) const {
				const std::size_t count = input.points.size();
				if (count >= 2) {
					const Foundation::Vec2 last = input.points.back();
					double				   offsetX = 0.0;
					double				   offsetY = 0.0;
					for (const Foundation::Vec2& point : input.points) {
						offsetX += static_cast<double>(point.x) - last.x;
						offsetY += static_cast<double>(point.y) - last.y;
					}
					offsetX /= static_cast<double>(count);
					offsetY /= static_cast<double>(count);
					const double alongX = static_cast<double>(input.cursor.x) - last.x;
					const double alongY = static_cast<double>(input.cursor.y) - last.y;
					const double alongLength = std::hypot(alongX, alongY);
					if (alongLength > 0.0) {
						const double sidePx = (offsetY * alongX - offsetX * alongY) / alongLength * screenPixelsPerMeter;
						if (std::abs(sidePx) > look.sideMinimumPx) {
							return sidePx > 0.0 ? Foundation::Vec2{-normal.x, -normal.y} : normal;
						}
					}
				}
				return facingUpOrLeft(normal);
			}

			// The part of the stroke from `from` to `to` inside the viewport, if any.
			std::optional<std::pair<Foundation::Vec2, Foundation::Vec2>> onScreenPart(Foundation::Vec2 from, Foundation::Vec2 to) const {
				const Foundation::Vec2 delta{to.x - from.x, to.y - from.y};
				float				   enter = 0.0F;
				float				   leave = 1.0F;
				// Keeps the t where `rate * t <= room`.
				const auto clip = [&](float rate, float room) {
					if (rate == 0.0F) {
						return room >= 0.0F;
					}
					const float bound = room / rate;
					if (rate < 0.0F) {
						enter = std::max(enter, bound);
					} else {
						leave = std::min(leave, bound);
					}
					return enter <= leave;
				};
				const float width = static_cast<float>(input.viewportWidth);
				const float height = static_cast<float>(input.viewportHeight);
				if (!clip(-delta.x, from.x) || !clip(delta.x, width - from.x) || !clip(-delta.y, from.y) || !clip(delta.y, height - from.y)) {
					return std::nullopt;
				}
				return std::pair{
					Foundation::Vec2{from.x + delta.x * enter, from.y + delta.y * enter},
					Foundation::Vec2{from.x + delta.x * leave, from.y + delta.y * leave},
				};
			}

			// D12's guide distance label: a guide's distance from its own node, beside the middle
			// of the part of its stroke on the screen, off the side away from the segment's
			// middle, moved along that part when it would cover a label placed before it, the
			// marker, or another obstacle: first back toward the node, then forward, alternately,
			// never past the part's ends. It draws only with the stroke, and is dropped when no
			// spot along the part is clear.
			void planGuideLabel(const SnapGuide& guide, Foundation::Vec2 from, Foundation::Vec2 strokeEnd) {
				if (guide.relation == GuideRelation::AngleStep || !std::isfinite(guide.distanceFromNodeMeters)) {
					return;
				}
				const auto onScreen = onScreenPart(from, strokeEnd);
				if (!onScreen) {
					return;
				}
				const auto [start, end] = *onScreen;
				const float partLength = length({end.x - start.x, end.y - start.y});
				if (partLength < look.guideLabelMinimumStrokePx) {
					return;
				}
				const Foundation::Vec2 along{(end.x - start.x) / partLength, (end.y - start.y) / partLength};
				const Foundation::Vec2 middle{(start.x + end.x) * 0.5F, (start.y + end.y) * 0.5F};
				const Foundation::Vec2 side = awayFromSegment(middle, {-along.y, along.x});

				const bool				 matched = matchesKeptLength(guide.distanceFromNodeMeters);
				const std::string		 text = lengthLabelText(guide.distanceFromNodeMeters, style.guideDistanceDecimals, matched);
				const FeedbackTextWeight weight = matched ? FeedbackTextWeight::Bold : FeedbackTextWeight::Regular;
				const Foundation::Color	 color = matched ? input.accentColor : withOpacity(guideColor(style, guide.source), 1.0F);
				const float				 sizePx = look.guideLabelPx;
				const Foundation::Vec2	 size = measure(text, sizePx, weight);
				const float			   halfPart = partLength * 0.5F;
				bool				   triedBackEnd = false;
				bool				   triedForwardEnd = false;
				for (int attempt = 0; attempt <= kGuideLabelShiftAttempts; ++attempt) {
					const float steps = static_cast<float>((attempt + 1) / 2);
					const float wanted = (attempt % 2 == 0 ? steps : -steps) * (size.x + look.guideLabelShiftPaddingPx);
					const float shift = std::clamp(wanted, -halfPart, halfPart);
					if (shift != wanted) {
						bool& tried = shift < 0.0F ? triedBackEnd : triedForwardEnd;
						if (tried) {
							continue;
						}
						tried = true;
					}
					const Foundation::Vec2 onGuide{middle.x + along.x * shift, middle.y + along.y * shift};
					const Foundation::Vec2 preferred =
						labelBeside({onGuide.x + side.x * look.labelGapPx, onGuide.y + side.y * look.labelGapPx}, side, size);
					const Foundation::Vec2 opposite =
						labelBeside({onGuide.x - side.x * look.labelGapPx, onGuide.y - side.y * look.labelGapPx}, {-side.x, -side.y}, size);
					const std::array<Placement, 2> candidates{{{preferred, preferred}, {opposite, opposite}}};
					const Foundation::Vec2		   topLeft = firstOnScreen(candidates, size);
					const FeedbackBox			   box = plateBox(topLeft, size, look.labelPlatePaddingPx);
					const bool covers = std::any_of(placedLabels.begin(), placedLabels.end(), [&](const FeedbackBox& other) { return overlaps(box, other); });
					if (!covers) {
						addLabel(text, topLeft, sizePx, size, color, weight);
						return;
					}
				}
			}

			// `normal` or its reverse, whichever points away from the segment's middle; when
			// the middle is about on the line (a guide along the segment), the fixed side.
			Foundation::Vec2 awayFromSegment(Foundation::Vec2 middle, Foundation::Vec2 normal) const {
				if (!input.points.empty()) {
					const Foundation::Vec2 last = toScreen(input.points.back());
					if (isFinite(last)) {
						const Foundation::Vec2 segmentMiddle{(last.x + snappedPoint.x) * 0.5F, (last.y + snappedPoint.y) * 0.5F};
						const float			   lean = dot({segmentMiddle.x - middle.x, segmentMiddle.y - middle.y}, normal);
						if (std::abs(lean) > look.sideMinimumPx) {
							return lean > 0.0F ? Foundation::Vec2{-normal.x, -normal.y} : normal;
						}
					}
				}
				return facingUpOrLeft(normal);
			}

			void addLabel(
				const std::string& text, Foundation::Vec2 topLeft, float sizePx, Foundation::Vec2 size, Foundation::Color color, FeedbackTextWeight weight
			) {
				const FeedbackBox plate = plateBox(topLeft, size, look.labelPlatePaddingPx);
				placedLabels.push_back(plate);
				result.labels.push_back({
					.text = text,
					.position = topLeft,
					.sizePx = sizePx,
					.plate = plate,
					.plateCornerRadius = look.labelPlateCornerPx,
					.color = color,
					.weight = weight,
				});
			}

			// Off buildable ground the dot takes the invalid color; the outline and ticks stay.
			void planCursor() {
				if (!isFinite(snappedPoint)) {
					return;
				}
				const Foundation::Vec2						 center = snappedPoint;
				const float									 inner = markerRadius + look.cursorTickGapPx;
				static const std::array<Foundation::Vec2, 4> kTickDirections{{{-1.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {0.0F, 1.0F}}};
				for (const auto& [layer, color, width] : {
						 std::tuple{FeedbackLayer::CursorOutline, kCursorOutlineColor, look.cursorTickOutlineWidthPx},
						 std::tuple{FeedbackLayer::Cursor, kCursorFillColor, look.cursorTickWidthPx},
					 }) {
					for (const Foundation::Vec2& direction : kTickDirections) {
						result.strokes.push_back(segmentStroke(
							{center.x + direction.x * inner, center.y + direction.y * inner},
							{center.x + direction.x * tickOuterRadius, center.y + direction.y * tickOuterRadius},
							color,
							width,
							layer
						));
					}
				}
				result.dots.push_back({
					.center = center,
					.radius = markerRadius,
					.fill = input.snappedPointInvalid ? input.invalidColor : kCursorFillColor,
					.outline = kCursorOutlineColor,
					.outlineWidth = look.cursorOutlineWidthPx,
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
					.radius = look.guideCrossingDotPx,
					.fill = withOpacity(guideColor(style, source), style.markerOpacity),
					.layer = FeedbackLayer::Target,
				});
			}

			void planSnapTarget() {
				if (!snapTarget) {
					return;
				}
				result.rings.push_back({
					.center = snapTarget->circle.center,
					.radius = snapTarget->circle.radius,
					.color = snapTarget->color,
					.width = look.snapTargetRingWidthPx,
					.layer = snapTarget->layer,
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

		// The game's default face has no bold cut; bold text takes the theme's display face, a
		// semibold.
		Renderer::FontFamily fontOf(FeedbackTextWeight weight) {
			return weight == FeedbackTextWeight::Bold ? theme::fontDisplay : Renderer::FontFamily::Default;
		}

		void drawRing(const FeedbackRing& ring, const ToolLayers& layers, float segmentPx) {
			if (!std::isfinite(ring.radius) || ring.radius <= 0.0F) {
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
			const int	segments = static_cast<int>(
				  std::clamp(std::ceil(circumference / segmentPx), static_cast<float>(kRingMinimumSegments), static_cast<float>(kRingMaximumSegments))
			  );
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

	std::int32_t feedbackLayerKey(ToolKind tool, FeedbackLayer layer) {
		return layerKey(toolLayers(tool), layer);
	}

	DrawingFeedbackPlan planSnapFeedback(const DrawingFeedbackInput& input, const MeasureTextSize& measure) {
		// An input that leaves uiScale out is 0, and every label would plan at size 0.
		assert(input.uiScale > 0.0F && "DrawingFeedbackInput::uiScale must be positive");
		return Planner(input, measure).plan();
	}

	Foundation::Vec2 measureFeedbackText(const std::string& text, float sizePx, FeedbackTextWeight weight) {
		Renderer::FontRenderer* fontRenderer = Renderer::Primitives::getFontRenderer();
		if (fontRenderer == nullptr) {
			return {0.0F, 0.0F};
		}
		const auto size = fontRenderer->MeasureText(text, sizePx / kRendererFontBaseSizePx, fontOf(weight));
		return {size.x, size.y};
	}

	void drawSnapFeedback(const DrawingFeedbackInput& input) {
		const DrawingFeedbackPlan plan = planSnapFeedback(input, measureFeedbackText);
		const ToolLayers		  layers = toolLayers(input.tool);
		const Look				  look = lookAt(input.style, input.uiScale);

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
			drawRing(ring, layers, look.ringSegmentPx);
		}
		for (const FeedbackArc& arc : plan.arcs) {
			Renderer::Primitives::drawArc({
				.center = arc.center,
				.innerRadius = arc.radius - arc.width * 0.5F,
				.outerRadius = arc.radius + arc.width * 0.5F,
				.startAngle = arc.startRadians,
				.sweep = arc.sweepRadians,
				.color = arc.color,
				.id = layerId(layers, arc.layer),
				.key = layerKey(layers, arc.layer),
			});
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
				.style = {.fill = kLabelPlateColor, .border = Foundation::BorderStyle{.width = 0.0F, .cornerRadius = label.plateCornerRadius}},
				.id = layerId(layers, FeedbackLayer::LabelPlate),
				.key = layerKey(layers, FeedbackLayer::LabelPlate),
			});
			Renderer::Primitives::drawText({
				.text = label.text,
				.position = label.position,
				.scale = label.sizePx / kRendererFontBaseSizePx,
				.color = label.color,
				.font = fontOf(label.weight),
				.id = layerId(layers, FeedbackLayer::Label),
				.key = layerKey(layers, FeedbackLayer::Label),
			});
		}
	}

} // namespace world_sim
