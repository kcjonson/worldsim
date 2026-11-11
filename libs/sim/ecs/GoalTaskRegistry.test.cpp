#include "GoalTaskRegistry.h"

#include "World.h"
#include "components/Inventory.h"
#include "components/Packaged.h"
#include "components/StorageConfiguration.h"
#include "components/StructureBlueprint.h"
#include "components/WorkQueue.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>

#include <gtest/gtest.h>

#include <map>
#include <set>

namespace ecs::test {

	class GoalTaskRegistryTest : public ::testing::Test {
	  protected:
		void SetUp() override { GoalTaskRegistry::Get().clear(); }
		void TearDown() override { GoalTaskRegistry::Get().clear(); }

		static uint64_t make(
			TaskType type, GoalOwner owner, EntityID destination, std::optional<uint64_t> parent = std::nullopt) {
			GoalTask goal;
			goal.type = type;
			goal.owner = owner;
			goal.destinationEntity = destination;
			goal.parentGoalId = parent;
			return GoalTaskRegistry::Get().createGoal(std::move(goal));
		}

		static std::set<uint64_t> ids(const std::vector<const GoalTask*>& goals) {
			std::set<uint64_t> out;
			for (const GoalTask* goal : goals) {
				out.insert(goal->id);
			}
			return out;
		}

		// Every lookup the registry offers must describe the same goal set getGoalsMatching walks.
		static void expectIndexesAgreeWithGoalSet() {
			auto&	   registry = GoalTaskRegistry::Get();
			const auto all = registry.getGoalsMatching([](const GoalTask&) { return true; });
			EXPECT_EQ(all.size(), registry.goalCount());

			std::map<GoalOwner, std::set<uint64_t>> byOwner;
			std::map<TaskType, std::set<uint64_t>>	byType;
			std::map<uint64_t, std::set<uint64_t>>	byParent;
			for (const GoalTask* goal : all) {
				EXPECT_EQ(registry.getGoal(goal->id), goal);
				byOwner[goal->owner].insert(goal->id);
				byType[goal->type].insert(goal->id);
				if (goal->parentGoalId.has_value()) {
					byParent[*goal->parentGoalId].insert(goal->id);
				} else {
					const GoalTask* found = registry.getGoalByDestination(goal->destinationEntity);
					ASSERT_NE(found, nullptr);
					EXPECT_EQ(found->id, goal->id);
				}
			}

			for (const auto& [owner, expected] : byOwner) {
				EXPECT_EQ(ids(registry.getGoalsByOwner(owner)), expected);
				EXPECT_EQ(registry.goalCount(owner), expected.size());
			}
			for (const auto& [type, expected] : byType) {
				EXPECT_EQ(ids(registry.getGoalsOfType(type)), expected);
				EXPECT_EQ(registry.goalCount(type), expected.size());
			}
			for (const auto& [parent, expected] : byParent) {
				EXPECT_EQ(ids(registry.getChildGoals(parent)), expected);
			}

			// Owners with no goals left answer empty rather than stale.
			for (GoalOwner owner : {GoalOwner::StorageGoalSystem,
									GoalOwner::CraftingGoalSystem,
									GoalOwner::BuildGoalSystem,
									GoalOwner::ConstructionGoalSystem}) {
				if (!byOwner.contains(owner)) {
					EXPECT_TRUE(registry.getGoalsByOwner(owner).empty());
					EXPECT_EQ(registry.goalCount(owner), 0U);
				}
			}
		}
	};

	// A second session in the same process must hand out the same claim sequence numbers as the first.
	TEST_F(GoalTaskRegistryTest, ClaimSequenceRestartsAfterClear) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t first = registry.allocateClaimSeq();
		const uint64_t second = registry.allocateClaimSeq();
		EXPECT_NE(first, 0U);
		EXPECT_NE(first, second);

		registry.clear();
		EXPECT_EQ(registry.allocateClaimSeq(), first);
		EXPECT_EQ(registry.allocateClaimSeq(), second);
	}

	// Pinned: removal cascades through grandchildren and leaves unrelated goals and their indexes alone.
	TEST_F(GoalTaskRegistryTest, RemoveGoalWithChildrenRemovesDescendantsOnly) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t craft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 10);
		const uint64_t haul = make(TaskType::Haul, GoalOwner::CraftingGoalSystem, 10, craft);
		const uint64_t grandchild = make(TaskType::Harvest, GoalOwner::CraftingGoalSystem, 10, haul);
		const uint64_t otherCraft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 20);
		const uint64_t otherChild = make(TaskType::Haul, GoalOwner::CraftingGoalSystem, 20, otherCraft);

		registry.removeGoalWithChildren(craft);

		EXPECT_EQ(registry.getGoal(craft), nullptr);
		EXPECT_EQ(registry.getGoal(haul), nullptr);
		EXPECT_EQ(registry.getGoal(grandchild), nullptr);
		EXPECT_NE(registry.getGoal(otherCraft), nullptr);
		EXPECT_NE(registry.getGoal(otherChild), nullptr);
		EXPECT_EQ(registry.getGoalByDestination(10), nullptr);
		EXPECT_EQ(registry.goalCount(), 2U);
		expectIndexesAgreeWithGoalSet();
	}

	TEST_F(GoalTaskRegistryTest, IndexesAgreeWithGoalSetThroughCreateStatusChangeAndRemoval) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t storage = make(TaskType::Haul, GoalOwner::StorageGoalSystem, 1);
		const uint64_t craft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 2);
		const uint64_t harvest = make(TaskType::Harvest, GoalOwner::CraftingGoalSystem, 2, craft);
		make(TaskType::Haul, GoalOwner::CraftingGoalSystem, 2, craft);
		make(TaskType::Build, GoalOwner::ConstructionGoalSystem, 3);
		expectIndexesAgreeWithGoalSet();

		registry.updateGoal(craft, [](GoalTask& goal) { goal.status = GoalStatus::Blocked; });
		registry.updateGoal(harvest, [](GoalTask& goal) { goal.status = GoalStatus::NoSource; });
		expectIndexesAgreeWithGoalSet();

		registry.removeGoal(harvest);
		expectIndexesAgreeWithGoalSet();
		registry.removeGoalByDestination(1);
		EXPECT_EQ(registry.getGoal(storage), nullptr);
		expectIndexesAgreeWithGoalSet();
		registry.removeGoalWithChildren(craft);
		expectIndexesAgreeWithGoalSet();
		EXPECT_EQ(registry.goalCount(), 1U);
	}

	// Pinned: a second top-level goal for the same destination replaces the first in place, keeping
	// the id and moving the owner and type index entries with it.
	TEST_F(GoalTaskRegistryTest, CreatingASecondTopLevelGoalForADestinationReplacesItKeepingTheId) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t first = make(TaskType::Haul, GoalOwner::StorageGoalSystem, 5);
		const uint64_t second = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 5);

		EXPECT_EQ(second, first);
		EXPECT_EQ(registry.goalCount(), 1U);
		EXPECT_EQ(registry.getGoal(first)->type, TaskType::Craft);
		EXPECT_EQ(registry.goalCount(GoalOwner::StorageGoalSystem), 0U);
		EXPECT_EQ(registry.goalCount(GoalOwner::CraftingGoalSystem), 1U);
		expectIndexesAgreeWithGoalSet();
	}

	// Remaining need is counted in items at the root, from world truth: what the destination still
	// lacks, never a slot count or a counter on a goal.
	class RemainingNeedTest : public GoalTaskRegistryTest {
	  protected:
		void SetUp() override {
			GoalTaskRegistryTest::SetUp();
			auto& assets = assets::AssetRegistry::Get();
			assets.clearDefinitions();
			for (const char* name : {"Stone", "Stick"}) {
				assets::AssetDefinition def;
				def.defName = name;
				def.label = name;
				def.category = assets::ItemCategory::RawMaterial;
				def.itemProperties = assets::ItemProperties{};
				def.itemProperties->stackSize = 10;
				assets.registerTestDefinition(std::move(def));
			}
			stone = assets.getDefNameId("Stone");
			stick = assets.getDefNameId("Stick");
			assets::RecipeRegistry::Get().clear();
			assets::RecipeDef recipe;
			recipe.defName = "Recipe_Need";
			recipe.label = "Need";
			recipe.inputs = {{.defName = "Stone", .count = 3}, {.defName = "Stick", .count = 1}};
			assets::RecipeRegistry::Get().registerTestRecipe(std::move(recipe));
		}
		void TearDown() override {
			assets::RecipeRegistry::Get().clear();
			assets::AssetRegistry::Get().clearDefinitions();
			GoalTaskRegistryTest::TearDown();
		}

		World	 world;
		uint32_t stone = 0;
		uint32_t stick = 0;
	};

	TEST_F(RemainingNeedTest, StorageNeedIsItemCountedAgainstMaxOrRoomAndMinimumForHarvest) {
		auto&		   registry = GoalTaskRegistry::Get();
		const EntityID box = world.createEntity();
		auto		   inventory = Inventory::createForStorage();
		inventory.maxCapacity = 3;
		inventory.addItem("Stone", 12); // two slots: 10 and 2
		world.addComponent<Inventory>(box, std::move(inventory));
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .minAmount = 15, .maxAmount = 20});
		config.addRule(StorageRule{.defName = "Stick", .category = assets::ItemCategory::RawMaterial});
		world.addComponent<StorageConfiguration>(box, std::move(config));
		const uint64_t root = make(TaskType::Haul, GoalOwner::StorageGoalSystem, box);

		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 8U) << "max 20 less the 12 held";
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Harvest), 3U) << "minimum 15 less the 12 held";
		EXPECT_EQ(registry.remainingNeed(world, root, stick, NeedBasis::Stock), 10U) << "no max: one free slot of room";

		world.getComponent<Inventory>(box)->addItem("Stone", 4);
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 4U) << "a deposit shows at once";
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Harvest), 0U);
	}

	// A category wildcard's minimum never drives cutting, so it does not inflate the cut need of an
	// item a rule names; stock hauled in is still bounded by the wildcard's room.
	TEST_F(RemainingNeedTest, AWildcardMinimumDoesNotInflateTheCutNeed) {
		auto&		   registry = GoalTaskRegistry::Get();
		const EntityID box = world.createEntity();
		world.addComponent<Inventory>(box, Inventory::createForStorage());
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "*", .category = assets::ItemCategory::RawMaterial, .minAmount = 50});
		config.addRule(StorageRule{.defName = "Stick", .category = assets::ItemCategory::RawMaterial, .minAmount = 4});
		world.addComponent<StorageConfiguration>(box, std::move(config));
		const uint64_t root = make(TaskType::Haul, GoalOwner::StorageGoalSystem, box);

		EXPECT_EQ(registry.remainingNeed(world, root, stick, NeedBasis::Harvest), 4U) << "the named rule's minimum only";
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Harvest), 0U) << "no rule names Stone";
	}

	// A packaged box is being moved: no haul, carry-in, or cut is aimed at it until it is placed.
	TEST_F(RemainingNeedTest, APackagedBoxWantsNothing) {
		auto&		   registry = GoalTaskRegistry::Get();
		const EntityID box = world.createEntity();
		world.addComponent<Inventory>(box, Inventory::createForStorage());
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .minAmount = 5});
		world.addComponent<StorageConfiguration>(box, std::move(config));
		const uint64_t root = make(TaskType::Haul, GoalOwner::StorageGoalSystem, box);
		ASSERT_GT(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 0U);

		world.addComponent<Packaged>(box, Packaged{std::nullopt, false});
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 0U);
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Harvest), 0U);
		world.removeComponent<Packaged>(box);
		EXPECT_GT(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 0U) << "placed again, it wants stock again";
	}

	TEST_F(RemainingNeedTest, CraftNeedIsTheNextJobsBillOfMaterialsLessTheStore) {
		auto&		   registry = GoalTaskRegistry::Get();
		const EntityID station = world.createEntity();
		world.addComponent<WorkQueue>(station).addJob("Recipe_Need", 2);
		auto& store = world.addComponent<Inventory>(station, Inventory::createForStorage());
		store.addItem("Stone", 1);
		const uint64_t root = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, station);
		const uint64_t child = make(TaskType::Haul, GoalOwner::CraftingGoalSystem, station, root);

		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 2U);
		EXPECT_EQ(registry.remainingNeed(world, root, stick, NeedBasis::Stock), 1U);
		EXPECT_EQ(registry.remainingNeed(world, child, stone, NeedBasis::Stock), 0U) << "need is asked of the root";

		store.addItem("Stone", 2);
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 0U);
		store.removeItem("Stone", 3); // a unit consumed its inputs
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 3U) << "the next unit needs them again";
	}

	TEST_F(RemainingNeedTest, ConstructionNeedIsTheManifestWhileTheUmbrellaAcceptsTheItem) {
		auto&		   registry = GoalTaskRegistry::Get();
		const EntityID site = world.createEntity();
		StructureBlueprint blueprint;
		blueprint.required = {{"Stone", 6}};
		blueprint.recordDelivery("Stone", 2);
		world.addComponent<StructureBlueprint>(site, std::move(blueprint));
		const uint64_t root = make(TaskType::Build, GoalOwner::ConstructionGoalSystem, site);

		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 0U) << "materials are not due yet";
		registry.updateGoal(root, [&](GoalTask& goal) { goal.acceptedDefNameIds = {stone}; });
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Stock), 4U);
		EXPECT_EQ(registry.remainingNeed(world, root, stick, NeedBasis::Stock), 0U);
	}

} // namespace ecs::test
