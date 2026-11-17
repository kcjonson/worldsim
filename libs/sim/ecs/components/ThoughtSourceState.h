#pragma once

// What the thought sources remember about one colonist so they fire on a change and never while a
// condition simply continues. ThoughtSourceSystem is the only writer.
// See /docs/design/game-systems/colonists/thoughts.md (What creates thoughts).

#include "Needs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

namespace ecs {

	struct ThoughtSourceState {
		/// Each need's level when last looked at (Need::level), by NeedType; none before the first look.
		std::optional<std::array<std::size_t, static_cast<std::size_t>(NeedType::Count)>> needLevels;
		/// When each idle source next rolls, on TimeSystem::simulationSeconds(), in the order of the idle
		/// sources among ThoughtTypes::sources().
		std::vector<double> idleRollsAt;
		/// The idle rolls made so far: the position in this colonist's own stream of rolls.
		uint64_t idleRolls = 0;
		/// The first-only sources that have fired, each with the kind of thing it was about:
		/// (source index << 32) | definition.
		std::unordered_set<uint64_t> firstsFired;
	};

} // namespace ecs
