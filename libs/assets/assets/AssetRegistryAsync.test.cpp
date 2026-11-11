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
			const uint32_t yieldId = reg.getDefNameId(yield);
			if (yieldId != 0) {
				EXPECT_EQ(reg.harvestablesYielding(yieldId), ids) << yield;
			}
		}
		for (const std::string& name : reg.getDefinitionNames()) {
			EXPECT_EQ(reg.getDefinitionById(reg.getDefNameId(name)), reg.getDefinition(name)) << name;
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
	const uint32_t someYield = reg.getDefNameId(harvestablesByYieldScan(reg).begin()->first);

	reg.clear();
	EXPECT_TRUE(reg.harvestablesYielding(someYield).empty()) << "clear() empties the index";
	EXPECT_EQ(reg.getDefinitionById(someYield), nullptr);

	ASSERT_GT(reg.loadDefinitionsFromFolder(world.string()), 0U);
	expectYieldIndexMatchesScan(reg);

	reg.clear();
}

// A test definition registered again with another yield moves to that yield's list, and a yield
// registered after its harvestable still finds it.
TEST(AssetRegistryYieldIndexTest, ReregisteredDefinitionMovesToItsNewYield) {
	auto& reg = AssetRegistry::Get();
	reg.clearDefinitions();
	const auto registerDef = [&](const std::string& name, const std::string& yield) {
		AssetDefinition def;
		def.defName = name;
		if (!yield.empty()) {
			def.capabilities.harvestable = HarvestableCapability{};
			def.capabilities.harvestable->yieldDefName = yield;
		}
		reg.registerTestDefinition(std::move(def));
	};

	registerDef("Test_YieldA", "");
	registerDef("Test_YieldBush", "Test_YieldA");
	const uint32_t id = reg.getDefNameId("Test_YieldBush");
	const uint32_t yieldA = reg.getDefNameId("Test_YieldA");
	EXPECT_EQ(reg.harvestablesYielding(yieldA), std::vector<uint32_t>{id});
	registerDef("Test_YieldBush", "Test_YieldB");
	EXPECT_TRUE(reg.harvestablesYielding(yieldA).empty());
	registerDef("Test_YieldB", "");
	const uint32_t yieldB = reg.getDefNameId("Test_YieldB");
	EXPECT_EQ(reg.harvestablesYielding(yieldB), std::vector<uint32_t>{id}) << "filed before the yield was defined";
	EXPECT_EQ(reg.getDefinitionById(id), reg.getDefinition("Test_YieldBush"));

	reg.clearDefinitions();
	EXPECT_TRUE(reg.harvestablesYielding(yieldB).empty());
}

// A synthetic definition whose name was first interned as an undefined harvest yield keeps that id and
// takes its capabilities; a real definition's capabilities are not replaced by a synthetic registration.
TEST(AssetRegistryYieldIndexTest, ASyntheticDefinitionNamedAsAYieldTakesItsCapabilities) {
	auto& reg = AssetRegistry::Get();
	reg.clearDefinitions();
	AssetDefinition spring;
	spring.defName = "Test_Spring";
	spring.capabilities.harvestable = HarvestableCapability{};
	spring.capabilities.harvestable->yieldDefName = "Test_SynthWater";
	reg.registerTestDefinition(std::move(spring));
	const uint32_t interned = reg.getDefNameId("Test_SynthWater");
	ASSERT_NE(interned, 0U);
	ASSERT_EQ(reg.getCapabilityMask(interned), 0U);

	const auto	   drinkable = static_cast<uint16_t>(1U << static_cast<uint8_t>(CapabilityType::Drinkable));
	const uint32_t id = reg.registerSyntheticDefinition("Test_SynthWater", drinkable);
	EXPECT_EQ(id, interned);
	EXPECT_TRUE(reg.hasCapability(id, CapabilityType::Drinkable));

	const uint32_t springId = reg.getDefNameId("Test_Spring");
	const uint16_t springMask = reg.getCapabilityMask(springId);
	EXPECT_EQ(reg.registerSyntheticDefinition("Test_Spring", drinkable), springId);
	EXPECT_EQ(reg.getCapabilityMask(springId), springMask) << "a real definition keeps its own capabilities";

	reg.clearDefinitions();
}
