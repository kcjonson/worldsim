#pragma once

// Traits Component - the trait ids a colonist was given at creation, defined in
// assets/config/colonists/traits. Effects come through conditions (trait(id)), not from this component.
//
// See /docs/design/game-systems/colonists/attributes.md (Traits).

#include <assets/TraitRegistry.h>

#include <random>
#include <string>
#include <vector>

namespace ecs {

	struct Traits {
		std::vector<std::string> ids;
	};

	/// Roll a colonist's traits: a count within the registry's range, each trait drawn from `rng` among
	/// those no trait already chosen excludes. Fewer come back when the exclusions leave too few to draw.
	[[nodiscard]] Traits rollTraits(const assets::TraitRegistry& registry, std::mt19937& rng);

} // namespace ecs
