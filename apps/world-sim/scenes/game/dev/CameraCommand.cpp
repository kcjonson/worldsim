#include "scenes/game/dev/CameraCommand.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace world_sim {

	std::optional<CameraCommand> parseCameraCommand(const std::map<std::string, std::string>& params, const char*& error) {
		const auto has = [&params](const char* name) {
			return params.contains(name);
		};
		const auto parseFloat = [&params](const char* name, float& out) -> bool {
			const auto it = params.find(name);
			if (it == params.end()) {
				return false;
			}
			const std::string& value = it->second;
			char*			   end = nullptr;
			const float		   parsed = std::strtof(value.c_str(), &end);
			if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
				return false;
			}
			out = parsed;
			return true;
		};

		CameraCommand cmd;
		bool		  malformed = false;
		if (has("x") || has("y")) {
			cmd.hasPosition = parseFloat("x", cmd.x) && parseFloat("y", cmd.y);
			malformed |= !cmd.hasPosition;
		}
		if (has("zoom")) {
			cmd.hasZoom = parseFloat("zoom", cmd.zoom);
			malformed |= !cmd.hasZoom;
		}
		if (has("panx") || has("pany")) {
			cmd.hasPan = true;
			if (has("panx") && !parseFloat("panx", cmd.panX)) {
				malformed = true;
			}
			if (has("pany") && !parseFloat("pany", cmd.panY)) {
				malformed = true;
			}
			cmd.panX = std::clamp(cmd.panX, -1.0F, 1.0F);
			cmd.panY = std::clamp(cmd.panY, -1.0F, 1.0F);
		}
		if (malformed) {
			error = "Camera parameters must be finite numbers (x+y together, zoom, panx, pany)";
			return std::nullopt;
		}
		if (!cmd.hasPosition && !cmd.hasZoom && !cmd.hasPan) {
			error = "Camera action requires x+y, zoom, panx, or pany parameters";
			return std::nullopt;
		}
		return cmd;
	}

} // namespace world_sim
