#pragma once

// The remembered sources one colonist's work evaluators draw on, read from Memory's per-def key lists
// so a goal for an item visits only that item's entries, and resolved once per def per evaluation.
// Each source query fills one reused buffer: its span is valid until the next query.

#include "../components/Memory.h"

#include <assets/AssetDefinition.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace assets {
	class AssetRegistry;
}

namespace ecs {

	struct Inventory;

	/// A remembered loose item the registry still calls Carryable.
	struct CarrySource {
		uint64_t				key = 0;
		const KnownWorldEntity* entity = nullptr;
	};

	/// A remembered harvestable this colonist can cut now.
	struct HarvestSource {
		uint32_t					   yieldDefNameId = 0;
		uint64_t					   key = 0;
		const KnownWorldEntity*		   entity = nullptr;
		const assets::AssetDefinition* def = nullptr;
		const assets::AssetDefinition* yieldDef = nullptr;
	};

	class WorkSources {
	  public:
		/// Start one colonist's evaluation. Storage is kept across colonists, so a decision allocates
		/// nothing once it has grown.
		void begin(const Memory& memory, const assets::AssetRegistry& registry, const Inventory& inventory);

		/// The item's definition when the registry calls it Carryable and the colonist remembers at least
		/// one carryable entry of it; nullptr otherwise. Looks at no more than the first such entry.
		[[nodiscard]] const assets::AssetDefinition* knownCarryable(uint32_t defNameId) const;

		/// The carryable sources of one item.
		[[nodiscard]] std::span<const CarrySource> carryablesOf(uint32_t defNameId);

		/// Every item knownCarryable() answers for, ascending by id.
		[[nodiscard]] std::span<const uint32_t> knownCarryableDefs();

		/// The harvestables that yield one item and that this colonist can cut now: it holds the tool
		/// the cut needs (colonists never fetch tools) and at least one unit of the yield fits its pack
		/// (a cut that collects 0 would be chosen again every decision).
		[[nodiscard]] std::span<const HarvestSource> harvestablesYielding(uint32_t yieldDefNameId);

		/// Every harvestable this colonist holds the tool for, whatever fits: a clear removes its
		/// blocker and drops what the pack can't take, so a full pack never stalls a site.
		[[nodiscard]] std::span<const HarvestSource> clearableHarvestables();

#ifdef DEVELOPMENT_BUILD
		/// Remembered entries placed in a source span since construction. Tests read it to show the
		/// cost follows the matching sources, not the size of the memory.
		uint64_t visits = 0;
#endif

	  private:
		/// The per-def answers every harvest asks, worked out once per def per evaluation. Indexed by
		/// def id (registry ids are dense) and stamped with the evaluation that filled it.
		struct HarvestDef {
			uint64_t					   evaluation = 0;
			const assets::AssetDefinition* def = nullptr;
			const assets::AssetDefinition* yieldDef = nullptr;
			uint32_t					   yieldDefNameId = 0;
			bool						   holdsTool = false;
			bool						   yieldFits = false;
		};
		const HarvestDef& harvestDef(uint32_t defNameId);
		void			  appendHarvestables(uint32_t defNameId, const HarvestDef& resolved);

		const Memory*				 m_memory = nullptr;
		const assets::AssetRegistry* m_registry = nullptr;
		const Inventory*			 m_inventory = nullptr;
		uint64_t					 m_evaluation = 0;
		bool						 m_carryDefsListed = false;
		/// What the colonist carries, summed the first time a harvest def asks in an evaluation
		std::optional<float>		 m_carriedKg;
		std::vector<uint32_t>		 m_carryDefs;
		std::vector<HarvestDef>		 m_harvestDefs;
		std::vector<CarrySource>	 m_carry;
		std::vector<HarvestSource>	 m_harvest;
	};

} // namespace ecs
