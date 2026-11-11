#pragma once

// The world side of ActionSystem's take-source callback: what a completing harvest or pickup finds
// standing where its source was, and how it leaves it. World-gen flora live in the placement index;
// runtime-spawned harvestables (dev spawns, future saplings) are ECS entities with an Appearance.
// The game and the tests both wire this, so there is one definition of "the source is still there".

#include "EntityID.h"
#include "systems/ActionSystem.h"

#include <glm/vec2.hpp>

#include <optional>
#include <string>
#include <vector>

namespace assets {
	class PlacementExecutor;
}

namespace ecs {

	class World;

	/// When the source `defName` at `at` is ready to give again while it regrows from a cut, or nullopt when
	/// it is ready now, on the world's simulation clock (simulationSeconds). The one answer to "is this
	/// source ready": the decision asks it of the sources a colonist remembers, and takeWorldSource of the
	/// one it takes.
	[[nodiscard]] std::optional<double>
	worldSourceRegrowingUntil(assets::PlacementExecutor& placement, World& world, const std::string& defName, glm::vec2 at);

	/// Take the source `defName` at `at` from the world and report what it was just before: Gone if
	/// neither the placement index nor a live spawned entity holds it, OnCooldown if it stands but is
	/// regrowing, Ready otherwise. Remove takes a placed source out of the index at once and queues a
	/// spawned one on `pendingRemoval` (the caller destroys it after the update, never inside a view),
	/// erasing its cooldown and resource pool either way; a spawned entity already queued there reads
	/// as Gone. Regrow starts its regrowth, `regrowthSeconds` on the simulation clock, unless one is
	/// running. Keep changes nothing.
	[[nodiscard]] ActionSystem::SourceState takeWorldSource(
		assets::PlacementExecutor& placement,
		World&					   world,
		std::vector<EntityID>&	   pendingRemoval,
		const std::string&		   defName,
		glm::vec2				   at,
		ActionSystem::SourceTake   take,
		float					   regrowthSeconds
	);

} // namespace ecs
