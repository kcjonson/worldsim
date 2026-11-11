// The trace's text is composed from each option's fixed reason and ids when drawn; these pin that it
// reads as the strings the evaluators used to build per option.

#include "DecisionTrace.h"

#include "../World.h"
#include "Appearance.h"
#include "Colonist.h"
#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"

#include <gtest/gtest.h>

namespace ecs::test {

	class DecisionTraceTextTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto& registry = assets::AssetRegistry::Get();
			assets::AssetDefinition wood;
			wood.defName = "Wood";
			wood.label = "Wood log";
			registry.registerTestDefinition(std::move(wood));
			assets::AssetDefinition tree;
			tree.defName = "Flora_TreeOak";
			tree.label = "Oak Tree";
			registry.registerTestDefinition(std::move(tree));
			assets::AssetDefinition unlabelled;
			unlabelled.defName = "Flora_Bare";
			registry.registerTestDefinition(std::move(unlabelled));
			assets::RecipeDef recipe;
			recipe.defName = "Recipe_Axe";
			recipe.label = "Stone Axe";
			assets::RecipeRegistry::Get().registerTestRecipe(recipe);
			woodId = registry.getDefNameId("Wood");
			treeId = registry.getDefNameId("Flora_TreeOak");
		}

		void TearDown() override {
			assets::RecipeRegistry::Get().clear();
			assets::AssetRegistry::Get().clearDefinitions();
		}

		World	 world;
		uint32_t woodId = 0;
		uint32_t treeId = 0;
	};

	TEST_F(DecisionTraceTextTest, WorkOptionsNameTheirItemsSourcesAndRecipes) {
		EvaluatedOption haul;
		haul.taskType = TaskType::Haul;
		haul.haulItemDefNameId = woodId;
		haul.reason = "Hauling {} to storage";
		EXPECT_EQ(describeOption(world, haul), "Hauling Wood to storage");
		haul.status = OptionStatus::NoRoom;
		haul.reason = "Fetching for crafting ({}: no room to carry it)";
		EXPECT_EQ(describeOption(world, haul), "Fetching for crafting (Wood: no room to carry it)");

		EvaluatedOption cut;
		cut.taskType = TaskType::Harvest;
		cut.targetDefNameId = treeId;
		cut.harvestYieldDefNameId = woodId;
		cut.reason = "Cutting {} for {}";
		EXPECT_EQ(describeOption(world, cut), "Cutting Oak Tree for Wood log");
		cut.status = OptionStatus::Regrowing;
		EXPECT_EQ(describeOption(world, cut), "Cutting Oak Tree for Wood log (regrowing)");
		cut.status = OptionStatus::Available;
		cut.reason = "Clearing {} from build site";
		cut.targetDefNameId = assets::AssetRegistry::Get().getDefNameId("Flora_Bare");
		EXPECT_EQ(describeOption(world, cut), "Clearing Flora_Bare from build site") << "no label: the name";
		EvaluatedOption cutNoRoom;
		cutNoRoom.taskType = TaskType::Harvest;
		cutNoRoom.harvestYieldDefNameId = woodId;
		cutNoRoom.status = OptionStatus::NoRoom;
		cutNoRoom.reason = "Cutting for {1} (no room to carry it)";
		EXPECT_EQ(describeOption(world, cutNoRoom), "Cutting for Wood log (no room to carry it)");

		EvaluatedOption craft;
		craft.taskType = TaskType::Craft;
		craft.craftRecipeDefName = "Recipe_Axe";
		craft.reason = "Crafting {} (awaiting materials)";
		EXPECT_EQ(describeOption(world, craft), "Crafting Stone Axe (awaiting materials)");

		const EntityID crate = world.createEntity();
		world.addComponent<Appearance>(crate, Appearance{"Crate", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		EvaluatedOption place;
		place.taskType = TaskType::PlacePackaged;
		place.placePackagedEntityId = crate;
		place.reason = "Placing {}";
		EXPECT_EQ(describeOption(world, place), "Placing Crate");
	}

	TEST_F(DecisionTraceTextTest, AReservedOptionNamesItsHolder) {
		const EntityID alice = world.createEntity();
		world.addComponent<Colonist>(alice, Colonist{"Alice"});

		EvaluatedOption cut;
		cut.taskType = TaskType::Harvest;
		cut.targetDefNameId = treeId;
		cut.harvestYieldDefNameId = woodId;
		cut.reason = "Cutting {} for {}";
		cut.status = OptionStatus::Reserved;
		cut.reservedBy = alice;
		EXPECT_EQ(describeOption(world, cut), "Cutting Oak Tree for Wood log (reserved by Alice)");

		EvaluatedOption craft;
		craft.taskType = TaskType::Craft;
		craft.craftRecipeDefName = "Recipe_Axe";
		craft.reason = "Crafting {}";
		craft.status = OptionStatus::Reserved;
		craft.reservedBy = alice;
		EXPECT_EQ(describeOption(world, craft), "Crafting Stone Axe (station in use by Alice)");

		EvaluatedOption hunger;
		hunger.taskType = TaskType::FulfillNeed;
		hunger.needType = NeedType::Hunger;
		hunger.needValue = 40.0F;
		hunger.threshold = 50.0F;
		hunger.status = OptionStatus::Reserved;
		hunger.reservedBy = alice;
		hunger.targetPosition = glm::vec2{4.0F, 0.0F};
		hunger.distanceToTarget = 4.0F;
		EXPECT_EQ(describeOption(world, hunger), "Hunger at 40% (reserved by Alice)");

		EvaluatedOption gather = hunger;
		gather.needValue = 100.0F;
		gather.threshold = 0.0F;
		gather.reason = "Food source";
		gather.reservedBy = world.createEntity();
		EXPECT_EQ(describeOption(world, gather), "Food source (reserved by another colonist)") << "a holder with no name";
	}

	TEST_F(DecisionTraceTextTest, NeedOptionsReadTheirLevelAndWhere) {
		EvaluatedOption hunger;
		hunger.taskType = TaskType::FulfillNeed;
		hunger.needType = NeedType::Hunger;
		hunger.needValue = 40.0F;
		hunger.threshold = 50.0F;
		hunger.status = OptionStatus::Available;
		hunger.targetPosition = glm::vec2{12.0F, 0.0F};
		hunger.distanceToTarget = 12.4F;
		hunger.reason = "harvest";
		EXPECT_EQ(describeOption(world, hunger), "Hunger at 40% (12m away) (harvest)");
		hunger.status = OptionStatus::Regrowing;
		EXPECT_EQ(describeOption(world, hunger), "Hunger at 40% (source regrowing)");
		hunger.status = OptionStatus::NoSource;
		hunger.targetPosition.reset();
		EXPECT_EQ(describeOption(world, hunger), "Hunger at 40% (no known source)");
		hunger.needValue = 5.0F;
		hunger.status = OptionStatus::Selected;
		hunger.reason = "from inventory";
		EXPECT_EQ(describeOption(world, hunger), "Hunger at 5% (critical) (from inventory)");

		EvaluatedOption gather = hunger;
		gather.needValue = 100.0F;
		gather.threshold = 0.0F;
		gather.reason = "Gathering food (inventory empty)";
		EXPECT_EQ(describeOption(world, gather), "Gathering food (inventory empty)");
	}

} // namespace ecs::test
