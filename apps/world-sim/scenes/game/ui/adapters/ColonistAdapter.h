#pragma once

// ColonistAdapter - Centralizes ECS queries for colonist data
//
// This adapter isolates ECS knowledge from the ViewModel layer.
// The ViewModel calls getColonists() and receives domain types,
// without needing to know about ecs::Colonist, NeedsComponent, etc.

#include <ecs/EntityID.h>
#include <ecs/World.h>

#include <string>
#include <vector>

namespace world_sim::adapters {

/// Data for a single colonist (extracted from ECS)
struct ColonistData {
	ecs::EntityID id;
	std::string name;
	float mood;  // 0-100, computed from needs
};

/// Query all colonists from the ECS world
/// @param world The ECS world to query
/// @return Vector of colonist data, one per colonist entity
/// @note Takes non-const world because ecs::World::view() is not const
[[nodiscard]] std::vector<ColonistData> getColonists(ecs::World& world);

} // namespace world_sim::adapters
