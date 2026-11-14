#pragma once

// The ground gates of the dev verbs that build or spawn, as the 400's message: empty when the
// ground takes the thing. No nav system wired (headless) takes any foundation or wall; validity is
// owned elsewhere there.

#include <math/Types.h> // Foundation::Vec2

#include <string>
#include <vector>

namespace ecs {
	class NavigationSystem;
}

namespace world_sim {

	class PlacementSystem;

	/// The foundation verb's: the whole closed footprint `pts`, interior included, is buildable
	/// (NavigationSystem::isAreaBuildableOnceCleared: placed obstacles become clear tasks; water, a ford, and
	/// ground no mover has seen refuse), the same gate as the foundation tool.
	std::string foundationGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts);

	/// The walls verb's: every segment of the chain `pts` is clear ground out of any ford
	/// (NavigationSystem::isLineBuildableNow), the same gate as the wall tool, hosted or freestanding.
	std::string wallChainGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts);

	/// The spawn verb's, for `def` at `at`. Furniture (PlacementSystem::isFurniture) goes where the
	/// placement tool would set it up (footprintBuildableNow, never a ford); anything else, a tree, a
	/// reed, or a loose item, goes on clear ground (isValidPosition), a ford included, since putting
	/// it there builds nothing. With no nav system nothing is taken.
	std::string spawnGroundRefusal(
		const PlacementSystem& placement, const ecs::NavigationSystem* navigation, const std::string& def, Foundation::Vec2 at
	);

} // namespace world_sim
