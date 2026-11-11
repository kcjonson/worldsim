#include "WorkSources.h"

#include "../InventoryMass.h"
#include "../components/Inventory.h"

#include <assets/AssetRegistry.h>

#include <algorithm>

namespace ecs {

	namespace {
		[[nodiscard]] bool rememberedAs(const KnownWorldEntity& entity, assets::CapabilityType capability) {
			return (entity.capabilityMask & (1U << static_cast<uint32_t>(capability))) != 0;
		}
	} // namespace

	void WorkSources::begin(const Memory& memory, const assets::AssetRegistry& registry, const Inventory& inventory) {
		m_memory = &memory;
		m_registry = &registry;
		m_inventory = &inventory;
		++m_evaluation;
		m_carryDefsListed = false;
	}

	const assets::AssetDefinition* WorkSources::knownCarryable(uint32_t defNameId) const {
		if (!m_registry->hasCapability(defNameId, assets::CapabilityType::Carryable)) {
			return nullptr;
		}
		const assets::AssetDefinition* def = m_registry->getDefinitionById(defNameId);
		if (def == nullptr) {
			return nullptr;
		}
		for (uint64_t key : m_memory->worldEntitiesOfDef(defNameId)) {
			if (rememberedAs(*m_memory->getWorldEntity(key), assets::CapabilityType::Carryable)) {
				return def;
			}
		}
		return nullptr;
	}

	std::span<const CarrySource> WorkSources::carryablesOf(uint32_t defNameId) {
		m_carry.clear();
		if (knownCarryable(defNameId) == nullptr) {
			return {};
		}
		const auto keys = m_memory->worldEntitiesOfDef(defNameId);
#ifdef DEVELOPMENT_BUILD
		visits += keys.size();
#endif
		for (uint64_t key : keys) {
			const KnownWorldEntity* entity = m_memory->getWorldEntity(key);
			if (rememberedAs(*entity, assets::CapabilityType::Carryable)) {
				m_carry.push_back({key, entity});
			}
		}
		return m_carry;
	}

	std::span<const uint32_t> WorkSources::knownCarryableDefs() {
		if (!m_carryDefsListed) {
			m_carryDefsListed = true;
			m_carryDefs.clear();
			for (const auto& [defNameId, keys] : m_memory->worldEntitiesByDef) {
				if (!keys.empty() && knownCarryable(defNameId) != nullptr) {
					m_carryDefs.push_back(defNameId);
				}
			}
			// Item ids, not entries: a category goal emits per item in id order, as it always has.
			std::sort(m_carryDefs.begin(), m_carryDefs.end());
		}
		return m_carryDefs;
	}

	const WorkSources::HarvestDef& WorkSources::harvestDef(uint32_t defNameId) {
		static const HarvestDef kNotHarvestable;
		if (!m_registry->hasCapability(defNameId, assets::CapabilityType::Harvestable)) {
			return kNotHarvestable;
		}
		if (defNameId >= m_harvestDefs.size()) {
			m_harvestDefs.resize(defNameId + 1);
		}
		HarvestDef& resolved = m_harvestDefs[defNameId];
		if (resolved.evaluation == m_evaluation) {
			return resolved;
		}
		resolved = HarvestDef{};
		resolved.evaluation = m_evaluation;
		const assets::AssetDefinition* def = m_registry->getDefinitionById(defNameId);
		if (def == nullptr || !def->capabilities.harvestable.has_value()) {
			return resolved;
		}
		const auto&	   harvestable = def->capabilities.harvestable.value();
		const auto&	   yieldName = harvestable.yieldDefName;
		const uint32_t yieldId = m_registry->getDefNameId(yieldName);
		resolved.def = def;
		resolved.yieldDefNameId = yieldId;
		resolved.yieldDef = m_registry->getDefinitionById(yieldId);
		resolved.holdsTool = ecs::inventoryHoldsToolType(*m_inventory, *m_registry, harvestable.requiredToolType);
		resolved.yieldFits = ecs::cargoUnitsThatFit(*m_inventory, *m_registry, yieldName) != 0 && m_inventory->addableCount(yieldName) != 0;
		return resolved;
	}

	void WorkSources::appendHarvestables(uint32_t defNameId, const HarvestDef& resolved) {
		const auto keys = m_memory->worldEntitiesOfDef(defNameId);
#ifdef DEVELOPMENT_BUILD
		visits += keys.size();
#endif
		for (uint64_t key : keys) {
			const KnownWorldEntity* entity = m_memory->getWorldEntity(key);
			if (rememberedAs(*entity, assets::CapabilityType::Harvestable)) {
				m_harvest.push_back({resolved.yieldDefNameId, key, entity, resolved.def, resolved.yieldDef});
			}
		}
	}

	std::span<const HarvestSource> WorkSources::harvestablesYielding(uint32_t yieldDefNameId) {
		m_harvest.clear();
		// The tool and carry gates are per def, so a colonist that can't cut a def visits none of it.
		for (uint32_t defNameId : m_registry->harvestablesYielding(yieldDefNameId)) {
			const HarvestDef& resolved = harvestDef(defNameId);
			if (resolved.def != nullptr && resolved.holdsTool && resolved.yieldFits) {
				appendHarvestables(defNameId, resolved);
			}
		}
		return m_harvest;
	}

	std::span<const HarvestSource> WorkSources::clearableHarvestables() {
		m_harvest.clear();
		for (const auto& [defNameId, keys] : m_memory->worldEntitiesByDef) {
			if (keys.empty()) {
				continue;
			}
			const HarvestDef& resolved = harvestDef(defNameId);
			if (resolved.def != nullptr && resolved.holdsTool) {
				appendHarvestables(defNameId, resolved);
			}
		}
		return m_harvest;
	}

} // namespace ecs
