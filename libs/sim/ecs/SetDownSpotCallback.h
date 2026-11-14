#pragma once

#include <functional>

#include <glm/vec2.hpp>

namespace ecs {

	/// Where packaged furniture set down at `at` lands: walkable ground off every unbuilt footprint, clear
	/// of what lies around, by the loose-drop rule. The one set-down rule: GameScene gives it to both the
	/// colonist setting an item down (AIDecisionSystem) and the move of items off a site (ConstructionSystem).
	using SetDownSpotCallback = std::function<glm::vec2(glm::vec2 at)>;

} // namespace ecs
