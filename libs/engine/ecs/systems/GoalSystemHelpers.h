#pragma once

// Shared helpers for the goal-generating systems (CraftingGoalSystem, StorageGoalSystem,
// ConstructionSystem). Each was duplicated across two systems; this is the single home so
// "does any colonist know a source" and "one trip's carry budget" have one definition.

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Needs.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>

#include <algorithm>
#include <cstdint>

namespace ecs {

	/// Does any colonist remember this storage box? A storage-to-storage pull may only source from a
	/// box the colony actually knows about (no god-view), mirroring how a loose pickup is gated on the
	/// pile being in memory. A box is recorded in memory by its Appearance defName + Position (the same
	/// key VisionSystem's Pass 2 uses when it sees the box), so resolve those from the entity and test
	/// every colonist's memory. A colony-placed/-built box is normally already in memory.
	[[nodiscard]] inline bool colonyKnowsStorageEntity(World* world, const engine::assets::AssetRegistry& reg, EntityID box) {
		const auto* appearance = world->getComponent<Appearance>(box);
		const auto* boxPos = world->getComponent<Position>(box);
		if (appearance == nullptr || boxPos == nullptr) {
			return false;
		}
		const uint32_t defNameId = reg.getDefNameId(appearance->defName);
		if (defNameId == 0) {
			return false;
		}
		for (auto [colonist, memory] : world->view<Memory>()) {
			(void)colonist;
			if (memory.knowsWorldEntity(boxPos->value, defNameId)) {
				return true;
			}
		}
		return false;
	}

	/// Does any colonist remember a harvestable whose yield is this item (e.g. a tree for Wood)?
	/// Colony "availability" = the union of colonist memories (no god-view). A goal created from
	/// this is still only fulfilled by a colonist that actually remembers the source.
	[[nodiscard]] inline bool
	colonyKnowsHarvestableSource(World* world, const engine::assets::AssetRegistry& reg, uint32_t yieldDefNameId) {
		for (auto [colonist, memory] : world->view<Memory>()) {
			(void)colonist;
			for (uint64_t key : memory.getEntitiesWithCapability(engine::assets::CapabilityType::Harvestable)) {
				const KnownWorldEntity* known = memory.getWorldEntity(key);
				if (known == nullptr) {
					continue;
				}
				const auto& srcDefName = reg.getDefName(known->defNameId);
				const auto* srcDef = reg.getDefinition(srcDefName);
				if (srcDef != nullptr && srcDef->capabilities.harvestable.has_value() &&
					reg.getDefNameId(srcDef->capabilities.harvestable->yieldDefName) == yieldDefNameId) {
					return true;
				}
			}
		}
		return false;
	}

	/// One trip's carry budget for sizing stocking/material harvests: the largest carry weight
	/// among colonists (a max, not a sum or first-hit, so it's independent of view iteration order
	/// and stays deterministic). Falls back to the colonist default in a headless/unit context with
	/// no colonists yet.
	[[nodiscard]] inline float colonistCarryCapacityKg(World* world) {
		float capacity = 0.0F;
		for (auto [entity, needs, inventory] : world->view<NeedsComponent, Inventory>()) {
			(void)entity;
			(void)needs;
			capacity = std::max(capacity, inventory.carryCapacityKg);
		}
		return capacity > 0.0F ? capacity : Inventory::createForColonist().carryCapacityKg;
	}

} // namespace ecs
