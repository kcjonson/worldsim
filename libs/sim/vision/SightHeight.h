#pragma once

// Sight by height (sight-softening.md 2.2): a placed body whose top is lower than a colonist's eye is
// no sight body. It never enters the placed occluder index, so colonists see over it; nav and
// collision still hold it.

#include <cstdint>
#include <optional>

namespace assets {
	struct AssetDefinition;
}

namespace vision {

	// A colonist's eye height. A constant, not a sight tunable: it decides whether a ring enters the
	// placed occluder index at all, once, as the ring is added, and a slider could not re-decide that.
	constexpr std::int64_t kEyeHeightMm = 1600;

	// The height of the top of a placed body of `def`, from the def alone: a simple def's worldHeight
	// (the parser's 1.0 m when unwritten), a parts item's tallest part top (0 for flat parts only). None when the def states no height: a procedural def's
	// generator draws to its own size.
	[[nodiscard]] std::optional<std::int64_t> bodyTopMm(const assets::AssetDefinition& def);

	// Whether a placed body of `def` reaches a colonist's eye: its top at least kEyeHeightMm, or no
	// height stated, which counts as tall.
	[[nodiscard]] bool reachesEyeHeight(const assets::AssetDefinition& def);

} // namespace vision
