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

	std::vector<const Thought*> heldThoughts(const Thoughts& thoughts) {
		std::vector<const Thought*> held;
		held.reserve(thoughts.heldIds.size());
		for (const ThoughtId id : thoughts.heldIds) {
			held.push_back(findThought(thoughts, id));
		}
		return held;
	}

	float heldMoodTotal(const Thoughts& thoughts) {
		float total = 0.0F;
		for (const ThoughtId id : thoughts.heldIds) {
			total += findThought(thoughts, id)->heldMoodAmount();
		}
		return total;
	}

} // namespace ecs
