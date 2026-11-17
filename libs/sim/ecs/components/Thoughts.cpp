#include "Thoughts.h"

#include <algorithm>
#include <utility>

namespace ecs {

	const Thought* findThought(const Thoughts& thoughts, ThoughtId id) {
		const auto found =
			std::lower_bound(thoughts.record.begin(), thoughts.record.end(), id, [](const Thought& thought, ThoughtId wanted) {
				return thought.id < wanted;
			});
		return found != thoughts.record.end() && found->id == id ? &*found : nullptr;
	}

	Thought* findThought(Thoughts& thoughts, ThoughtId id) {
		return const_cast<Thought*>(findThought(std::as_const(thoughts), id));
	}

	bool isHeld(const Thought& thought) {
		return thought.moodEffect.has_value() && !thought.endedAt.has_value();
	}

	float heldMoodAmount(const Thought& thought) {
		return isHeld(thought) ? thought.moodEffect->amount * static_cast<float>(thought.count) : 0.0F;
	}

	float heldMoodTotal(const Thoughts& thoughts) {
		float total = 0.0F;
		forEachHeldThought(thoughts, [&total](const Thought& thought) { total += heldMoodAmount(thought); });
		return total;
	}

} // namespace ecs
