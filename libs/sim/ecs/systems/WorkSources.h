#pragma once

// The remembered sources one colonist's work evaluators draw on, read from Memory's per-def key lists
// so a goal for an item visits only that item's entries, and resolved once per def per evaluation.
// Each item's sources are filled once per evaluation and every goal that asks for the item reads the
// same entries, so an answer one goal works out about a source (Lazy) serves every other.

#include "../EntityID.h"
#include "../GoalTaskRegistry.h"
#include "../components/Memory.h"

#include <assets/AssetDefinition.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace assets {
	class AssetRegistry;
}

namespace ecs {

	struct Inventory;

	/// An answer about a source worked out the first time anything asks in an evaluation, and kept for
	/// the rest of it.
	template <typename T> class Lazy {
	  public:
		template <typename Compute> const T& get(Compute&& compute) {
			if (!value.has_value()) {
				value.emplace(compute());
			}
			return *value;
		}

	  private:
		std::optional<T> value;
	};

	/// What a remembered pile holds for the deciding colonist: the units other colonists' claims take
	/// off it, and its live count, nullopt when the world can't find it (taken since it was seen, or a
	/// world with no pile index).
	struct PileStock {
		uint32_t				claimed = 0;
		std::optional<uint32_t> live;

		/// Units it still offers a fetch wanting `wanted`. A pile with no count offers the wanted amount
		/// while nobody else claims it, so one colonist goes to look and the stale memory is corrected on
		/// arrival.
		[[nodiscard]] uint32_t left(uint32_t wanted) const {
			if (!live.has_value()) {
				return claimed > 0 ? 0U : wanted;
			}
			return *live > claimed ? *live - claimed : 0U;
		}
	};

	/// A remembered loose item the registry still calls Carryable.
	struct CarrySource {
		uint64_t				key = 0;
		const KnownWorldEntity* entity = nullptr;
		float					distance = 0.0F; // from the deciding colonist
		Lazy<PileStock>			stock;
		/// Connected to him by meshed ground, to where a route to it ends; verdicts are asked apart.
		Lazy<bool> connected;
	};

	/// A remembered harvestable this colonist can cut now.
	struct HarvestSource {
		uint32_t					   yieldDefNameId = 0;
		uint64_t					   key = 0;
		const KnownWorldEntity*		   entity = nullptr;
		const assets::AssetDefinition* def = nullptr;
		const assets::AssetDefinition* yieldDef = nullptr;
		float						   distance = 0.0F; // from the deciding colonist
		/// The other colonist whose Use claim holds it (ClaimIndex::useHolder), kInvalidEntity for none.
		Lazy<EntityID> holder;
		/// Regrowing from an earlier cut, with nothing to give now.
		Lazy<bool> regrowing;
		/// Where he stands to cut it; nullopt: nowhere.
		Lazy<std::optional<glm::vec2>> standPoint;
		/// He may be sent to its stand point: no verdict, and connected to it.
		Lazy<bool> reachable;
	};

	class WorkSources {
	  public:
		/// Start one colonist's evaluation, the colonist standing at `position`. Storage is kept across
		/// colonists, so a decision allocates nothing once it has grown.
		void begin(const Memory& memory, const assets::AssetRegistry& registry, const Inventory& inventory, glm::vec2 position);

		/// The item's definition when the registry calls it Carryable and the colonist remembers at least
		/// one carryable entry of it; nullptr otherwise. Looks at no more than the first such entry.
		[[nodiscard]] const assets::AssetDefinition* knownCarryable(uint32_t defNameId) const;

		/// The carryable sources of one item, filled on the first ask in an evaluation. Valid until the
		/// next query fills another item's.
		[[nodiscard]] std::span<CarrySource> carryablesOf(uint32_t defNameId);

		/// Every item knownCarryable() answers for, ascending by id.
		[[nodiscard]] std::span<const uint32_t> knownCarryableDefs();

		/// The harvestables that yield one item and that this colonist can cut now: it holds the tool
		/// the cut needs (colonists never fetch tools) and at least one unit of the yield fits its pack
		/// (a cut that collects 0 would be chosen again every decision). Filled on the first ask in an
		/// evaluation; valid until the next query fills another.
		[[nodiscard]] std::span<HarvestSource> harvestablesYielding(uint32_t yieldDefNameId);

		/// Every harvestable inside `footprint` that this colonist holds the tool for, whatever fits: a clear
		/// removes its blocker and drops what the pack can't take, so a full pack never stalls a site.
		/// Filled on every ask; valid until the next query fills another.
		[[nodiscard]] std::span<HarvestSource> clearableHarvestables(const FootprintBounds& footprint);

		/// A source by its index in this evaluation's storage of its kind: an index stays valid while later
		/// queries fill more, a span does not.
		[[nodiscard]] HarvestSource& harvest(uint32_t index) { return m_harvest[index]; }
		[[nodiscard]] CarrySource&	 carry(uint32_t index) { return m_carry[index]; }
		[[nodiscard]] uint32_t		 indexOf(const HarvestSource& source) const {
			  return static_cast<uint32_t>(&source - m_harvest.data());
		}
		[[nodiscard]] uint32_t indexOf(const CarrySource& source) const { return static_cast<uint32_t>(&source - m_carry.data()); }

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
		void appendHarvestables(uint32_t defNameId, const HarvestDef& resolved, const FootprintBounds* inside = nullptr);

		/// Where one item's sources lie in this evaluation's storage, indexed by item id and stamped with
		/// the evaluation that filled it.
		struct ItemSpan {
			uint64_t evaluation = 0;
			uint32_t begin = 0;
			uint32_t end = 0;
		};
		/// The item's span, filled by `fill` when this evaluation has not filled it yet.
		template <typename Source, typename Fill>
		std::span<Source> sourcesOf(std::vector<ItemSpan>& spans, std::vector<Source>& storage, uint32_t item, Fill&& fill);

		const Memory*				 m_memory = nullptr;
		const assets::AssetRegistry* m_registry = nullptr;
		const Inventory*			 m_inventory = nullptr;
		glm::vec2					 m_position{0.0F, 0.0F};
		uint64_t					 m_evaluation = 0;
		bool						 m_carryDefsListed = false;
		/// What the colonist carries, summed the first time a harvest def asks in an evaluation
		std::optional<float>		 m_carriedKg;
		std::vector<uint32_t>		 m_carryDefs;
		std::vector<HarvestDef>		 m_harvestDefs;
		std::vector<ItemSpan>		 m_carrySpans;
		std::vector<ItemSpan>		 m_harvestSpans;
		std::vector<CarrySource>	 m_carry;
		std::vector<HarvestSource>	 m_harvest;
	};

} // namespace ecs
