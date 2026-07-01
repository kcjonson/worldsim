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

	// Current activity, for the roster tile's task meter. `activity` is a concise verb
	// ("Harvesting"), empty when idle. `activityProgress` is 0..1 once the action is
	// running, or <0 while the colonist is still traveling to it (tile shows an empty
	// track in that case).
	std::string activity;
	float activityProgress = -1.0F;

	// True when the colonist is under direct player control (drives the roster tile's
	// controlled-state styling so the player can spot the driven colonist in the list).
	bool playerControlled = false;
};

/// A colonist's current activity: a concise label and the running action's progress.
struct ColonistActivity {
	std::string label;	  // "" when idle
	float		progress;  // 0..1 while the action runs; <0 before it starts / idle
};

/// Current activity + action progress for one colonist. Shared by the roster tile and
/// the dossier so both read progress the same way (construction reads the blueprint's
/// workDone; everything else reads the action clock).
[[nodiscard]] ColonistActivity getColonistActivity(ecs::World& world, ecs::EntityID colonist);

/// Query all colonists from the ECS world
/// @param world The ECS world to query
/// @return Vector of colonist data, one per colonist entity
/// @note Takes non-const world because ecs::World::view() is not const
[[nodiscard]] std::vector<ColonistData> getColonists(ecs::World& world);

} // namespace world_sim::adapters
