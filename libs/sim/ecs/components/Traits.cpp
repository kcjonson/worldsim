#include "Traits.h"

#include <numeric>

namespace ecs {

	Traits rollTraits(const assets::TraitRegistry& registry, std::mt19937& rng) {
		const assets::TraitCount range = registry.count();
		auto					 wanted = static_cast<std::size_t>(std::uniform_int_distribution<int>(range.min, range.max)(rng));
		while (!registry.reachable({}, wanted)) {
			--wanted;
		}

		std::vector<std::size_t> candidates(registry.traits().size());
		std::iota(candidates.begin(), candidates.end(), std::size_t{0});
		std::vector<std::size_t> held;
		while (held.size() < wanted) {
			const std::size_t drawn = std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(rng);
			held.push_back(candidates[drawn]);
			candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(drawn));
			if (!registry.reachable(held, wanted)) {
				held.pop_back();
			}
		}

		Traits traits;
		for (const std::size_t trait : held) {
			traits.ids.push_back(registry.traits()[trait].id);
		}
		return traits;
	}

} // namespace ecs
