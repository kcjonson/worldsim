#pragma once

// DrawingFeedback - the snap feedback the drawing tools draw over their preview: the
// guides behind the snapped point, their markers and distance labels, the segment's
// length label, the first point's close ring and halo, the snapped-point marker, and
// the wall tool's snap-target ring.
// One rule: solid is geometry, dotted and part transparent is alignment, small solid
// markers state a fact. Sizes are window pixels and never scale with zoom; only the
// label text and its plate are in ui px, following the interface size.
//
// planSnapFeedback decides what to draw and where (pure, tested); drawSnapFeedback
// draws that plan.

#include "UserSettings.h"

#include <construction/SnapEngine.h>

#include <graphics/Color.h>
#include <graphics/PrimitiveStyles.h>
#include <math/Types.h>

#include <array>
#include <cstddef>
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
		const DrawingSettings&				 display;
		const assets::PreviewStyle&			 style;
		const world::WorldCamera&			 camera;
		int									 viewportWidth;
		int									 viewportHeight;
		float								 uiScale; // screen pixels per ui px, the interface size's scale, positive; sizes the labels only
		float								 pixelsPerMeter;	// before the camera's zoom, as worldToScreen takes it
		float								 closeRadiusMeters; // the first point's close radius, zoom-floored
		bool								 canClose;			// enough points placed for a click on the first point to close
		bool								 willClose;			// the next click closes the shape and commits it: the close halo draws
		bool								 snappedPointInvalid = false; // off buildable ground, or a refused close: the marker's dot takes invalidColor
		Foundation::Color					 invalidColor{};
		std::optional<FeedbackBox>			 keepClear; // something else the preview draws there (a rejection reason): labels keep off it
	};

	/// The order things draw in, each above the one before.
	enum class FeedbackLayer {
		Alignment,	   // guides and the close ring
		Marker,		   // right-angle markers and the close halo
		LabelPlate,	   // the dark plate behind each label
		Label,		   // the segment label and guide distance labels
		CursorOutline, // the dark strokes under the snapped-point marker's ticks
		Cursor,		   // the snapped-point marker
		Target,		   // over the marker: the crossing dot and the wall tool's snap-target ring
	};

	/// An open stroke in screen pixels: a guide (two points) or a right-angle marker (three).
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

	/// A filled dot in screen pixels, with an optional outline.
	struct FeedbackDot {
		Foundation::Vec2  center;
		float			  radius = 0.0F;
		Foundation::Color fill;
		Foundation::Color outline;
		float			  outlineWidth = 0.0F;
		FeedbackLayer	  layer = FeedbackLayer::Marker;
	};

	/// Text on a dark plate. `position` is the text's top-left corner in screen pixels;
	/// `plate` is the plate behind it, the text's measured size plus a margin all round,
	/// which is also the box other labels keep clear of.
	struct FeedbackLabel {
		std::string		  text;
		Foundation::Vec2  position;
		float			  sizePx = 0.0F;
		FeedbackBox		  plate;
		float			  plateCornerRadius = 0.0F;
		Foundation::Color color;
	};

	struct DrawingFeedbackPlan {
		std::vector<FeedbackStroke> strokes;
		std::vector<FeedbackRing>	rings;
		std::vector<FeedbackDot>	dots;
		std::vector<FeedbackLabel>	labels;
	};

	/// The renderer's text scale of 1 is this font size.
	inline constexpr float kRendererFontBaseSizePx = 16.0F;

	/// Width and line height in pixels of `text` at a font size of `sizePx`.
	using MeasureTextSize = std::function<Foundation::Vec2(const std::string& text, float sizePx)>;

	/// Decide the feedback for one frame. No renderer calls: text is measured through
	/// `measure`.
	[[nodiscard]] DrawingFeedbackPlan planSnapFeedback(const DrawingFeedbackInput& input, const MeasureTextSize& measure);

	/// The renderer's text measure, the one drawSnapFeedback plans with: width and line
	/// height of `text` at `sizePx`.
	[[nodiscard]] Foundation::Vec2 measureFeedbackText(const std::string& text, float sizePx);

	/// Plan the feedback and draw it with the renderer's primitives.
	void drawSnapFeedback(const DrawingFeedbackInput& input);

} // namespace world_sim
