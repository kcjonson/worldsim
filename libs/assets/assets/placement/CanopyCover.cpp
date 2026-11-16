#include "assets/placement/CanopyCover.h"

#include "assets/AssetRegistry.h"

#include <algorithm>
#include <string>
#include <vector>

namespace assets {

	world::grove::CanopyCover canopyCoverFromDefinitions(const AssetRegistry& registry) {
		// Sorted, so the sums add in one order on every platform.
		std::vector<std::string> names = registry.getDefinitionNames();
		std::sort(names.begin(), names.end());
		std::vector<const AssetDefinition*> trees;
		for (const std::string& name : names) {
			const AssetDefinition*			def = registry.getDefinition(name);
			const std::vector<std::string>& groups = def->placement.groups;
			if (std::find(groups.begin(), groups.end(), "trees") != groups.end()) {
				trees.push_back(def);
			}
		}

		world::grove::CanopyCover cover;
		for (size_t b = 0; b < cover.byBiome.size(); ++b) {
			const std::string biomeName = world::biomeToString(static_cast<world::Biome>(b));
			float			  density = 0.0F;
			for (const AssetDefinition* def : trees) {
				const BiomePlacement* bp = def->placement.findBiome(biomeName);
				if (bp != nullptr && bp->distribution == Distribution::Spaced) {
					const auto pitch = static_cast<float>(def->placement.samplePitch);
					density += bp->spawnChance / (pitch * pitch);
				}
			}
			cover.byBiome[b] = std::clamp((density - kCanopyOpenDensity) / (kCanopyClosedDensity - kCanopyOpenDensity), 0.0F, 1.0F);
		}
		return cover;
	}

} // namespace assets
