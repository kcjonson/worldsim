#pragma once

// ThoughtSystem: the one interface every source creates and ends colonists' thoughts through, and the
// clock that ends mood effects. See /docs/design/game-systems/colonists/thoughts.md.
//
// A source names the colonist, the event type, and the subject; the type's definition supplies the
// rest. The colonist's feeling is the most specific of the type's feelings whose `when` holds, and it
// sets the mood effect; the line is picked the same way among the lines that fit that feeling.
//
// A thought with a mood effect is held while the effect lasts. A new thought with a mood effect whose
// event type and subject match one the colonist already holds, and whose timer has not run out, is
// folded into it rather than held twice. The held thought takes the repeat's feeling, line, and effect,
// a timed effect restarts its duration from the repeat, and a stacking one counts up. A thought without
// a mood effect is momentary and is always its own record; as a repeat of a held thought it ends that
// one first. A `while` effect ends at its next check once its condition is False or its subject is gone.
// When a held thought ends, however it ends, its type's <ends> thought is created.

#include "../ISystem.h"
#include "../ThoughtTypes.h"
#include "../components/Thoughts.h"

#include <conditions/TermSignature.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <random>
#include <vector>

namespace ecs {

	/// Names one thought on one colonist, for the creator of a cause-bound effect to end it later.
	struct ThoughtHandle {
		EntityID  colonist = kInvalidEntity;
		ThoughtId thought = 0;
	};

	enum class ThoughtArrival : uint8_t { New, Repeat };

	/// The thought as it stands after the arrival; a repeat reports the held thought it folded into.
	using ThoughtArrivedObserver = std::function<void(EntityID colonist, const Thought& thought, ThoughtArrival arrival)>;
	using ThoughtEndedObserver = std::function<void(EntityID colonist, const Thought& thought)>;

	struct CreateThoughtArgs {
		EntityID		   colonist = kInvalidEntity;
		ThoughtEventTypeId eventType = 0;
		ThoughtSubject	   subject{};
	};

	/// Priority 11: right after the clock, so every later system in the tick sees effects that ran out
	/// already ended.
	class ThoughtSystem : public ISystem {
	  public:
		/// `types` must outlive the system. `randomSeed` drives the pick among equally fitting lines.
		ThoughtSystem(const ThoughtTypes& types, uint32_t randomSeed);

		void update(float deltaTime) override;

		[[nodiscard]] int		  priority() const override { return 11; }
		[[nodiscard]] const char* name() const override { return "Thought"; }

		[[nodiscard]] const ThoughtTypes& types() const { return thoughtTypes; }

		/// Records a thought on the colonist at the current game time, adding its Thoughts if it has none.
		/// Nothing is recorded, and the handle is invalid, when the colonist is not alive, the event type is
		/// unknown, or none of the type's feelings applies to this colonist.
		ThoughtHandle create(const CreateThoughtArgs& args);

		/// Ends a held thought; it stays in the record. False when it is not held or the colonist is gone.
		bool end(ThoughtHandle handle);
		/// Ends the held thought of this event type about this subject. False when none is held or the
		/// colonist is gone.
		bool endByCause(EntityID colonist, ThoughtEventTypeId eventType, const ThoughtSubject& subject);

		void addArrivedObserver(ThoughtArrivedObserver observer);
		void addEndedObserver(ThoughtEndedObserver observer);

	  private:
		struct DueThought {
			double	  dueAt = 0.0;
			EntityID  colonist = kInvalidEntity;
			ThoughtId thought = 0;
		};

		[[nodiscard]] std::optional<uint16_t> chooseFeeling(const ThoughtType& type, const conditions::ConditionContext& context) const;
		[[nodiscard]] std::optional<uint16_t> chooseLine(
			const ThoughtType&					type,
			FeelingId							feeling,
			const conditions::ConditionContext& context,
			bool								subjectHasName,
			std::optional<uint16_t>				lastLine
		);
		/// True when the held thought's `while` condition was checked and is False, or its subject is gone.
		[[nodiscard]] bool causeHasPassed(EntityID colonist, const Thought& thought) const;
		void			   endHeld(EntityID colonist, Thoughts& thoughts, Thought& thought, double endedAt);

		const ThoughtTypes& thoughtTypes;
		std::mt19937		random;

		// An observer may add observers while it is being notified; they first hear the next event. A
		// deque keeps the running observer in place while others are added.
		std::deque<ThoughtArrivedObserver> arrivedObservers;
		std::deque<ThoughtEndedObserver>   endedObservers;
		std::vector<DueThought>			   dueScratch;
		std::vector<uint16_t>			   lineScratch;
	};

} // namespace ecs
