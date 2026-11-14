#include "scenes/game/dev/GroundRefusal.h"

#include "scenes/game/world/placement/PlacementSystem.h"

#include <ecs/systems/NavigationSystem.h>

namespace world_sim {

	std::string foundationGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts) {
		if (navigation == nullptr || navigation->isAreaBuildableOnceCleared(Foundation::toGlmVec2(pts))) {
			return {};
		}
		return "foundation: footprint not on buildable ground (water, a fordable stream, or ground no mover has seen)";
	}

	std::string wallChainGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts) {
		if (navigation == nullptr || navigation->isLineBuildableNow(Foundation::toGlmVec2(pts))) {
			return {};
		}
		return "walls: chain not on clear buildable ground (water, a fordable stream, a placed obstacle, or ground no mover has seen)";
	}

	std::string spawnGroundRefusal(
		const PlacementSystem& placement, const ecs::NavigationSystem* navigation, const std::string& def, Foundation::Vec2 at
	) {
		const std::string where = "spawn: " + def + " at (" + std::to_string(at.x) + ", " + std::to_string(at.y) + ") ";
		if (navigation == nullptr) {
			return where + "has no nav mesh to stand on";
		}
		if (PlacementSystem::isFurniture(def)) {
			if (placement.footprintBuildableNow(def, at)) {
				return {};
			}
			return where + "is not on clear buildable ground (water, a fordable stream, a placed obstacle, or ground no mover has seen)";
		}
		if (navigation->isValidPosition({at.x, at.y})) {
			return {};
		}
		return where + (navigation->inSimArea({at.x, at.y}) ? "is water or a placed obstacle" : "is ground no mover has seen (off the nav mesh)");
	}

} // namespace world_sim
