// Tests for AssetRegistry asynchronous loading (beginLoadAsync).

#include "assets/AssetRegistry.h"
#include "assets/AssetRegistry.test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace assets;

namespace {

	// <root>/libs/assets/assets/AssetRegistryAsync.test.cpp -> walk up to <root>.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__;
		return p.parent_path().parent_path().parent_path().parent_path();
	}

} // namespace

TEST(AssetRegistryAsyncTest, BeginLoadAsyncPopulatesRegistry) {
	auto& reg = AssetRegistry::Get();
	reg.clear();

	const std::filesystem::path world = projectRoot() / "assets" / "world";
	if (!std::filesystem::exists(world)) {
		GTEST_SKIP() << "assets/world not found";
	}

	reg.setSharedScriptsPath(projectRoot() / "assets" / "shared" / "scripts");

	EXPECT_FALSE(reg.loadProgress().started.load());
	reg.beginLoadAsync(world.string());
	EXPECT_TRUE(reg.loadProgress().started.load());

	waitForAsyncLoad(reg);
	EXPECT_GT(reg.getDefinitionNames().size(), 0U);
	EXPECT_GT(reg.loadProgress().defsLoaded.load(), 0);
	EXPECT_EQ(reg.getValidationReport().errorCount(), 0); // shipped library is clean

	// clear() joins the worker and resets progress.
	reg.clear();
	EXPECT_FALSE(reg.loadProgress().started.load());
}

// A load that fails early must leave the validation report reflecting the failure
// (not stale/empty), so an async splash can reliably gate on it.
TEST(AssetRegistryAsyncTest, MissingFolderRecordsValidationError) {
	auto& reg = AssetRegistry::Get();
	reg.clear();

	const size_t loaded = reg.loadDefinitionsFromFolder("__no_such_assets_folder__");
	EXPECT_EQ(loaded, 0U);
	EXPECT_GT(reg.getValidationReport().errorCount(), 0);

	reg.clear();
}

// Capability bitmask must cover ALL 9 capability types, including Storage at bit 8 and Craftable
// at bit 7 -- both overflow a uint8_t and were dropped before the mask widened to uint16_t and
// kCapabilityTypeCount went 7 -> 9. This guards the widening directly: if getCapabilityMask ever
// narrows back to uint8_t, the Storage bit vanishes and this test fails (the indirect storage-haul
// tests would all still pass, so the bug would otherwise slip through).
TEST(AssetRegistryCapabilityTest, MaskCoversStorageAndCraftableHighBits) {
	auto& reg = AssetRegistry::Get();
	reg.clearDefinitions();

	AssetDefinition def;
	def.defName = "Test_StorageCraftable";
	def.label = "Test Storage Craftable";
	def.category = ItemCategory::Furniture;
	def.capabilities.storage = StorageCapability{};   // bit 8 -- overflows uint8_t
	def.capabilities.craftable = CraftableCapability{}; // bit 7 -- also fixed by the count bump
	reg.registerTestDefinition(std::move(def));

	const uint32_t id = reg.getDefNameId("Test_StorageCraftable");
	ASSERT_NE(id, 0u) << "the def must get a non-zero interning id";

	const uint16_t mask = reg.getCapabilityMask(id);
	const uint16_t storageBit = static_cast<uint16_t>(1u << static_cast<size_t>(CapabilityType::Storage));
	const uint16_t craftableBit = static_cast<uint16_t>(1u << static_cast<size_t>(CapabilityType::Craftable));
	EXPECT_NE(mask & storageBit, 0) << "Storage (bit 8) must survive in the widened mask";
	EXPECT_NE(mask & craftableBit, 0) << "Craftable (bit 7) must survive in the widened mask";
	EXPECT_TRUE(reg.hasCapability(id, CapabilityType::Storage)) << "hasCapability must see Storage at bit 8";
	EXPECT_TRUE(reg.hasCapability(id, CapabilityType::Craftable)) << "hasCapability must see Craftable at bit 7";

	reg.clearDefinitions();
}

namespace {

	// The yield index, worked out the slow way: every definition, asked whether it is harvestable.
	std::map<std::string, std::vector<uint32_t>> harvestablesByYieldScan(const AssetRegistry& reg) {
		std::map<std::string, std::vector<uint32_t>> byYield;
		for (const std::string& name : reg.getDefinitionNames()) {
			const AssetDefinition* def = reg.getDefinition(name);
			if (def != nullptr && def->capabilities.harvestable.has_value() && !def->capabilities.harvestable->yieldDefName.empty()) {
				byYield[def->capabilities.harvestable->yieldDefName].push_back(reg.getDefNameId(name));
			}
		}
		for (auto& [yield, ids] : byYield) {
			std::ranges::sort(ids);
		}
		return byYield;
	}

	void expectYieldIndexMatchesScan(const AssetRegistry& reg) {
		const auto byYield = harvestablesByYieldScan(reg);
		ASSERT_FALSE(byYield.empty()) << "the shipped library has harvestables";
		for (const auto& [yield, ids] : byYield) {
			EXPECT_EQ(reg.harvestablesYielding(yield), ids) << yield;
		}
	}

} // namespace

// The yield index after a folder load, after clear(), and after the asset manager's Reload (clear,
// then load the folder again): each time it names exactly the harvestables yielding each item.
TEST(AssetRegistryYieldIndexTest, MatchesDefinitionsAcrossReload) {
	auto& reg = AssetRegistry::Get();
	reg.clear();
	const std::filesystem::path world = projectRoot() / "assets" / "world";
	if (!std::filesystem::exists(world)) {
		GTEST_SKIP() << "assets/world not found";
	}
	reg.setSharedScriptsPath(projectRoot() / "assets" / "shared" / "scripts");

	ASSERT_GT(reg.loadDefinitionsFromFolder(world.string()), 0U);
	expectYieldIndexMatchesScan(reg);
	const std::string someYield = harvestablesByYieldScan(reg).begin()->first;

	reg.clear();
	EXPECT_TRUE(reg.harvestablesYielding(someYield).empty()) << "clear() empties the index";

	ASSERT_GT(reg.loadDefinitionsFromFolder(world.string()), 0U);
	expectYieldIndexMatchesScan(reg);

	reg.clear();
}

// A test definition registered again with another yield moves to that yield's list.
TEST(AssetRegistryYieldIndexTest, ReregisteredDefinitionMovesToItsNewYield) {
	auto& reg = AssetRegistry::Get();
	reg.clearDefinitions();
	const auto registerBush = [&](const std::string& yield) {
		AssetDefinition bush;
		bush.defName = "Test_YieldBush";
		bush.capabilities.harvestable = HarvestableCapability{};
		bush.capabilities.harvestable->yieldDefName = yield;
		reg.registerTestDefinition(std::move(bush));
	};

	registerBush("Test_YieldA");
	const uint32_t id = reg.getDefNameId("Test_YieldBush");
	EXPECT_EQ(reg.harvestablesYielding("Test_YieldA"), std::vector<uint32_t>{id});
	registerBush("Test_YieldB");
	EXPECT_TRUE(reg.harvestablesYielding("Test_YieldA").empty());
	EXPECT_EQ(reg.harvestablesYielding("Test_YieldB"), std::vector<uint32_t>{id});

	reg.clearDefinitions();
	EXPECT_TRUE(reg.harvestablesYielding("Test_YieldB").empty());
}
