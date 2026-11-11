#pragma once

// The world side of ActionSystem's take-source callback: what a completing harvest or pickup finds
// standing where its source was, and how it leaves it. World-gen flora live in the placement index;
// runtime-spawned harvestables (dev spawns, future saplings) are ECS entities with an Appearance.
// The game and the tests both wire this, so there is one definition of "the source is still there",
// and of how a loose pile enters and leaves the pile index a pickup finds it by.

#include "EntityID.h"
#include "systems/ActionSystem.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace assets {
	class PlacementExecutor;
}

namespace ecs {

	class World;

	/// When the source `defNameId` (interned, AssetRegistry::getDefNameId) at `at` is ready to give again while it regrows from a cut, or nullopt when
	/// it is ready at `now`, a time on the world's simulation clock (simulationSeconds). The one answer to "is this
	/// source ready": the decision asks it of the sources a colonist remembers, and takeWorldSource of the
	/// one it takes.
	[[nodiscard]] std::optional<double>
	worldSourceRegrowingUntil(assets::PlacementExecutor& placement, uint32_t defNameId, glm::vec2 at, double now);

	/// Take the source `defName` at `at` from the world and report what it was just before: Gone if
	/// neither the placement index nor a live spawned entity holds it (a loose pile, a ResourceStack,
	/// never counts: piles are LoosePileIndex's), OnCooldown if it stands but is
	/// regrowing, Ready otherwise. Remove takes a placed source out of the index at once and queues a
	/// spawned one on `pendingRemoval` (the caller destroys it after the update, never inside a view),
	/// erasing its cooldown and resource pool either way; a spawned entity already queued there reads
	/// as Gone. Regrow starts its regrowth, `regrowthSeconds` on the simulation clock, unless one is
	/// running. Keep changes nothing.
	/// Make `entity`, already spawned at `position` with the item's Appearance, a loose pile of
	/// `quantity` x `defNameId` and file it in the ActionSystem's pile index.
	void makeLoosePile(World& world, EntityID entity, uint32_t defNameId, glm::vec2 position, uint32_t quantity);

	/// Destroy a world entity outside any view (the deferred removal drain, a dev kill), taking it out
	/// of the pile index first if it is a pile.
	void destroyWorldEntity(World& world, EntityID entity);

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
