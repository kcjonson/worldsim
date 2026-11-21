#pragma once

// ThoughtSourceSystem: turns what happens in the sim into thoughts, by the <source> elements of the
// thought types. Every trigger is a change: a need crossing a threshold, an action starting or
// finishing, a first sight, a recipe learned, or an idle roll. A need that simply stays low creates
// nothing new. See /docs/design/game-systems/colonists/thoughts.md (What creates thoughts).

#include "../ISystem.h"
#include "../ThoughtTypes.h"
#include "ActionSystem.h"
#include "VisionSystem.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ecs {

	struct ThoughtSourceState;

	/// Priority 360: after ActionSystem, so the needs an action changed and the actions and discoveries
	/// heard this tick become thoughts in the same tick.
	class ThoughtSourceSystem : public ISystem {
	  public:
		/// `types` must outlive the system. Each colonist's idle rolls are a stream of their own, derived
		/// from `randomSeed` and the colonist's entity.
		ThoughtSourceSystem(const ThoughtTypes& types, uint64_t randomSeed);

		void update(float deltaTime) override;

		[[nodiscard]] int		  priority() const override { return 360; }
		[[nodiscard]] const char* name() const override { return "ThoughtSource"; }

		/// Hear the actions and discoveries these systems report. Call once, after registering them.
		void listenTo(ActionSystem& actions, VisionSystem& vision);

	  private:
		/// An action or a discovery, held until this system's update.
		struct HeardEvent {
			EntityID	   colonist = kInvalidEntity;
			ThoughtTrigger							 trigger = ThoughtTrigger::Idle;
			assets::EntryHandle<assets::ActionEntry> action;
			uint32_t								 definition = 0;
		};

		ThoughtSourceState& stateOf(EntityID colonist, double now);
		void				hear(const HeardEvent& event, double now);
		void				watchNeeds(EntityID colonist, ThoughtSourceState& state);
		void				rollIdle(EntityID colonist, ThoughtSourceState& state, double now);
		[[nodiscard]] bool	thingMatches(const ThoughtSource& source, uint32_t definition) const;
		void				think(EntityID colonist, ThoughtEventTypeId thought, uint32_t definition);

		const ThoughtTypes& thoughtTypes;
		uint64_t			seed;
		/// The idle sources, as indexes into ThoughtTypes::sources().
		std::vector<std::size_t> idleSources;

		std::vector<HeardEvent> heardEvents;
		std::vector<HeardEvent> heardScratch;
		std::vector<EntityID>	colonistScratch;
	};

} // namespace ecs
