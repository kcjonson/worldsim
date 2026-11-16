#include "scenes/game/dev/GpuHoldCommand.h"

#include <cmath>
#include <cstdlib>

namespace world_sim {

	std::optional<GpuHoldCommand> parseGpuHoldCommand(const std::map<std::string, std::string>& params, std::string& error) {
		GpuHoldCommand command;
		if (const auto it = params.find("value"); it != params.end()) {
			if (it->second != "0" && it->second != "1") {
				error = "gpuhold: value must be 0 or 1";
				return std::nullopt;
			}
			command.enabled = it->second == "1";
		}
		if (const auto it = params.find("target"); it != params.end()) {
			const char* text = it->second.c_str();
			char*		end = nullptr;
			const float parsed = std::strtof(text, &end);
			if (end == text || *end != '\0' || !std::isfinite(parsed) || parsed < GpuHoldCommand::kMinTarget ||
				parsed > GpuHoldCommand::kMaxTarget) {
				error = "gpuhold: target must be a number from 0.05 to 0.95";
				return std::nullopt;
			}
			command.target = parsed;
		}
		return command;
	}

} // namespace world_sim
