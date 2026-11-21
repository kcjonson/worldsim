#pragma once

// Each biome's food and wood sources a square kilometer, read off the flora defs: the harvestable
// defs that place in a biome unconditionally, as spawn chance over sample pitch squared (the rolls a
// square kilometer, the measure canopyCoverFromDefinitions uses) times the plants a roll places (a
// clumped placement's mean clump, (clumpSizeMin + clumpSizeMax) / 2, else one), added to food when
// the yield item is edible and to wood when the yield item declares the group kWoodItemGroup.
// Landing-site rating takes the table; worldgen can't read defs itself.

#include <worldgen/sampling/LandingSite.h>

namespace assets {

	class AssetRegistry;

	inline constexpr const char* kWoodItemGroup = "wood";

	[[nodiscard]] worldgen::BiomeProvisionTable biomeProvisionsFromDefinitions(const AssetRegistry& registry);

} // namespace assets
