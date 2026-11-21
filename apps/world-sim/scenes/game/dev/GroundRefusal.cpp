#include "scenes/game/dev/GroundRefusal.h"

#include "scenes/game/world/placement/PlacementSystem.h"

#include <ecs/systems/NavigationSystem.h>

namespace world_sim {

	std::string foundationGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts) {
		if (navigation == nullptr || navigation->isAreaBuildableOnceCleared(Foundation::toGlmVec2(pts))) {
			return {};
		}
		return "foundation: footprint not on buildable ground (water, a fordable stream, or ground no mover has meshed)";
	}

	std::string wallChainGroundRefusal(const ecs::NavigationSystem* navigation, const std::vector<Foundation::Vec2>& pts) {
		if (navigation == nullptr || navigation->isLineBuildableNow(Foundation::toGlmVec2(pts))) {
			return {};
		}
		return "walls: chain not on clear buildable ground (water, a fordable stream, a placed obstacle, or ground no mover has meshed)";
	}

	std::string spawnGroundRefusal(
		const PlacementSystem& placement, const ecs::NavigationSystem* navigation, const std::string& def, Foundation::Vec2 at, uint32_t turnQuanta
	) {
		const auto where = [&] { return "spawn: " + def + " at (" + std::to_string(at.x) + ", " + std::to_string(at.y) + ") "; };
		if (navigation == nullptr) {
			return where() + "has no nav mesh to stand on";
		}
		if (PlacementSystem::isFurniture(def)) {
			if (placement.insideUnbuiltSite(at)) {
				return where() + "is inside a foundation that is not built yet";
			}
			if (placement.footprintBuildableNow(def, at, turnQuanta)) {
				return {};
			}
			return where() + "is not on clear buildable ground (water, a fordable stream, a placed obstacle, or ground no mover has meshed)";
		}
		const std::string refusal = putDownRefusal(navigation, at);
		return refusal.empty() ? refusal : "spawn: " + def + " at " + refusal;
	}

	std::string putDownRefusal(const ecs::NavigationSystem* navigation, Foundation::Vec2 at) {
		const std::string where = "(" + std::to_string(at.x) + ", " + std::to_string(at.y) + ") ";
		if (navigation == nullptr) {
			return where + "has no nav mesh to stand on";
		}
		if (navigation->isValidPosition({at.x, at.y})) {
			return {};
		}
		return where + (navigation->inSimArea({at.x, at.y}) ? "is water or a placed obstacle" : "is ground no mover has meshed (off the nav mesh)");
	}

	std::string moverGroundRefusal(const ecs::NavigationSystem* navigation, Foundation::Vec2 at) {
		if (navigation != nullptr && !navigation->inSimArea({at.x, at.y})) {
			return {};
		}
		return putDownRefusal(navigation, at);
	}

} // namespace world_sim
