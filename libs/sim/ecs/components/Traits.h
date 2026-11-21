#pragma once

// Traits Component - the trait ids a colonist was given at creation, defined in
// assets/world/creatures/Human/traits.xml. Effects come through conditions (trait(id)), not from this component.
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

	/// Roll a colonist's traits from `rng`: a count within the registry's range, lowered to the most the
	/// exclusive groups let one colonist hold, then each trait drawn among those that still leave that count
	/// reachable. Never fewer than the range's min, which the load proved reachable.
	[[nodiscard]] Traits rollTraits(const assets::TraitRegistry& registry, std::mt19937& rng);

} // namespace ecs
