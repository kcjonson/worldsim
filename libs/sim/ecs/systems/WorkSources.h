#pragma once

// The remembered sources one colonist's work evaluators draw on, collected once per evaluation from
// Memory's capability index and resolved once per def. Evaluators then visit only the entries for
// the item a goal wants, so their cost follows the matching sources, not the size of the memory.

#include "../components/Memory.h"

#include <assets/AssetDefinition.h>

#include <cstdint>
#include <span>
#include <vector>

namespace assets {
	class AssetRegistry;
}

namespace ecs {

	struct Inventory;

	/// A remembered loose item the registry still calls Carryable.
	struct CarrySource {
		uint32_t					   defNameId = 0;
		uint64_t					   key = 0;
		const KnownWorldEntity*		   entity = nullptr;
		const assets::AssetDefinition* def = nullptr;
	};

	/// A remembered harvestable, with the per-def answers every Harvest goal asks.
	struct HarvestSource {
		uint32_t					   yieldDefNameId = 0;
		uint64_t					   key = 0;
		const KnownWorldEntity*		   entity = nullptr;
		const assets::AssetDefinition* def = nullptr;
		const assets::AssetDefinition* yieldDef = nullptr;
		bool						   holdsTool = false; // the colonist holds the tool the cut needs
		bool						   yieldFits = false; // at least one unit of the yield fits the pack
	};

	class WorkSources {
	  public:
		/// Start one colonist's evaluation. Nothing is collected until an evaluator asks, and storage
		/// is kept across colonists, so a decision allocates nothing once it has grown.
		void begin(const Memory& memory, const assets::AssetRegistry& registry, const Inventory& inventory);

		/// Every carryable source, ordered by (defNameId, key).
		[[nodiscard]] std::span<const CarrySource> carryables();

		/// The carryable sources of one item.
		[[nodiscard]] std::span<const CarrySource> carryablesOf(uint32_t defNameId);

		/// Every harvestable source, ordered by (yieldDefNameId, key).
		[[nodiscard]] std::span<const HarvestSource> harvestables();

		/// The harvestable sources that yield one item.
		[[nodiscard]] std::span<const HarvestSource> harvestablesYielding(uint32_t yieldDefNameId);

#ifdef DEVELOPMENT_BUILD
		/// Entries looked at since construction: one per capability-index entry collected, one per
		/// source visited for a goal. Tests read it to show the cost follows the matching sources.
		uint64_t visits = 0;
		void	 countVisits(uint64_t n) { visits += n; }
#else
		void countVisits(uint64_t /*n*/) {}
#endif

	  private:
		void collectCarryables();
		void collectHarvestables();

		const Memory*				 m_memory = nullptr;
		const assets::AssetRegistry* m_registry = nullptr;
		const Inventory*			 m_inventory = nullptr;
		bool						 m_carryCollected = false;
		bool						 m_harvestCollected = false;
		std::vector<CarrySource>	 m_carry;
		std::vector<HarvestSource>	 m_harvest;
	};

} // namespace ecs
