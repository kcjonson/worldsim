#include "assets/placement/BiomeProvisions.h"

#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace assets {
	namespace {

		BiomePlacement placedIn(worldgen::Biome biome, float spawnChance, std::string nearTileType = {}) {
			BiomePlacement placement;
			placement.biomeName = worldgen::biomeToString(biome);
			placement.spawnChance = spawnChance;
			placement.nearTileType = std::move(nearTileType);
			return placement;
		}

		AssetDefinition harvestable(const std::string& name, const std::string& yield, uint16_t samplePitch) {
			AssetDefinition definition;
			definition.defName = name;
			definition.placement.samplePitch = samplePitch;
			definition.capabilities.harvestable = HarvestableCapability{};
			definition.capabilities.harvestable->yieldDefName = yield;
			return definition;
		}

		AssetDefinition item(const std::string& name, bool edible) {
			AssetDefinition definition;
			definition.defName = name;
			definition.itemProperties = ItemProperties{};
			if (edible) {
				definition.itemProperties->edible = EdibleCapability{};
			}
			return definition;
		}

		const worldgen::BiomeProvisions& provisionsOf(const worldgen::BiomeProvisionTable& table, worldgen::Biome biome) {
			return table[static_cast<size_t>(biome)];
		}

	} // namespace

	TEST(BiomeProvisions, EdibleYieldsAreFoodWoodYieldsAreWoodAndNearPlacementsDoNotCount) {
		auto& registry = AssetRegistry::Get();
		registry.clearDefinitions();
		registry.registerTestDefinition(item("Test_Berry", true));
		registry.registerTestDefinition(item("Wood", false));
		registry.registerTestDefinition(item("Test_Stick", false));

		AssetDefinition bush = harvestable("Test_BerryBush", "Test_Berry", 4);
		bush.placement.biomes.push_back(placedIn(worldgen::Biome::TemperateGrassland, 0.02F));
		bush.placement.biomes.push_back(placedIn(worldgen::Biome::BorealForest, 0.1F, "Mud"));
		registry.registerTestDefinition(std::move(bush));

		AssetDefinition tree = harvestable("Test_Oak", "Wood", 2);
		tree.placement.biomes.push_back(placedIn(worldgen::Biome::TemperateGrassland, 0.01F));
		registry.registerTestDefinition(std::move(tree));

		AssetDefinition twigs = harvestable("Test_Twigs", "Test_Stick", 1);
		twigs.placement.biomes.push_back(placedIn(worldgen::Biome::TemperateGrassland, 0.5F));
		registry.registerTestDefinition(std::move(twigs));

		const worldgen::BiomeProvisionTable table = biomeProvisionsFromDefinitions(registry);

		const worldgen::BiomeProvisions& grassland = provisionsOf(table, worldgen::Biome::TemperateGrassland);
		EXPECT_FLOAT_EQ(grassland.foodSourcesPerSquareKilometer, 0.02F / 16.0F * 1e6F);
		EXPECT_FLOAT_EQ(grassland.woodSourcesPerSquareKilometer, 0.01F / 4.0F * 1e6F) << "the stick yield is neither";

		const worldgen::BiomeProvisions& boreal = provisionsOf(table, worldgen::Biome::BorealForest);
		EXPECT_FLOAT_EQ(boreal.foodSourcesPerSquareKilometer, 0.0F) << "the planet can't see mud";
		EXPECT_FLOAT_EQ(boreal.woodSourcesPerSquareKilometer, 0.0F);

		registry.clearDefinitions();
	}

} // namespace assets
