#pragma once

// The pieces of `/api/state?what=construction` that read the construction world alone, kept apart
// from the dev server so a test can build them. Points are {x, y} in world meters, printed to
// three decimals.

#include <construction/ConstructionWorld.h>
#include <ecs/EntityID.h>

#include <functional>
#include <string>

namespace world_sim {

	/// What a blueprint entity adds to its entry, as a fragment that starts with a comma (its phase and
	/// material progress), or empty when the entity carries no blueprint.
	using BlueprintFragment = std::function<std::string(ecs::EntityID entity)>;

	/// The foundation's ring as a JSON array of corners, in the ring's order, the first not repeated at the end.
	std::string foundationCornersJson(const construction::Foundation& foundation);

	/// Every opening as a JSON array: its wall, `fractionAlongWall` (0 at the wall's first vertex, 1 at its
	/// second) and the point that is on the wall, in world meters, with the blueprint fragment of an opening that has an entity.
	std::string openingsJson(const construction::ConstructionWorld& world, const BlueprintFragment& blueprintFragment);

} // namespace world_sim
