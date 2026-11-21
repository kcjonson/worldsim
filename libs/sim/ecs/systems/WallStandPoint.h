#pragma once

#include <construction/WallStand.h>

#include <glm/vec2.hpp>

#include <optional>
#include <span>

namespace ecs {

	class NavigationSystem;

	/// Where a builder of `agentRadius` stands to work side `stand` of a wall: the mesh's nearest standing
	/// point for the first of the side's stand points (construction::WallStand::standPoints, the middle
	/// first) that serves the side; nullopt when none does, as when the ground there is water or taken.
	[[nodiscard]] std::optional<glm::vec2> wallStandPoint(const construction::WallStand& stand, float agentRadius, const NavigationSystem& nav);

	/// Whether none of a wall's allowed sides `stands` can be stood on by a builder of `agentRadius`, so it
	/// cannot be built: what its panel says. False with no mesh yet, or no sides to judge.
	[[nodiscard]] bool noGroundToBuildFrom(std::span<const construction::WallStand> stands, float agentRadius, const NavigationSystem& nav);

} // namespace ecs
