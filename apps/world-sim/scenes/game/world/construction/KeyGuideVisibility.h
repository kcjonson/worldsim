#pragma once

// Whether the key guide is on screen. The guide draws it and `what=drawing` reports it.

#include "UserSettings.h"
#include "scenes/game/world/construction/DrawingSystem.h"

#include <optional>

namespace world_sim {

	/// Whether the panel is on screen: a drawing tool is active and Show key guide is on. Folded or open is
	/// separate; a folded guide is still shown, as its chip.
	[[nodiscard]] bool keyGuideShown(std::optional<ToolKind> tool, const DrawingSettings& settings);

} // namespace world_sim
