#pragma once

// /api/control?action=camera, the control GameScene registers, which it applies in its next update.
// Only what the request names applies: x and y together (world position), zoom (factor), and panx,
// pany (-1..1, a direction held like the movement keys until set back to 0).

#include <map>
#include <optional>
#include <string>

namespace world_sim {

	struct CameraCommand {
		bool  hasPosition = false;
		float x = 0.0F;
		float y = 0.0F;
		bool  hasZoom = false;
		float zoom = 0.0F;
		bool  hasPan = false;
		float panX = 0.0F;
		float panY = 0.0F;
	};

	/// The camera control's parameters; nullopt, with the 400's message in error, when one isn't a
	/// finite number, x or y comes alone, or none is given.
	std::optional<CameraCommand> parseCameraCommand(const std::map<std::string, std::string>& params, const char*& error);

} // namespace world_sim
