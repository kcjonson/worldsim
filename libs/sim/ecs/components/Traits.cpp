#include "Traits.h"

#include <utils/Log.h>

#include <algorithm>

namespace ecs {

	Traits rollTraits(const assets::TraitRegistry& registry, std::mt19937& rng) {
		const assets::TraitCount range = registry.count();
		const int				 wanted = std::uniform_int_distribution<int>(range.min, range.max)(rng);

		std::vector<const assets::TraitDef*> candidates;
		for (const assets::TraitDef& trait : registry.traits()) {
			candidates.push_back(&trait);
		}

		Traits traits;
		while (static_cast<int>(traits.ids.size()) < wanted && !candidates.empty()) {
			const std::size_t drawn = std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(rng);
			const std::string chosen = candidates[drawn]->id;
			traits.ids.push_back(chosen);
			std::erase_if(candidates, [&](const assets::TraitDef* candidate) {
				return candidate->id == chosen || registry.exclusive(candidate->id, chosen);
			});
		}
		if (static_cast<int>(traits.ids.size()) < wanted) {
			LOG_WARNING(Game, "Rolled %zu traits where %d were wanted: the exclusions left too few to draw", traits.ids.size(), wanted);
		}
		return traits;
	}

} // namespace ecs
