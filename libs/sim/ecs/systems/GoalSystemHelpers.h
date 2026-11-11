#pragma once

// Shared helpers for the goal-generating systems (CraftingGoalSystem, StorageGoalSystem,
// ConstructionSystem). Each was duplicated across two systems; this is the single home so
// "does any colonist know a source" and "does the colony know this box" have one definition.

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Memory.h"
#include "../components/StorageConfiguration.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>

#include <cstdint>
#include <vector>

namespace ecs {

	/// The storage boxes any colonist remembers. A storage-to-storage pull may only source from a box
	/// the colony actually knows about (no god-view), mirroring how a loose pickup is gated on the pile
	/// being in memory. A box is recorded in memory by its Appearance defName + Position (the same key
	/// VisionSystem's Pass 2 uses when it sees the box). A colony-placed/-built box is normally already
	/// in memory.
	///
	/// A memo for one pass (an AI update, a UI model refresh): a box's answer is worked out the first
	/// time it is asked about after invalidate(), one probe of each colonist's memory, and later
	/// questions about it read the memo. Boxes nobody asks about cost nothing. Indexed by entity index
	/// and stamped with the pass, so a warm memo allocates nothing.
	class ColonyStorageKnowledge {
	  public:
		void invalidate() { ++m_pass; }

		[[nodiscard]] bool knows(World& world, const assets::AssetRegistry& reg, EntityID box) {
			const uint32_t index = getIndex(box);
			if (index >= m_answers.size()) {
				m_answers.resize(index + 1);
			}
			Answer& answer = m_answers[index];
			if (answer.pass != m_pass || answer.box != box) {
				answer = {m_pass, box, workOut(world, reg, box)};
			}
			return answer.known;
		}

#ifdef DEVELOPMENT_BUILD
		/// Boxes worked out since construction; tests read it to show each asked box is worked out
		/// once per pass, and unasked boxes not at all.
		uint64_t workedOut = 0;
#endif

	  private:
		struct Answer {
			uint64_t pass = 0;
			EntityID box = kInvalidEntity;
			bool	 known = false;
		};

		[[nodiscard]] bool workOut(World& world, const assets::AssetRegistry& reg, EntityID box) {
#ifdef DEVELOPMENT_BUILD
			++workedOut;
#endif
			const auto* appearance = world.getComponent<Appearance>(box);
			const auto* boxPos = world.getComponent<Position>(box);
			if (appearance == nullptr || boxPos == nullptr || world.getComponent<StorageConfiguration>(box) == nullptr) {
				return false;
			}
			const uint32_t defNameId = reg.getDefNameId(appearance->defName);
			if (defNameId == 0) {
				return false;
			}
			for (auto [colonist, memory] : world.view<Memory>()) {
				(void)colonist;
				if (memory.knowsWorldEntity(boxPos->value, defNameId)) {
					return true;
				}
			}
			return false;
		}

		uint64_t			m_pass = 1;
		std::vector<Answer> m_answers;
	};

	/// Does any colonist remember a harvestable whose yield is this item (e.g. a tree for Wood)?
	/// Colony "availability" = the union of colonist memories (no god-view). A goal created from
	/// this is still only fulfilled by a colonist that actually remembers the source.
	[[nodiscard]] inline bool colonyKnowsHarvestableSource(World* world, const assets::AssetRegistry& reg, uint32_t yieldDefNameId) {
		const auto harvestable = static_cast<uint16_t>(1U << static_cast<size_t>(assets::CapabilityType::Harvestable));
		for (auto [colonist, memory] : world->view<Memory>()) {
			(void)colonist;
			for (uint32_t source : reg.harvestablesYielding(yieldDefNameId)) {
				for (uint64_t key : memory.worldEntitiesOfDef(source)) {
					if ((memory.getWorldEntity(key)->capabilityMask & harvestable) != 0) {
						return true;
					}
				}
			}
		}
		return false;
	}

} // namespace ecs
