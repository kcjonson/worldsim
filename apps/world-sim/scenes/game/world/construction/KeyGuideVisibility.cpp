#include "scenes/game/world/construction/KeyGuideVisibility.h"

namespace world_sim {

	bool keyGuideShown(std::optional<ToolKind> tool, const DrawingSettings& settings) {
		return tool.has_value() && settings.showKeyGuide;
	}

} // namespace world_sim
