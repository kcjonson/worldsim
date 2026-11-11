#pragma once

// Shared helpers for the goal-generating systems (CraftingGoalSystem, StorageGoalSystem,
// ConstructionSystem). Each was duplicated across two systems; this is the single home so
// "does any colonist know a source" and "one trip's carry budget" have one definition.

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Needs.h"
#include "../components/StorageConfiguration.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace ecs {

	/// The storage boxes any colonist remembers. A storage-to-storage pull may only source from a box
	/// the colony actually knows about (no god-view), mirroring how a loose pickup is gated on the pile
	/// being in memory. A box is recorded in memory by its Appearance defName + Position (the same key
	/// VisionSystem's Pass 2 uses when it sees the box). A colony-placed/-built box is normally already
	/// in memory.
	///
	/// The answer for every box is worked out on the first question after invalidate(), one probe of
	/// each colonist's memory per box, and later questions are a set lookup.
	class ColonyStorageKnowledge {
	  public:
		void invalidate() { m_built = false; }

		[[nodiscard]] bool knows(World& world, const assets::AssetRegistry& reg, EntityID box) {
			if (!m_built) {
				build(world, reg);
			}
			return m_known.contains(box);
		}

#ifdef DEVELOPMENT_BUILD
		/// Builds since construction; tests read it to show the walk runs once per AI update.
		uint64_t builds = 0;
#endif

	  private:
		void build(World& world, const assets::AssetRegistry& reg) {
			m_built = true;
			m_known.clear();
#ifdef DEVELOPMENT_BUILD
			++builds;
#endif
			for (auto [box, config, appearance, boxPos] : world.view<StorageConfiguration, Appearance, Position>()) {
				(void)config;
				const uint32_t defNameId = reg.getDefNameId(appearance.defName);
				if (defNameId == 0) {
					continue;
				}
				for (auto [colonist, memory] : world.view<Memory>()) {
					(void)colonist;
					if (memory.knowsWorldEntity(boxPos.value, defNameId)) {
						m_known.insert(box);
						break;
					}
				}
			}
		}

		bool						 m_built = false;
		std::unordered_set<EntityID> m_known;
	};

	/// Does any colonist remember a harvestable whose yield is this item (e.g. a tree for Wood)?
	/// Colony "availability" = the union of colonist memories (no god-view). A goal created from
	/// this is still only fulfilled by a colonist that actually remembers the source.
	[[nodiscard]] inline bool colonyKnowsHarvestableSource(World* world, const assets::AssetRegistry& reg, uint32_t yieldDefNameId) {
		for (auto [colonist, memory] : world->view<Memory>()) {
			(void)colonist;
			for (uint64_t key : memory.getEntitiesWithCapability(assets::CapabilityType::Harvestable)) {
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
