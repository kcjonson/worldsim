#pragma once

// ThoughtSystem: the one interface every source creates and ends colonists' thoughts through, and the
// clock that ends timed mood effects. See /docs/design/game-systems/colonists/thoughts.md.
//
// A thought with a mood effect is held while the effect lasts. A new thought with a mood effect whose
// event type and subject match one the colonist already holds, and whose timer has not run out, is
// folded into it rather than held twice. The held thought takes the repeat's feeling, effect, and
// importance, a timed effect restarts its duration from the repeat, and a stacking one counts up. A
// thought without a mood effect is momentary and is always its own record.

#include "../ISystem.h"
#include "../components/Thoughts.h"

#include <deque>
#include <functional>
#include <vector>

namespace ecs {

	/// Names one thought on one colonist, for the creator of a cause-bound effect to end it later.
	struct ThoughtHandle {
		EntityID  colonist = kInvalidEntity;
		ThoughtId thought = 0;
	};

	enum class ThoughtArrival : uint8_t {
		New,
		Repeat
	};

	/// The thought as it stands after the arrival; a repeat reports the held thought it folded into.
	using ThoughtArrivedObserver = std::function<void(EntityID colonist, const Thought& thought, ThoughtArrival arrival)>;
	using ThoughtEndedObserver = std::function<void(EntityID colonist, const Thought& thought)>;

	struct CreateThoughtArgs {
		EntityID				  colonist = kInvalidEntity;
		ThoughtEventTypeId		  eventType = 0;
		ThoughtSubject			  subject{};
		FeelingId				  feeling = 0;
		std::optional<MoodEffect> moodEffect{};
		ThoughtImportance		  importance = ThoughtImportance::Normal;
		bool					  stacks = false;
	};

	/// Priority 11: right after the clock, so every later system in the tick sees timed effects that
	/// ran out already ended.
	class ThoughtSystem : public ISystem {
	  public:
		void update(float deltaTime) override;

		[[nodiscard]] int		  priority() const override { return 11; }
		[[nodiscard]] const char* name() const override { return "Thought"; }

		/// Records a thought on the colonist at the current game time, adding its Thoughts if it has none.
		/// A colonist that is not alive gets nothing and an invalid handle.
		ThoughtHandle create(const CreateThoughtArgs& args);

		/// Ends a held thought; it stays in the record. False when it is not held or the colonist is gone.
		bool end(ThoughtHandle handle);
		/// Ends the held thought of this event type about this subject. False when none is held or the
		/// colonist is gone.
		bool endByCause(EntityID colonist, ThoughtEventTypeId eventType, const ThoughtSubject& subject);

		void addArrivedObserver(ThoughtArrivedObserver observer);
		void addEndedObserver(ThoughtEndedObserver observer);

	  private:
		struct ExpiredThought {
			double	  expiresAt = 0.0;
			EntityID  colonist = kInvalidEntity;
			ThoughtId thought = 0;
		};

		void endHeld(EntityID colonist, Thoughts& thoughts, Thought& thought, double endedAt);

		// An observer may add observers while it is being notified; they first hear the next event. A
		// deque keeps the running observer in place while others are added.
		std::deque<ThoughtArrivedObserver> arrivedObservers;
		std::deque<ThoughtEndedObserver>   endedObservers;
		std::vector<ExpiredThought>		   expiredScratch;
	};

} // namespace ecs
