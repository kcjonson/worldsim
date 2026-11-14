#include "scenes/game/dev/GroundRefusal.h"

#include <ecs/systems/NavigationSystem.h>

namespace world_sim {

	std::string foundationGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts) {
		if (navigation == nullptr || navigation->isAreaBuildable(Foundation::toGlmVec2(pts))) {
			return {};
		}
		return "foundation: footprint not on buildable ground (water, a fordable stream, or ground no mover has seen)";
	}

	std::string wallChainGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts) {
		if (navigation == nullptr || navigation->isLineClearToBuild(Foundation::toGlmVec2(pts))) {
			return {};
		}
		return "walls: chain not on clear buildable ground (water, a fordable stream, a placed obstacle, or ground no mover has seen)";
	}

} // namespace world_sim
