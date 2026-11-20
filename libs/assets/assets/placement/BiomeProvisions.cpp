#include "assets/placement/BiomeProvisions.h"

#include "assets/AssetRegistry.h"

#include <algorithm>
#include <string>
#include <vector>

namespace assets {

	namespace {
		constexpr float kSquareMetersPerSquareKilometer = 1e6F;

		bool declaresGroup(const AssetDefinition& definition, const char* group) {
			const std::vector<std::string>& groups = definition.placement.groups;
			return std::find(groups.begin(), groups.end(), group) != groups.end();
		}

		// Plants a successful roll places: a clump of clumpSizeMin to clumpSizeMax, uniformly, as
		// PlacementExecutor draws it; one otherwise.
		float instancesPerRoll(const BiomePlacement& placement) {
			if (placement.distribution != Distribution::Clumped) {
				return 1.0F;
			}
			return 0.5F * static_cast<float>(placement.clumping.clumpSizeMin + placement.clumping.clumpSizeMax);
		}
	} // namespace

	worldgen::BiomeProvisionTable biomeProvisionsFromDefinitions(const AssetRegistry& registry) {
		// Sorted, so the sums add in one order on every platform.
		std::vector<std::string> names = registry.getDefinitionNames();
		std::sort(names.begin(), names.end());

		worldgen::BiomeProvisionTable table{};
		for (const std::string& name : names) {
			const AssetDefinition* definition = registry.getDefinition(name);
			if (definition == nullptr || !definition->capabilities.harvestable.has_value()) {
				continue;
			}
			const std::string&	   yieldName = definition->capabilities.harvestable->yieldDefName;
			const AssetDefinition* yield = registry.getDefinition(yieldName);
			const bool			   food = yield != nullptr && yield->isEdible();
			const bool			   wood = yield != nullptr && declaresGroup(*yield, kWoodItemGroup);
			if (!food && !wood) {
				continue;
			}
			const auto pitch = static_cast<float>(definition->placement.samplePitch);
			for (size_t biome = 0; biome < table.size(); ++biome) {
				const BiomePlacement* placement =
					definition->placement.findBiome(worldgen::biomeToString(static_cast<worldgen::Biome>(biome)));
				// A conditional placement (near="Mud") is left out: the planet can't see the surface it keys on.
				if (placement == nullptr || !placement->nearTileType.empty()) {
					continue;
				}
				const float density =
					placement->spawnChance * instancesPerRoll(*placement) / (pitch * pitch) * kSquareMetersPerSquareKilometer;
				if (food) {
					table[biome].foodSourcesPerSquareKilometer += density;
				}
				if (wood) {
					table[biome].woodSourcesPerSquareKilometer += density;
				}
			}
		}
		return table;
	}

} // namespace assets
