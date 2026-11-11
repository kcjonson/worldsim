#pragma once

// Each biome's canopy cover for the forest floor (world::grove::canopyWeight), read off the tree
// defs: the spaced "trees" members that place in a biome, as candidate trees a square meter
// (spawn chance over sample pitch squared), ramped from open land to a closed canopy. Chunk
// generation takes the table; the world library can't read defs itself.

#include "world/generation/GroveField.h"

namespace assets {

	class AssetRegistry;

	/// Candidate trees a square meter at or under which a biome is open land (no floor): the
	/// grassland's lone maples and the beaches' palms sit well below it.
	inline constexpr float kCanopyOpenDensity = 0.005F;
	/// Candidate trees a square meter at or over which a biome is closed forest (cover 1).
	inline constexpr float kCanopyClosedDensity = 0.02F;

	[[nodiscard]] world::grove::CanopyCover canopyCoverFromDefinitions(const AssetRegistry& registry);

} // namespace assets
