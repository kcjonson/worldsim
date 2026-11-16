#pragma once

// DrawingFeedback - the snap feedback the drawing tools draw over their preview: the
// guides behind the snapped point, their markers and distance labels, the segment's
// length label, the first point's close ring and halo, and the snapped-point marker.
// One rule: solid is geometry, dotted and part transparent is alignment, small solid
// markers state a fact. Every size is screen pixels and never scales with zoom.
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

	struct DrawingFeedbackInput {
		ToolKind								 tool;
		const construction::SnapResult&			 snap;
		const std::vector<Foundation::Vec2>&	 points; // placed points, world meters
		Foundation::Vec2						 cursor; // the snapped cursor, world meters
		const DrawingSettings&					 display;
		const assets::PreviewStyle&				 style;
		const world::WorldCamera&				 camera;
		int										 viewportWidth;
		int										 viewportHeight;
		float									 pixelsPerMeter; // before the camera's zoom, as worldToScreen takes it
		float									 closeRadiusMeters; // the first point's close radius, zoom-floored
		bool									 canClose;			// enough points placed for a click on the first point to close
		bool									 willClose;			// the next click closes the shape
	};

	/// The order things draw in, each above the one before.
	enum class FeedbackLayer {
		Alignment,	   // guides and the close ring
		Marker,		   // right-angle markers, the crossing dot, the close halo
		Label,		   // the segment label and guide distance labels
		CursorOutline, // the dark strokes under the snapped-point marker's ticks
		Cursor,		   // the snapped-point marker
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

	/// Text with a dark halo; `position` is its top-left corner in screen pixels.
	struct FeedbackLabel {
		std::string		  text;
		Foundation::Vec2  position;
		float			  sizePx = 0.0F;
		float			  width = 0.0F;
		Foundation::Color color;
	};

	struct DrawingFeedbackPlan {
		std::vector<FeedbackStroke> strokes;
		std::vector<FeedbackRing>	rings;
		std::vector<FeedbackDot>	dots;
		std::vector<FeedbackLabel>	labels;
	};

	/// Width in pixels of `text` drawn `sizePx` tall.
	using MeasureTextWidth = std::function<float(const std::string& text, float sizePx)>;

	/// Decide the feedback for one frame. No renderer calls: text is measured through
	/// `measure`.
	[[nodiscard]] DrawingFeedbackPlan planSnapFeedback(const DrawingFeedbackInput& input, const MeasureTextWidth& measure);

	/// Plan the feedback and draw it with the renderer's primitives.
	void drawSnapFeedback(const DrawingFeedbackInput& input);

} // namespace world_sim
