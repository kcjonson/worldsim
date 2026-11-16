#include "WorkSources.h"

#include "../InventoryMass.h"
#include "../components/Inventory.h"

#include <assets/AssetRegistry.h>

#include <glm/geometric.hpp>

#include <algorithm>

namespace ecs {

	namespace {
		[[nodiscard]] bool rememberedAs(const KnownWorldEntity& entity, assets::CapabilityType capability) {
			return (entity.capabilityMask & (1U << static_cast<uint32_t>(capability))) != 0;
		}
	} // namespace

	void WorkSources::begin(const Memory& memory, const assets::AssetRegistry& registry, const Inventory& inventory, glm::vec2 position) {
		m_memory = &memory;
		m_registry = &registry;
		m_inventory = &inventory;
		m_position = position;
		++m_evaluation;
		m_carryDefsListed = false;
		m_carriedKg.reset();
		m_carry.clear();
		m_harvest.clear();
	}

	template <typename Source, typename Fill>
	std::span<Source> WorkSources::sourcesOf(std::vector<ItemSpan>& spans, std::vector<Source>& storage, uint32_t item, Fill&& fill) {
		if (item >= spans.size()) {
			spans.resize(item + 1);
		}
		ItemSpan& span = spans[item];
		if (span.evaluation != m_evaluation) {
			span.evaluation = m_evaluation;
			span.begin = static_cast<uint32_t>(storage.size());
			fill();
			span.end = static_cast<uint32_t>(storage.size());
		}
		return {storage.data() + span.begin, span.end - span.begin};
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

	std::span<CarrySource> WorkSources::carryablesOf(uint32_t defNameId) {
		return sourcesOf(m_carrySpans, m_carry, defNameId, [&] {
			if (knownCarryable(defNameId) == nullptr) {
				return;
			}
			const auto keys = m_memory->worldEntitiesOfDef(defNameId);
#ifdef DEVELOPMENT_BUILD
			visits += keys.size();
#endif
			for (uint64_t key : keys) {
				const KnownWorldEntity* entity = m_memory->getWorldEntity(key);
				if (rememberedAs(*entity, assets::CapabilityType::Carryable)) {
					CarrySource& source = m_carry.emplace_back();
					source.key = key;
					source.entity = entity;
					source.distance = glm::distance(m_position, entity->position);
				}
			}
		});
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
		const uint32_t yieldId = m_registry->harvestYieldId(defNameId);
		resolved.def = def;
		resolved.yieldDefNameId = yieldId;
		resolved.yieldDef = m_registry->getDefinitionById(yieldId);
		resolved.holdsTool = ecs::inventoryHoldsToolType(*m_inventory, *m_registry, harvestable.requiredToolType);
		// The rule the pickup and the cut's landing use: a two-hand yield goes to free hands whatever the pack holds.
		if (!m_carriedKg.has_value()) {
			m_carriedKg = ecs::carriedCargoMassKg(*m_inventory, *m_registry);
		}
		resolved.yieldFits = ecs::pickupUnitsThatFit(*m_inventory, *m_registry, yieldId, *m_carriedKg) != 0;
		return resolved;
	}

	void WorkSources::appendHarvestables(uint32_t defNameId, const HarvestDef& resolved, const FootprintBounds* inside) {
		const auto keys = m_memory->worldEntitiesOfDef(defNameId);
#ifdef DEVELOPMENT_BUILD
		visits += keys.size();
#endif
		for (uint64_t key : keys) {
			const KnownWorldEntity* entity = m_memory->getWorldEntity(key);
			if (rememberedAs(*entity, assets::CapabilityType::Harvestable) &&
				(inside == nullptr || inside->blockedBy(*m_registry, entity->defNameId, entity->position))) {
				HarvestSource& source = m_harvest.emplace_back();
				source.yieldDefNameId = resolved.yieldDefNameId;
				source.key = key;
				source.entity = entity;
				source.def = resolved.def;
				source.yieldDef = resolved.yieldDef;
				source.distance = glm::distance(m_position, entity->position);
			}
		}
	}

	std::span<HarvestSource> WorkSources::harvestablesYielding(uint32_t yieldDefNameId) {
		return sourcesOf(m_harvestSpans, m_harvest, yieldDefNameId, [&] {
			// The tool and carry gates are per def, so a colonist that can't cut a def visits none of it.
			for (uint32_t defNameId : m_registry->harvestablesYielding(yieldDefNameId)) {
				const HarvestDef& resolved = harvestDef(defNameId);
				if (resolved.def != nullptr && resolved.holdsTool && resolved.yieldFits) {
					appendHarvestables(defNameId, resolved);
				}
			}
		});
	}

	std::span<HarvestSource> WorkSources::clearableHarvestables(const FootprintBounds& footprint) {
		const auto begin = static_cast<uint32_t>(m_harvest.size());
		for (const auto& [defNameId, keys] : m_memory->worldEntitiesByDef) {
			if (keys.empty()) {
				continue;
			}
			const HarvestDef& resolved = harvestDef(defNameId);
			if (resolved.def != nullptr && resolved.holdsTool) {
				appendHarvestables(defNameId, resolved, &footprint);
			}
		}
		return {m_harvest.data() + begin, m_harvest.size() - begin};
	}

} // namespace ecs
