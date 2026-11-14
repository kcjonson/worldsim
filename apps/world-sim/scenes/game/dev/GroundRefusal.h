#pragma once

// The ground gates of the dev verbs that build, as the 400's message: empty when the ground takes
// the structure. No nav system wired (headless) takes anything; validity is owned elsewhere there.

#include <math/Types.h> // Foundation::Vec2

#include <string>
#include <vector>

namespace ecs {
	class NavigationSystem;
}

namespace world_sim {

	/// The foundation verb's: the whole closed footprint `pts`, interior included, is buildable
	/// (NavigationSystem::isAreaBuildableOnceCleared: placed obstacles become clear tasks; water, a ford, and
	/// ground no mover has seen refuse), the same gate as the foundation tool.
	std::string foundationGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts);

	/// The walls verb's: every segment of the chain `pts` is clear ground out of any ford
	/// (NavigationSystem::isLineBuildableNow), the same gate as the wall tool, hosted or freestanding.
	std::string wallChainGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts);

} // namespace world_sim
