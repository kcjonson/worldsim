#pragma once

// Per-tree traits (docs/technical/asset-system/tree-variety.md, R1): which age class a
// placed tree is and which mesh variant it draws, as a pure function of its def, world
// position, and the world seed. Placement, harvest, selection, and dev spawn all have
// a def and a position, so they agree without saving anything or drawing from the
// chunk's placement RNG.

#include <glm/vec2.hpp>

#include <cstdint>

namespace assets {

	struct AssetDefinition;

	struct TreeTraits {
		uint8_t	 ageClass = 0; // index into AssetDefinition::ageClasses (0 when it has none)
		uint16_t variant = 0;  // mesh variant within the age class

		bool operator==(const TreeTraits&) const = default;
	};

	/// Feature size of the age field: old stands and young regrowth form patches about this wide.
	inline constexpr float kAgeFeatureMeters = 30.0F;

	/// The traits of an instance of `def` at `worldPosition`. The position is first
	/// quantised to the 0.1 m grid (decimeterCell), so callers holding slightly
	/// different floats for one tree agree.
	[[nodiscard]] TreeTraits computeTreeTraits(const AssetDefinition& def, glm::vec2 worldPosition, uint64_t worldSeed);

} // namespace assets
