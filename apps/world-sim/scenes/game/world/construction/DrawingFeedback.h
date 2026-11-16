#pragma once

// DrawingFeedback - the snap feedback the drawing tools draw over their preview:
// the guides behind the snapped point. One function for every tool, fed from the
// tool's SnapResult, so the foundation and wall previews draw guides one way.

#include "UserSettings.h"

#include <construction/SnapEngine.h>

#include <math/Types.h>

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
		float									 pixelsPerMeter;
	};

	/// Draw the snap's guides, each from its node to the snapped point, in the preview
	/// style's guide colour and width. A guide whose node is the snapped point draws
	/// nothing.
	void drawSnapFeedback(const DrawingFeedbackInput& input);

} // namespace world_sim
