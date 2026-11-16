#pragma once

// /api/control?action=gpuhold, the control GameScene registers for perf captures (GpuHold.h): value=0|1
// turns the hold off or on, and target=0.05..0.95 sets the fraction of the frame time it holds. Either may
// be left out, and with neither the control only reads the hold's state back.

#include <map>
#include <optional>
#include <string>

namespace world_sim {

	struct GpuHoldCommand {
		static constexpr float kMinTarget = 0.05F;
		static constexpr float kMaxTarget = 0.95F;

		std::optional<bool>	 enabled;
		std::optional<float> target;
	};

	/// The gpuhold control's parameters; nullopt, with the 400's message in error, when value isn't 0 or 1
	/// or target isn't a finite number from kMinTarget to kMaxTarget.
	std::optional<GpuHoldCommand> parseGpuHoldCommand(const std::map<std::string, std::string>& params, std::string& error);

} // namespace world_sim
