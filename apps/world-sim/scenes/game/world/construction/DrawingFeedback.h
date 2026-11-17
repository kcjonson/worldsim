#pragma once

// DrawingFeedback - the snap feedback the drawing tools draw over their preview: the
// guides behind the snapped point, their markers and distance labels, the segment's
// length label, the common-length marks, the angle readout, the first point's close ring
// and halo, the snapped-point marker, and the wall tool's snap-target ring. The Rectangle
// tool draws the snapped-point marker and its own: the size label, a cross on the grid
// point, and a ring on an existing corner or edge.
// One rule: solid is geometry, dotted and part transparent is alignment, small solid
// markers state a fact. Sizes are window pixels and never scale with zoom; only the
// label text and its plate are in ui px, following the interface size.
//
// planSnapFeedback decides what to draw and where (pure, tested); drawSnapFeedback
// draws that plan.

#include "UserSettings.h"

#include <construction/RectangleSnap.h>
#include <construction/SnapEngine.h>

#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <math/Types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace assets {
	struct PreviewStyle;
}

namespace world {
	class WorldCamera;
}

namespace world_sim {

	enum class ToolKind;

	/// A box in screen pixels.
	struct FeedbackBox {
		float left = 0.0F;
		float top = 0.0F;
		float right = 0.0F;
		float bottom = 0.0F;
	};

	struct DrawingFeedbackInput {
		ToolKind							 tool;
		const construction::SnapResult&		 snap;
		const std::vector<Foundation::Vec2>& points; // placed points, world meters
		Foundation::Vec2					 cursor; // the snapped cursor, world meters
		bool								 freeform = false; // Alt held for the snap: no angle readout
		const DrawingSettings&				 display;
		const assets::PreviewStyle&			 style;
		const world::WorldCamera&			 camera;
		int									 viewportWidth;
		int									 viewportHeight;
		float								 uiScale; // screen pixels per ui px, the interface size's scale, positive; sizes the labels only
		float								 pixelsPerMeter;	// before the camera's zoom, as worldToScreen takes it
		float								 closeRadiusMeters; // the first point's close radius, zoom-floored, as the snap was given it
		bool								 canClose;			// enough points placed for a click on the first point to close
		bool								 willClose;			// the next click closes the shape and commits it: the close halo draws
		bool								 clickCloses = false; // the tool's close: the next click closes the shape, on the first point or by the near-start rescue; the angle readout reads the closed shape
		bool								 snappedPointInvalid = false; // off buildable ground, or a refused close: the marker's dot takes invalidColor
		Foundation::Color					 invalidColor{};
		Foundation::Color					 accentColor{}; // a length label that matches a kept common length, from the UI theme
		float								 commonLengthMergeMeters = 0.0F; // a length this close to a kept common length matches it
		std::optional<FeedbackBox>			 keepClear; // something else the preview draws there (a rejection reason): labels keep off it
		construction::RectangleSnapKind		 rectangleSnap = construction::RectangleSnapKind::None; // the Rectangle tool: what placed the corner
		std::string							 sizeLabel; // the Rectangle tool: the pending rectangle's size ("4 m x 3 m"), empty while there is none
	};

	/// The order things draw in, each above the one before.
	enum class FeedbackLayer {
		Alignment,	   // guides, the close ring, the matched edges' faint bands, and the Rectangle tool's grid cross
		Marker,		   // right-angle markers, the close halo, the common-length ticks, the angle readout's arcs, and the Rectangle tool's rings
		LabelPlate,	   // the dark plate behind each label
		Label,		   // the segment label, the size label, guide distance labels, and the angle readout's degrees
		CursorOutline, // the dark strokes under the snapped-point marker's ticks
		Cursor,		   // the snapped-point marker
		Target,		   // over the marker: the crossing dot and the wall tool's snap-target ring
	};

	/// The renderer's draw-order key `tool`'s feedback draws `layer` at.
	[[nodiscard]] std::int32_t feedbackLayerKey(ToolKind tool, FeedbackLayer layer);

	/// An open stroke in screen pixels: a guide, a tick, or a band (two points), or a
	/// right-angle marker (three).
	struct FeedbackStroke {
		std::array<Foundation::Vec2, 3>		   points{};
		std::size_t							   pointCount = 0;
		Foundation::Color					   color;
		float								   width = 1.0F;
		std::optional<Foundation::DashPattern> dash;
		FeedbackLayer						   layer = FeedbackLayer::Alignment;
	};

	/// A ring in screen pixels, dashed or solid.
	struct FeedbackRing {
		Foundation::Vec2					   center;
		float								   radius = 0.0F;
		Foundation::Color					   color;
		float								   width = 1.0F;
		std::optional<Foundation::DashPattern> dash;
		FeedbackLayer						   layer = FeedbackLayer::Alignment;
	};

	/// A stroked arc in screen pixels: `width` wide, centered on `radius`. Angles are radians
	/// from +x in screen space (y down), a positive sweep turning clockwise on screen.
	struct FeedbackArc {
		Foundation::Vec2  center;
		float			  radius = 0.0F;
		float			  startRadians = 0.0F;
		float			  sweepRadians = 0.0F;
		Foundation::Color color;
		float			  width = 1.0F;
		FeedbackLayer	  layer = FeedbackLayer::Marker;
	};

	/// A filled dot in screen pixels, with an optional outline.
	struct FeedbackDot {
		Foundation::Vec2  center;
		float			  radius = 0.0F;
		Foundation::Color fill;
		Foundation::Color outline;
		float			  outlineWidth = 0.0F;
		FeedbackLayer	  layer = FeedbackLayer::Marker;
	};

	/// A label's weight. Bold is a matched length's label.
	enum class FeedbackTextWeight {
		Regular,
		Bold,
	};

	/// Text on a dark plate. `position` is the text's top-left corner in screen pixels;
	/// `plate` is the plate behind it, the text's measured size plus a margin all round,
	/// which is also the box other labels keep clear of.
	struct FeedbackLabel {
		std::string		   text;
		Foundation::Vec2   position;
		float			   sizePx = 0.0F;
		FeedbackBox		   plate;
		float			   plateCornerRadius = 0.0F;
		Foundation::Color  color;
		FeedbackTextWeight weight = FeedbackTextWeight::Regular;
	};

	struct DrawingFeedbackPlan {
		std::vector<FeedbackStroke> strokes;
		std::vector<FeedbackRing>	rings;
		std::vector<FeedbackArc>	arcs;
		std::vector<FeedbackDot>	dots;
		std::vector<FeedbackLabel>	labels;
	};

	/// The renderer's text scale of 1 is this font size.
	inline constexpr float kRendererFontBaseSizePx = 16.0F;

	/// Width and line height in pixels of `text` at a font size of `sizePx` and `weight`.
	using MeasureTextSize = std::function<Foundation::Vec2(const std::string& text, float sizePx, FeedbackTextWeight weight)>;

	/// Decide the feedback for one frame. No renderer calls: text is measured through
	/// `measure`.
	[[nodiscard]] DrawingFeedbackPlan planSnapFeedback(const DrawingFeedbackInput& input, const MeasureTextSize& measure);

	/// The renderer's text measure, the one drawSnapFeedback plans with: width and line
	/// height of `text` at `sizePx` in `weight`'s face.
	[[nodiscard]] Foundation::Vec2 measureFeedbackText(const std::string& text, float sizePx, FeedbackTextWeight weight);

	/// Plan the feedback and draw it with the renderer's primitives.
	void drawSnapFeedback(const DrawingFeedbackInput& input);

	/// The key guide's legend glyphs (architecture D15): each helper's mark as the preview draws it.
	enum class LegendGlyph {
		Guide,		  // a dotted guide
		RightAngle,	  // two edges and the right-angle marker
		Crossing,	  // two dotted guides and the crossing dot
		CommonLength, // an edge with its matched band and the double tick
		AngleArc,	  // two edges and the angle readout's arc
	};

	/// A legend glyph's box, in the ui px of the panel it sits in.
	inline constexpr float kLegendGlyphWidth = 32.0F;
	inline constexpr float kLegendGlyphHeight = 14.0F;

	/// `glyph` in a kLegendGlyphWidth by kLegendGlyphHeight box at the origin, planned with the
	/// preview's own strokes, markers, and look keys. `geometryColor` is the solid edges', the
	/// preview's valid color.
	[[nodiscard]] DrawingFeedbackPlan
	planLegendGlyph(LegendGlyph glyph, const assets::PreviewStyle& style, Foundation::Color geometryColor);

	/// Draw `plan` in a ui component's space: every layer at its draw key `key`, in the plan's
	/// order, each color's alpha times `opacity`.
	void drawFeedbackPlan(const DrawingFeedbackPlan& plan, std::int32_t key, float opacity);

} // namespace world_sim
