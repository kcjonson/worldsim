#include "WorkSources.h"

#include "../InventoryMass.h"
#include "../components/Inventory.h"

#include <assets/AssetRegistry.h>

#include <algorithm>
#include <tuple>

namespace ecs {

	void WorkSources::begin(const Memory& memory, const assets::AssetRegistry& registry, const Inventory& inventory) {
		m_memory = &memory;
		m_registry = &registry;
		m_inventory = &inventory;
		m_carryCollected = false;
		m_harvestCollected = false;
	}

	std::span<const CarrySource> WorkSources::carryables() {
		if (!m_carryCollected) {
			collectCarryables();
		}
		return m_carry;
	}

	std::span<const CarrySource> WorkSources::carryablesOf(uint32_t defNameId) {
		const auto all = carryables();
		const auto first =
			std::partition_point(all.begin(), all.end(), [defNameId](const CarrySource& s) { return s.defNameId < defNameId; });
		const auto last = std::partition_point(first, all.end(), [defNameId](const CarrySource& s) { return s.defNameId == defNameId; });
		return {first, last};
	}

	std::span<const HarvestSource> WorkSources::harvestables() {
		if (!m_harvestCollected) {
			collectHarvestables();
		}
		return m_harvest;
	}

	std::span<const HarvestSource> WorkSources::harvestablesYielding(uint32_t yieldDefNameId) {
		const auto all = harvestables();
		const auto first = std::partition_point(all.begin(), all.end(), [yieldDefNameId](const HarvestSource& s) {
			return s.yieldDefNameId < yieldDefNameId;
		});
		const auto last =
			std::partition_point(first, all.end(), [yieldDefNameId](const HarvestSource& s) { return s.yieldDefNameId == yieldDefNameId; });
		return {first, last};
	}

	void WorkSources::collectCarryables() {
		m_carryCollected = true;
		m_carry.clear();
		const auto& keys = m_memory->getEntitiesWithCapability(assets::CapabilityType::Carryable);
		countVisits(keys.size());
		for (uint64_t key : keys) {
			if (const KnownWorldEntity* entity = m_memory->getWorldEntity(key)) {
				m_carry.push_back({entity->defNameId, key, entity, nullptr});
			}
		}
		std::sort(m_carry.begin(), m_carry.end(), [](const CarrySource& a, const CarrySource& b) {
			return std::tie(a.defNameId, a.key) < std::tie(b.defNameId, b.key);
		});

		// One registry lookup per def: a run of one def keeps or drops together.
		auto out = m_carry.begin();
		for (auto run = m_carry.begin(); run != m_carry.end();) {
			const uint32_t defNameId = run->defNameId;
			auto runEnd = std::find_if(run, m_carry.end(), [defNameId](const CarrySource& s) { return s.defNameId != defNameId; });
			const assets::AssetDefinition* def = m_registry->hasCapability(defNameId, assets::CapabilityType::Carryable)
													 ? m_registry->getDefinition(m_registry->getDefName(defNameId))
													 : nullptr;
			if (def != nullptr) {
				for (; run != runEnd; ++run, ++out) {
					*out = *run;
					out->def = def;
				}
			}
			run = runEnd;
		}
		m_carry.erase(out, m_carry.end());
	}

	void WorkSources::collectHarvestables() {
		m_harvestCollected = true;
		m_harvest.clear();
		const auto& keys = m_memory->getEntitiesWithCapability(assets::CapabilityType::Harvestable);
		countVisits(keys.size());
		for (uint64_t key : keys) {
			if (const KnownWorldEntity* entity = m_memory->getWorldEntity(key)) {
				// The def id rides in yieldDefNameId until its def is resolved below.
				m_harvest.push_back({entity->defNameId, key, entity, nullptr, nullptr, false, false});
			}
		}
		std::sort(m_harvest.begin(), m_harvest.end(), [](const HarvestSource& a, const HarvestSource& b) {
			return std::tie(a.yieldDefNameId, a.key) < std::tie(b.yieldDefNameId, b.key);
		});

		auto out = m_harvest.begin();
		for (auto run = m_harvest.begin(); run != m_harvest.end();) {
			const uint32_t defNameId = run->yieldDefNameId;
			auto runEnd = std::find_if(run, m_harvest.end(), [defNameId](const HarvestSource& s) { return s.yieldDefNameId != defNameId; });
			const assets::AssetDefinition* def = m_registry->hasCapability(defNameId, assets::CapabilityType::Harvestable)
													 ? m_registry->getDefinition(m_registry->getDefName(defNameId))
													 : nullptr;
			if (def != nullptr && def->capabilities.harvestable.has_value()) {
				const auto&	   harvestable = def->capabilities.harvestable.value();
				const uint32_t yieldId = m_registry->getDefNameId(harvestable.yieldDefName);
				const auto&	   yieldName = harvestable.yieldDefName;
				const bool	   holdsTool = ecs::inventoryHoldsToolType(*m_inventory, *m_registry, harvestable.requiredToolType);
				const bool	   yieldFits =
					ecs::cargoUnitsThatFit(*m_inventory, *m_registry, yieldName) != 0 && m_inventory->addableCount(yieldName) != 0;
				const assets::AssetDefinition* yieldDef = m_registry->getDefinition(m_registry->getDefName(yieldId));
				for (; run != runEnd; ++run, ++out) {
					*out = {yieldId, run->key, run->entity, def, yieldDef, holdsTool, yieldFits};
				}
			}
			run = runEnd;
		}
		m_harvest.erase(out, m_harvest.end());
		std::sort(m_harvest.begin(), m_harvest.end(), [](const HarvestSource& a, const HarvestSource& b) {
			return std::tie(a.yieldDefNameId, a.key) < std::tie(b.yieldDefNameId, b.key);
		});
	}

} // namespace ecs
