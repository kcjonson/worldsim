#include "ColonyGoalWork.h"

#include "../ClaimIndex.h"
#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Colonist.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Needs.h"
#include "../components/StorageConfiguration.h"
#include "../components/Task.h"

#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

#include <span>
#include <vector>

namespace ecs::test {

	// The task panel's colonists per goal, from each colonist's task and last decision, and its
	// quantities, from the claim index and the root's remaining need.
	class ColonyGoalWorkTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			GoalTaskRegistry::Get().clear();
			auto& assets = assets::AssetRegistry::Get();
			assets.clearDefinitions();
			assets::AssetDefinition def;
			def.defName = "Stone";
			def.label = "Stone";
			def.category = assets::ItemCategory::RawMaterial;
			def.itemProperties = assets::ItemProperties{};
			def.itemProperties->stackSize = 10;
			assets.registerTestDefinition(std::move(def));
			stone = assets.getDefNameId("Stone");
		}
		void TearDown() override {
			assets::AssetRegistry::Get().clearDefinitions();
			GoalTaskRegistry::Get().clear();
		}

		static uint64_t make(TaskType type, GoalOwner owner, EntityID destination, std::optional<uint64_t> parent = std::nullopt) {
			GoalTask goal;
			goal.type = type;
			goal.owner = owner;
			goal.destinationEntity = destination;
			goal.parentGoalId = parent;
			return GoalTaskRegistry::Get().createGoal(std::move(goal));
		}

		/// A box taking Stone, and its storage umbrella, accepting Stone.
		uint64_t stoneBox(StorageRule rule) {
			const EntityID box = world.createEntity();
			world.addComponent<Inventory>(box, Inventory::createForStorage());
			StorageConfiguration config;
			config.addRule(rule);
			world.addComponent<StorageConfiguration>(box, std::move(config));
			const uint64_t root = make(TaskType::Haul, GoalOwner::StorageGoalSystem, box);
			GoalTaskRegistry::Get().updateGoal(root, [&](GoalTask& goal) { goal.acceptedDefNameIds = {stone}; });
			return root;
		}

		EntityID colonist(const Task& task, std::vector<EvaluatedOption> options) {
			const EntityID entity = world.createEntity();
			world.addComponent<Colonist>(entity, Colonist{.name = "Colonist"});
			world.addComponent<Task>(entity, task);
			DecisionTrace trace;
			trace.options = std::move(options);
			world.addComponent<DecisionTrace>(entity, std::move(trace));
			return entity;
		}

		static Task hauling(uint64_t goal, uint32_t item, uint32_t inFlight) {
			Task task;
			task.type = TaskType::Haul;
			task.haulGoalId = goal;
			task.haulItemDefNameId = item;
			EXPECT_TRUE(task.claims.push(Claim::inFlight(goal, item, inFlight)));
			return task;
		}

		static Task cutting(uint64_t goal, uint64_t root, uint32_t item, uint32_t expectedYield) {
			Task task;
			task.type = TaskType::Harvest;
			task.harvestGoalId = goal;
			EXPECT_TRUE(task.claims.push(Claim::inFlight(root, item, expectedYield)));
			return task;
		}

		/// Seeing to `need` with a record of goods toward `root` set aside.
		static Task seeingTo(NeedType need, uint64_t root, uint32_t item, uint32_t carried) {
			Task task;
			task.type = TaskType::FulfillNeed;
			task.needToFulfill = need;
			task.suspended = SuspendedWork{.rootGoalId = root, .itemDefNameId = item};
			EXPECT_TRUE(task.suspended->claims.push(Claim::inFlight(root, item, carried)));
			return task;
		}

		static EvaluatedOption haulOption(uint64_t goal, OptionStatus status) {
			EvaluatedOption option;
			option.taskType = TaskType::Haul;
			option.haulGoalId = goal;
			option.status = status;
			return option;
		}

		static std::vector<EntityID> listed(std::span<const EntityID> colonists) { return {colonists.begin(), colonists.end()}; }

		World	   world;
		ClaimIndex claims;
		uint32_t   stone = 0;
	};

	TEST_F(ColonyGoalWorkTest, ListsEveryColonistOfferedAGoalAndCountsItsInFlight) {
		auto&		   registry = GoalTaskRegistry::Get();
		const uint64_t root = stoneBox(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .maxAmount = 20});

		const EntityID first = colonist(hauling(root, stone, 3), {haulOption(root, OptionStatus::Selected)});
		const EntityID second =
			colonist(hauling(root, stone, 4), {haulOption(root, OptionStatus::Selected), haulOption(root, OptionStatus::Available)});
		const EntityID idle = colonist(Task{}, {haulOption(root, OptionStatus::NoRoom)});
		const EntityID notAColonist = world.createEntity();
		world.addComponent<Task>(notAColonist, hauling(root, stone, 1));

		const ColonyGoalWork work(world, claims);
		const GoalTask&		 goal = *registry.getGoal(root);
		EXPECT_EQ(listed(work.offeredTo(root)), (std::vector<EntityID>{first, second, idle})) << "each once, in view order";
		EXPECT_EQ(listed(work.workersOf(goal)), (std::vector<EntityID>{first, second})) << "colonists only";
		EXPECT_TRUE(work.hasWork(goal));
		EXPECT_FALSE(work.allSourcesClaimed(root));
		const auto count = work.itemCount(world, goal);
		ASSERT_TRUE(count.has_value());
		EXPECT_EQ(count->onTheWay, claims.inFlight(root, stone, ClaimExclusion{})) << "the claim index's figure";
		EXPECT_EQ(count->onTheWay, 8U);
	}

	// Right after a cut lands, the cutter hauls toward the box's root goal before his next decision offers it.
	TEST_F(ColonyGoalWorkTest, AStorageGoalWithAWorkerHasWorkWithoutAnOffer) {
		const uint64_t root = stoneBox(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .maxAmount = 20});
		EXPECT_FALSE(ColonyGoalWork(world, claims).hasWork(*GoalTaskRegistry::Get().getGoal(root)));

		colonist(hauling(root, stone, 2), {});
		EXPECT_TRUE(ColonyGoalWork(world, claims).hasWork(*GoalTaskRegistry::Get().getGoal(root)));
	}

	// A box's room is no shortfall: a box with no maximum would read "needs 1987".
	TEST_F(ColonyGoalWorkTest, AStorageHaulReportsNoNeed) {
		const uint64_t root = stoneBox(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial});
		ASSERT_GT(GoalTaskRegistry::Get().remainingNeed(world, root, stone, NeedBasis::Stock), 0U);
		colonist(hauling(root, stone, 2), {haulOption(root, OptionStatus::Selected)});

		const auto count = ColonyGoalWork(world, claims).itemCount(world, *GoalTaskRegistry::Get().getGoal(root));
		ASSERT_TRUE(count.has_value());
		EXPECT_EQ(count->needed, 0U);
		EXPECT_EQ(count->onTheWay, 2U);
	}

	// A cut stocking a box counts toward its minimum, net of what is on its way.
	TEST_F(ColonyGoalWorkTest, AStockingCutsNeedIsTheMinimumLessWhatIsOnItsWay) {
		auto&		   registry = GoalTaskRegistry::Get();
		const uint64_t root =
			stoneBox(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .minAmount = 6, .maxAmount = 20});
		const GoalTask& umbrella = *registry.getGoal(root);
		const uint64_t	cut = make(TaskType::Harvest, GoalOwner::StorageGoalSystem, umbrella.destinationEntity, root);
		registry.updateGoal(cut, [&](GoalTask& goal) { goal.acceptedDefNameIds = {stone}; });
		colonist(cutting(cut, root, stone, 2), {});

		const auto count = ColonyGoalWork(world, claims).itemCount(world, *registry.getGoal(cut));
		ASSERT_TRUE(count.has_value());
		EXPECT_EQ(registry.remainingNeed(world, root, stone, NeedBasis::Harvest), 6U);
		EXPECT_EQ(count->needed, 4U) << "the minimum 6 less the 2 on the way, not the max";
		EXPECT_EQ(count->onTheWay, 2U);
	}

	// What the AI counts as not coming is not on the way: a sleeping colonist's set-aside goods, and the
	// expected yield of a cutter who can't find a way to his tree.
	TEST_F(ColonyGoalWorkTest, ASleepersGoodsAndAStrandedCuttersYieldAreNotOnTheWay) {
		auto&		   registry = GoalTaskRegistry::Get();
		const uint64_t root =
			stoneBox(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .minAmount = 30, .maxAmount = 40});
		const uint64_t cut = make(TaskType::Harvest, GoalOwner::StorageGoalSystem, registry.getGoal(root)->destinationEntity, root);
		registry.updateGoal(cut, [&](GoalTask& goal) { goal.acceptedDefNameIds = {stone}; });

		colonist(seeingTo(NeedType::Thirst, root, stone, 4), {});
		colonist(seeingTo(NeedType::Energy, root, stone, 5), {});
		colonist(cutting(cut, root, stone, 2), {});
		Task stranded = cutting(cut, root, stone, 3);
		stranded.navState = NavState::CantFindWayTo;
		colonist(stranded, {});

		const auto count = ColonyGoalWork(world, claims).itemCount(world, *registry.getGoal(cut));
		ASSERT_TRUE(count.has_value());
		EXPECT_EQ(count->onTheWay, 6U) << "the drinker's 4 and the walking cutter's 2";
		EXPECT_EQ(count->needed, 24U);
	}

	// All claimed is the reason only when nothing else is: a free source someone's load keeps him from,
	// or one regrowing, is a different wait.
	TEST_F(ColonyGoalWorkTest, AllClaimedOnlyWhenNoOptionFoundASourceFree) {
		const uint64_t goal = make(TaskType::Haul, GoalOwner::StorageGoalSystem, 1);
		const uint64_t other = make(TaskType::Haul, GoalOwner::StorageGoalSystem, 2);
		const uint64_t loaded = make(TaskType::Haul, GoalOwner::StorageGoalSystem, 3);
		const uint64_t regrowing = make(TaskType::Haul, GoalOwner::StorageGoalSystem, 4);
		colonist(Task{}, {haulOption(goal, OptionStatus::Reserved), haulOption(loaded, OptionStatus::Reserved)});
		colonist(Task{}, {haulOption(other, OptionStatus::Available), haulOption(loaded, OptionStatus::NoRoom)});
		const EntityID waiting = colonist(Task{}, {haulOption(regrowing, OptionStatus::Reserved)});
		colonist(Task{}, {haulOption(regrowing, OptionStatus::Regrowing)});
		{
			const ColonyGoalWork work(world, claims);
			EXPECT_TRUE(work.allSourcesClaimed(goal));
			EXPECT_FALSE(work.allSourcesClaimed(other));
			EXPECT_FALSE(work.allSourcesClaimed(loaded)) << "a free source exists, his own load is in the way";
			EXPECT_FALSE(work.allSourcesClaimed(regrowing)) << "the wait is regrowth";
			EXPECT_EQ(listed(work.offeredTo(regrowing)), (std::vector<EntityID>{waiting})) << "a regrowing option is no offer";
		}

		colonist(Task{}, {haulOption(goal, OptionStatus::Available)});
		EXPECT_FALSE(ColonyGoalWork(world, claims).allSourcesClaimed(goal)) << "one colonist can take a source";
	}

	// A craft or a build works an entity, not a goal: it is an offer toward the goal that entity is the
	// destination of, the root and not its fetch child.
	TEST_F(ColonyGoalWorkTest, ACraftOrBuildOptionIsAnOfferTowardItsEntitysGoal) {
		const EntityID station = world.createEntity();
		const EntityID site = world.createEntity();
		const uint64_t craft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, station);
		const uint64_t fetch = make(TaskType::Haul, GoalOwner::CraftingGoalSystem, station, craft);
		const uint64_t build = make(TaskType::Build, GoalOwner::ConstructionGoalSystem, site);
		const auto	   entityOption = [](TaskType type, EntityID entity, OptionStatus status) {
			EvaluatedOption option;
			option.taskType = type;
			option.stationEntityId = type == TaskType::Craft ? static_cast<uint64_t>(entity) : 0U;
			option.buildBlueprintEntityId = type == TaskType::Build ? static_cast<uint64_t>(entity) : 0U;
			option.status = status;
			return option;
		};
		const EntityID crafter = colonist(Task{}, {entityOption(TaskType::Craft, station, OptionStatus::Selected)});
		const EntityID builder = colonist(Task{}, {entityOption(TaskType::Build, site, OptionStatus::Reserved)});
		colonist(Task{}, {entityOption(TaskType::Craft, station, OptionStatus::NoSource)});

		const ColonyGoalWork work(world, claims);
		EXPECT_EQ(listed(work.offeredTo(craft)), (std::vector<EntityID>{crafter})) << "awaiting materials is no offer";
		EXPECT_TRUE(work.offeredTo(fetch).empty());
		EXPECT_EQ(listed(work.offeredTo(build)), (std::vector<EntityID>{builder}));
		EXPECT_TRUE(work.allSourcesClaimed(build));
	}

} // namespace ecs::test
