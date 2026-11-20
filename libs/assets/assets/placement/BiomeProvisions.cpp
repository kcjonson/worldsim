#include "assets/placement/BiomeProvisions.h"

#include "assets/AssetRegistry.h"

#include <algorithm>
#include <string>
#include <vector>

namespace assets {

	namespace {
		constexpr const char* kWoodItemDefName = "Wood";
		constexpr float		  kSquareMetersPerSquareKilometer = 1e6F;
	} // namespace

	worldgen::BiomeProvisionTable biomeProvisionsFromDefinitions(const AssetRegistry& registry) {
		// Sorted, so the sums add in one order on every platform.
		std::vector<std::string> names = registry.getDefinitionNames();
		std::sort(names.begin(), names.end());

		worldgen::BiomeProvisionTable table{};
		for (const std::string& name : names) {
			const AssetDefinition* def = registry.getDefinition(name);
			if (def == nullptr || !def->capabilities.harvestable.has_value()) {
				continue;
			}
			const std::string&	   yieldName = def->capabilities.harvestable->yieldDefName;
			const AssetDefinition* yield = registry.getDefinition(yieldName);
			const bool			   food = yield != nullptr && yield->isEdible();
			const bool			   wood = yieldName == kWoodItemDefName;
			if (!food && !wood) {
				continue;
			}
			const auto pitch = static_cast<float>(def->placement.samplePitch);
			for (size_t b = 0; b < table.size(); ++b) {
				const BiomePlacement* placement = def->placement.findBiome(worldgen::biomeToString(static_cast<worldgen::Biome>(b)));
				// A conditional placement (near="Mud") is left out: the planet can't see the surface it keys on.
				if (placement == nullptr || !placement->nearTileType.empty()) {
					continue;
				}
				const float density = placement->spawnChance / (pitch * pitch) * kSquareMetersPerSquareKilometer;
				if (food) {
					table[b].foodSourcesPerSquareKilometer += density;
				}
				if (wood) {
					table[b].woodSourcesPerSquareKilometer += density;
				}
			}
		}
		return table;
	}

} // namespace assets
