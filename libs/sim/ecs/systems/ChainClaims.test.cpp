// The claims each task chain holds at each stage, recorded by AIDecisionSystem at acceptance and
// rewritten by ActionSystem as the chain advances (docs/technical/task-claims.md, Chain claims). Each
// test drives the two systems one at a time, so it can read the claims between stages; every AI
// update here also runs the development-build validator, which aborts on a violation.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "ChainClaims.h"
#include "CraftingGoalSystem.h"
#include "MovementSystem.h"
#include "PhysicsSystem.h"
#include "StorageGoalSystem.h"
#include "TimeSystem.h"
#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "../WorldSource.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Packaged.h"
#include "../components/ResourceStack.h"
#include "../components/Skills.h"
#include "../components/SourceReachMemo.h"
#include "../components/StorageConfiguration.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"
#include "assets/ActionTypeRegistry.h"
#include "assets/AssetRegistry.h"
#include "assets/PriorityConfig.h"
#include "assets/RecipeRegistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

namespace ecs::test {

	namespace {
		constexpr uint32_t kSeed = 42;

		std::optional<Claim> claimOf(const Task& task, ClaimKind kind) {
			for (const Claim& claim : task.claims) {
				if (claim.kind() == kind) {
					return claim;
				}
			}
			return std::nullopt;
		}
	} // namespace

	class ChainClaimsTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			world = std::make_unique<World>();
			GoalTaskRegistry::Get().clear();
			assets::PriorityConfig::Get().clear();

			auto& registry = assets::AssetRegistry::Get();
			registerItem("Berry", 1, 0.1F, 1, /*edible=*/true);
			registerItem("Stone", 1, 0.5F, /*carryQuantity=*/5);
			registerItem("Wood", 2, 2.5F);
			registerItem("Stick", 1, 0.2F);
			registerHarvestable("BerryBush", "Berry", 2, 3);
			registerHarvestable("StickBush", "Stick", 2, 3);
			assets::AssetDefinition box;
			box.defName = "Box";
			box.label = "Box";
			box.capabilities.storage = assets::StorageCapability{};
			registry.registerTestDefinition(std::move(box));
			assets::AssetDefinition crate;
			crate.defName = "Crate";
			crate.label = "Crate";
			registry.registerTestDefinition(std::move(crate));

			stoneId = registry.getDefNameId("Stone");
			woodId = registry.getDefNameId("Wood");
			stickId = registry.getDefNameId("Stick");

			ai = &world->registerSystem<AIDecisionSystem>(registry, assets::RecipeRegistry::Get(), kSeed);
			actions = &world->registerSystem<ActionSystem>(kSeed);
		}

		void TearDown() override {
			assets::RecipeRegistry::Get().clear();
			assets::ActionTypeRegistry::Get().clear();
			assets::AssetRegistry::Get().clearDefinitions();
			GoalTaskRegistry::Get().clear();
			world.reset();
		}

		void registerItem(const std::string& name, uint8_t hands, float massKg, uint32_t carryQuantity = 1, bool edible = false) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.handsRequired = hands;
			def.category = assets::ItemCategory::RawMaterial;
			def.capabilities.carryable = assets::CarryableCapability{carryQuantity};
			def.itemProperties = assets::ItemProperties{};
			def.itemProperties->stackSize = 40;
			def.itemProperties->massKg = massKg;
			if (edible) {
				def.itemProperties->edible = assets::EdibleCapability{0.3F, assets::CapabilityQuality::Normal, true};
			}
			assets::AssetRegistry::Get().registerTestDefinition(std::move(def));
		}

		void registerHarvestable(const std::string& name, const std::string& yield, uint32_t amountMin, uint32_t amountMax) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.capabilities.harvestable = assets::HarvestableCapability{};
			def.capabilities.harvestable->yieldDefName = yield;
			def.capabilities.harvestable->amountMin = amountMin;
			def.capabilities.harvestable->amountMax = amountMax;
			def.capabilities.harvestable->durability = 1.0F;
			def.capabilities.harvestable->destructive = false;
			assets::AssetRegistry::Get().registerTestDefinition(std::move(def));
		}

		EntityID createColonist(glm::vec2 position) {
			const EntityID entity = world->createEntity();
			world->addComponent<Position>(entity, Position{position});
			world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
			world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
			auto needs = NeedsComponent::createDefault();
			for (auto need : NeedsComponent::kActionableNeeds) {
				needs.get(need).value = 100.0F;
			}
			world->addComponent<NeedsComponent>(entity, needs);
			world->addComponent<Memory>(entity, Memory{});
			world->addComponent<Inventory>(entity, Inventory::createForColonist());
			world->addComponent<Task>(entity, Task{});
			world->addComponent<Action>(entity, Action{});
			world->addComponent<DecisionTrace>(entity, DecisionTrace{});
			world->addComponent<SourceReachMemo>(entity, SourceReachMemo{});
			return entity;
		}

		void remember(EntityID colonist, glm::vec2 position, const std::string& defName, assets::CapabilityType capability) {
			const auto mask = static_cast<uint16_t>(1U << static_cast<size_t>(capability));
			world->getComponent<Memory>(colonist)->rememberWorldEntity(position, assets::AssetRegistry::Get().getDefNameId(defName), mask);
		}

		EntityID makeBox(glm::vec2 position, StoragePriority priority, uint32_t stone = 0) {
			const EntityID box = world->createEntity();
			world->addComponent<Position>(box, Position{position});
			world->addComponent<Appearance>(box, Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			auto inventory = Inventory::createForStorage();
			if (stone > 0) {
				inventory.addItem("Stone", stone);
			}
			world->addComponent<Inventory>(box, std::move(inventory));
			StorageConfiguration config;
			config.addRule(StorageRule{.defName = "Stone", .category = assets::ItemCategory::RawMaterial, .priority = priority});
			world->addComponent<StorageConfiguration>(box, std::move(config));
			return box;
		}

		uint64_t storageGoal(EntityID box, glm::vec2 position) {
			GoalTask goal;
			goal.type = TaskType::Haul;
			goal.owner = GoalOwner::StorageGoalSystem;
			goal.destinationEntity = box;
			goal.destinationPosition = position;
			goal.acceptedDefNameIds = {stoneId};
			return GoalTaskRegistry::Get().createGoal(std::move(goal));
		}

		/// A blueprint wanting `needed` of `item`, and its construction umbrella with the material due.
		struct Site {
			EntityID blueprint;
			uint64_t umbrella;
		};
		Site constructionSite(glm::vec2 position, const std::string& item, uint32_t needed) {
			const EntityID blueprint = world->createEntity();
			world->addComponent<Position>(blueprint, Position{position});
			StructureBlueprint bp;
			bp.phase = StructureBlueprint::BuildPhase::AwaitingMaterials;
			bp.required = {{item, needed}};
			world->addComponent<StructureBlueprint>(blueprint, std::move(bp));

			auto&	 goals = GoalTaskRegistry::Get();
			GoalTask umbrella;
			umbrella.type = TaskType::Build;
			umbrella.owner = GoalOwner::ConstructionGoalSystem;
			umbrella.destinationEntity = blueprint;
			umbrella.destinationPosition = position;
			umbrella.acceptedDefNameIds = {assets::AssetRegistry::Get().getDefNameId(item)};
			umbrella.status = GoalStatus::Blocked;
			return {blueprint, goals.createGoal(std::move(umbrella))};
		}

		/// A crafting station's Craft root, its next job needing `count` of `item`.
		uint64_t craftRoot(EntityID station, glm::vec2 position, const std::string& item, uint32_t count) {
			assets::RecipeDef recipe;
			recipe.defName = "Recipe_" + item;
			recipe.label = recipe.defName;
			recipe.inputs.push_back({item, 0, count});
			assets::RecipeRegistry::Get().registerTestRecipe(recipe);
			world->addComponent<WorkQueue>(station).addJob(recipe.defName, 1);
			world->addComponent<Inventory>(station, Inventory::createForStorage());

			GoalTask craft;
			craft.type = TaskType::Craft;
			craft.owner = GoalOwner::CraftingGoalSystem;
			craft.destinationEntity = station;
			craft.destinationPosition = position;
			craft.status = GoalStatus::Blocked;
			return GoalTaskRegistry::Get().createGoal(std::move(craft));
		}

		uint64_t childGoal(TaskType type, uint64_t root, EntityID destination, glm::vec2 position, uint32_t item) {
			GoalTask child;
			child.type = type;
			child.owner = GoalOwner::CraftingGoalSystem;
			child.destinationEntity = destination;
			child.destinationPosition = position;
			child.acceptedDefNameIds = {item};
			child.yieldDefNameId = type == TaskType::Harvest ? item : 0;
			child.parentGoalId = root;
			return GoalTaskRegistry::Get().createGoal(std::move(child));
		}

		/// One AI update, forcing `colonist` to decide now.
		void decide(EntityID colonist) {
			getTask(colonist)->timeSinceEvaluation = 1.0F;
			ai->update(0.016F);
		}

		/// Stand the colonist at `where`, arrived, and run actions until its task leaves the Arrived
		/// state or clears.
		void act(EntityID colonist, glm::vec2 where) {
			world->getComponent<Position>(colonist)->value = where;
			getTask(colonist)->state = TaskState::Arrived;
			for (int step = 0; step < 50 && getTask(colonist)->state == TaskState::Arrived && getTask(colonist)->isActive(); ++step) {
				actions->update(0.25F);
			}
		}

		/// One AI update in which every colonist named decides, in view order.
		void decideAll(std::initializer_list<EntityID> colonists) {
			for (EntityID colonist : colonists) {
				getTask(colonist)->timeSinceEvaluation = 1.0F;
			}
			ai->update(0.016F);
		}

		/// The first option in the colonist's trace of this type and status, or nullptr.
		const EvaluatedOption* optionOf(EntityID colonist, TaskType type, OptionStatus status) const {
			for (const EvaluatedOption& option : world->getComponent<DecisionTrace>(colonist)->options) {
				if (option.taskType == type && option.status == status) {
					return &option;
				}
			}
			return nullptr;
		}

		size_t indexedClaimsOf(EntityID colonist) const {
			size_t count = 0;
			ai->claimIndex().forEachClaim([&](const Claim&, const ClaimHolder& holder) { count += holder.colonist == colonist ? 1 : 0; });
			return count;
		}

		Task* getTask(EntityID entity) { return world->getComponent<Task>(entity); }

		std::unique_ptr<World> world;
		AIDecisionSystem*	   ai = nullptr;
		ActionSystem*		   actions = nullptr;
		uint32_t			   stoneId = 0;
		uint32_t			   woodId = 0;
		uint32_t			   stickId = 0;
	};

	TEST_F(ChainClaimsTest, PileHaulClaimsTheSourceThenCarriesWhatItLifted) {
		const glm::vec2 boxPos{0.0F, 10.0F};
		const glm::vec2 pilePos{5.0F, 0.0F};
		const EntityID	box = makeBox(boxPos, StoragePriority::Medium);
		const uint64_t	goal = storageGoal(box, boxPos);
		const EntityID	pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, stoneId, pilePos, 8);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, pilePos, "Stone", assets::CapabilityType::Carryable);

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		ASSERT_NE(task.instanceId, 0U);
		ASSERT_EQ(task.claims.size(), 3);
		const uint32_t q = task.haulQuantity;
		const auto	   source = claimOf(task, ClaimKind::SourceQuantity);
		ASSERT_TRUE(source.has_value());
		EXPECT_EQ(source->targetKind(), TargetKind::WorldKey);
		EXPECT_EQ(source->target(), hashWorldEntity(pilePos, stoneId));
		EXPECT_EQ(source->quantity(), q);
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->target(), static_cast<uint64_t>(box));
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->target(), goal);
		EXPECT_EQ(ai->claimIndex().sourceClaimed({.kind = TargetKind::WorldKey, .key = hashWorldEntity(pilePos, stoneId)}, stoneId, {}), q);

		// The pickup lifts what was claimed, here the whole pile: the source claim is spent and the rest
		// follows the goods.
		ASSERT_EQ(q, 8U) << "the live pile caps the fetch, under the box's room and one trip";
		act(colonist, pilePos);
		ASSERT_EQ(task.type, TaskType::Haul);
		ASSERT_EQ(ecs::availableQuantity(*world->getComponent<Inventory>(colonist), "Stone"), 8U);
		EXPECT_FALSE(claimOf(task, ClaimKind::SourceQuantity).has_value());
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->quantity(), 8U);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), 8U);

		act(colonist, boxPos);
		EXPECT_FALSE(task.isActive());
		EXPECT_TRUE(task.claims.empty()) << "the deposit ends the task and its claims";
		// The next haul from what is left of the pile is a new instance with claims of its own.
		const uint64_t depositedInstance = task.instanceId;
		decide(colonist);
		EXPECT_NE(task.instanceId, depositedInstance);
		const auto next = claimOf(task, ClaimKind::InFlight);
		EXPECT_EQ(ai->claimIndex().inFlight(goal, stoneId, {}), next ? next->quantity() : 0U);
	}

	TEST_F(ChainClaimsTest, BoxToBoxPullClaimsTheSourceBox) {
		const glm::vec2 lowPos{4.0F, 0.0F};
		const glm::vec2 highPos{0.0F, 8.0F};
		const EntityID	low = makeBox(lowPos, StoragePriority::Low, 6);
		const EntityID	high = makeBox(highPos, StoragePriority::High);
		const uint64_t	goal = storageGoal(high, highPos);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		remember(colonist, lowPos, "Box", assets::CapabilityType::Storage);
		remember(colonist, highPos, "Box", assets::CapabilityType::Storage);

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.haulSourceStorageId, static_cast<uint64_t>(low));
		const auto source = claimOf(task, ClaimKind::SourceQuantity);
		ASSERT_TRUE(source.has_value());
		EXPECT_EQ(source->targetKind(), TargetKind::Entity);
		EXPECT_EQ(source->target(), static_cast<uint64_t>(low));
		EXPECT_EQ(source->quantity(), 6U);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->target(), goal);

		act(colonist, lowPos);
		EXPECT_FALSE(claimOf(task, ClaimKind::SourceQuantity).has_value());
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->quantity(), 6U);
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->target(), static_cast<uint64_t>(high));
	}

	// A craft fetch's in-flight names the Craft root, not the Haul child it was picked through.
	TEST_F(ChainClaimsTest, CraftFetchCountsTowardTheCraftRoot) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stone", 2);
		childGoal(TaskType::Haul, root, station, stationPos, stoneId);
		const EntityID pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, stoneId, pilePos, 8);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, pilePos, "Stone", assets::CapabilityType::Carryable);

		decide(colonist);
		const Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		EXPECT_EQ(claimOf(task, ClaimKind::SourceQuantity)->quantity(), 2U);
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->target(), static_cast<uint64_t>(station));
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->target(), root);
		EXPECT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 2U);
	}

	// A cut claims its source and expects its largest yield toward the root. When it lands the task
	// becomes its own deposit leg: the Use drops, In-flight and Inbound follow what was cut, and the
	// deposit ends them with the task.
	TEST_F(ChainClaimsTest, HarvestForAGoalClaimsTheSourceAndItsExpectedYield) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stick", 5);
		childGoal(TaskType::Harvest, root, station, stationPos, stickId);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, bushPos, "StickBush", assets::CapabilityType::Harvestable);

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Harvest);
		const auto use = claimOf(task, ClaimKind::Use);
		ASSERT_TRUE(use.has_value());
		EXPECT_EQ(use->targetKind(), TargetKind::WorldKey);
		EXPECT_EQ(use->target(), task.harvestTargetEntityId);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->target(), root);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), 3U) << "amountMax, under the need and the carry";
		EXPECT_EQ(ai->claimIndex().useHolder({.kind = TargetKind::WorldKey, .key = task.harvestTargetEntityId}, {})->colonist, colonist);

		act(colonist, bushPos);
		const uint32_t cut = ecs::availableQuantity(*world->getComponent<Inventory>(colonist), "Stick");
		ASSERT_GT(cut, 0U);
		ASSERT_EQ(task.type, TaskType::Haul) << "the cut carries itself in";
		EXPECT_FALSE(claimOf(task, ClaimKind::Use).has_value());
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->target(), root);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), cut);
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->target(), static_cast<uint64_t>(station));
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->quantity(), cut);

		act(colonist, stationPos);
		EXPECT_FALSE(task.isActive());
		EXPECT_TRUE(task.claims.empty());
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Stick"), cut);
	}

	TEST_F(ChainClaimsTest, BuildSiteClearClaimsOnlyItsSource) {
		const glm::vec2 sitePos{4.0F, 4.0F};
		const glm::vec2 bushPos{4.5F, 4.5F};
		const Site		site = constructionSite(sitePos, "Wood", 4);
		GoalTask		clear;
		clear.type = TaskType::Harvest;
		clear.owner = GoalOwner::ConstructionGoalSystem;
		clear.destinationEntity = site.blueprint;
		clear.destinationPosition = sitePos;
		clear.clearFootprint = FootprintBounds{.min = {3.0F, 3.0F}, .max = {6.0F, 6.0F}};
		clear.parentGoalId = site.umbrella;
		GoalTaskRegistry::Get().createGoal(std::move(clear));
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, bushPos, "StickBush", assets::CapabilityType::Harvestable);

		decide(colonist);
		const Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Harvest);
		ASSERT_EQ(task.claims.size(), 1);
		EXPECT_EQ(task.claims[0].kind(), ClaimKind::Use);
	}

	// Construction delivery carries in goods already held: inbound at the blueprint and in-flight
	// toward the umbrella, with no source.
	TEST_F(ChainClaimsTest, ConstructionDeliveryClaimsTheSiteAndTheUmbrella) {
		const glm::vec2 sitePos{0.0F, 6.0F};
		const Site		site = constructionSite(sitePos, "Stone", 10);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		world->getComponent<Inventory>(colonist)->addItem("Stone", 4);

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		ASSERT_TRUE(task.haulFromInventory);
		ASSERT_EQ(task.claims.size(), 2);
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->target(), static_cast<uint64_t>(site.blueprint));
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->quantity(), 4U);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->target(), site.umbrella);

		act(colonist, sitePos);
		EXPECT_FALSE(task.isActive());
		EXPECT_TRUE(task.claims.empty());
		EXPECT_EQ(world->getComponent<StructureBlueprint>(site.blueprint)->remaining("Stone"), 6U);
	}

	TEST_F(ChainClaimsTest, HungerClaimsTheBushItWalksToAndThirstClaimsNothing) {
		const glm::vec2 bushPos{6.0F, 0.0F};
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		remember(colonist, bushPos, "BerryBush", assets::CapabilityType::Harvestable);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 30.0F;

		decide(colonist);
		const Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::FulfillNeed);
		ASSERT_EQ(task.claims.size(), 1);
		EXPECT_EQ(task.claims[0].kind(), ClaimKind::Use);
		EXPECT_EQ(task.claims[0].target(), hashWorldEntity(bushPos, assets::AssetRegistry::Get().getDefNameId("BerryBush")));

		const EntityID thirsty = createColonist({0.0F, 20.0F});
		remember(thirsty, {0.0F, 25.0F}, "Crate", assets::CapabilityType::Drinkable);
		world->getComponent<NeedsComponent>(thirsty)->get(NeedType::Thirst).value = 30.0F;
		decide(thirsty);
		ASSERT_EQ(getTask(thirsty)->needToFulfill, NeedType::Thirst);
		EXPECT_TRUE(getTask(thirsty)->claims.empty()) << "water is unbounded";
	}

	TEST_F(ChainClaimsTest, PlacingAPackagedItemUsesItUntilPickedUp) {
		const glm::vec2 itemPos{3.0F, 0.0F};
		const EntityID	packaged = world->createEntity();
		world->addComponent<Position>(packaged, Position{itemPos});
		world->addComponent<Appearance>(packaged, Appearance{"Crate", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world->addComponent<Packaged>(packaged, Packaged{.targetPosition = glm::vec2{8.0F, 0.0F}});
		const EntityID colonist = createColonist({0.0F, 0.0F});

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::PlacePackaged);
		ASSERT_EQ(task.claims.size(), 1);
		EXPECT_EQ(task.claims[0].targetKind(), TargetKind::Entity);
		EXPECT_EQ(task.claims[0].target(), static_cast<uint64_t>(packaged));

		act(colonist, itemPos);
		ASSERT_EQ(task.chainStep, 1);
		EXPECT_TRUE(task.claims.empty()) << "a carried item is possessed";
	}

	// A colonist carrying one packaged item sees another only as an item to go and pick up: held by
	// whoever is on its way to it, never as one it is already delivering.
	TEST_F(ChainClaimsTest, CarryingOnePackagedItemDoesNotMakeAnotherOneDelivering) {
		const auto makePackaged = [&](glm::vec2 at, glm::vec2 target) {
			const EntityID item = world->createEntity();
			world->addComponent<Position>(item, Position{at});
			world->addComponent<Appearance>(item, Appearance{"Crate", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			world->addComponent<Packaged>(item, Packaged{.targetPosition = target});
			return item;
		};
		const EntityID carried = makePackaged({0.0F, 0.0F}, {20.0F, 0.0F});
		const EntityID other = makePackaged({3.0F, 0.0F}, {8.0F, 0.0F});
		const EntityID holder = createColonist({4.0F, 0.0F});
		decide(holder);
		ASSERT_EQ(getTask(holder)->placePackagedEntityId, static_cast<uint64_t>(other));

		const EntityID carrier = createColonist({0.0F, 0.0F});
		world->getComponent<Packaged>(carried)->beingCarried = true;
		world->getComponent<Inventory>(carrier)->carryingPackagedEntity = static_cast<uint64_t>(carried);
		decide(carrier);
		bool sawOther = false;
		for (const EvaluatedOption& option : world->getComponent<DecisionTrace>(carrier)->options) {
			if (option.taskType == TaskType::PlacePackaged && option.placePackagedEntityId == static_cast<uint64_t>(other)) {
				sawOther = true;
				EXPECT_EQ(option.status, OptionStatus::Reserved);
				EXPECT_EQ(option.reservedBy, holder);
				EXPECT_EQ(option.targetPosition, glm::vec2(3.0F, 0.0F)) << "it would go to pick it up, not deliver it";
			}
		}
		EXPECT_TRUE(sawOther);
	}

	// The switch path rewrites a live instance into a smaller record and accepts the need in one
	// update; the record survives task.clear() and the validator holds through it.
	TEST_F(ChainClaimsTest, NeedKeepsAStowedHaulAsItsSuspendedRecordAndCarryInAdoptsIt) {
		const glm::vec2 sitePos{0.0F, 6.0F};
		const Site		site = constructionSite(sitePos, "Stone", 10);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		auto&			inventory = *world->getComponent<Inventory>(colonist);
		inventory.addItem("Stone", 4);
		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_TRUE(task.haulFromInventory);
		const uint64_t haulInstance = task.instanceId;

		// Two of the four are gone (used elsewhere); the record keeps only what is still held.
		inventory.removeItem("Stone", 2);
		inventory.addItem("Berry", 1);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::FulfillNeed);
		ASSERT_TRUE(task.suspended.has_value()) << "the record survives the switch's task.clear()";
		EXPECT_EQ(task.suspended->instanceId, haulInstance);
		EXPECT_EQ(task.suspended->rootGoalId, site.umbrella);
		EXPECT_EQ(task.suspended->claims.size(), 2);
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, stoneId, {}), 2U);
		EXPECT_EQ(ai->claimIndex().inboundItems(site.blueprint, stoneId, {}), 2U);
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, stoneId, ClaimExclusion::of(task)), 0U);

		// Another need keeps it.
		task.clear();
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::FulfillNeed);
		ASSERT_TRUE(task.suspended.has_value());

		// Fed, the colonist carries the goods in again under the same instance, counted once.
		inventory.removeItem("Berry", 1);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 100.0F;
		task.clear();
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		EXPECT_FALSE(task.suspended.has_value());
		EXPECT_EQ(task.instanceId, haulInstance);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), 2U);
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, stoneId, {}), 2U) << "adopted, not appended twice";
		EXPECT_EQ(ai->claimIndex().inboundItems(site.blueprint, stoneId, {}), 2U);
	}

	// A record claims only what the colonist still holds, and goes when its root goal does.
	TEST_F(ChainClaimsTest, ASuspendedRecordFollowsTheGoodsAndItsRoot) {
		const glm::vec2 sitePos{0.0F, 6.0F};
		const Site		site = constructionSite(sitePos, "Stone", 10);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		auto&			inventory = *world->getComponent<Inventory>(colonist);
		inventory.addItem("Stone", 4);
		inventory.addItem("Berry", 1);
		decide(colonist);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_TRUE(task.suspended.has_value());
		ASSERT_EQ(ai->claimIndex().inFlight(site.umbrella, stoneId, {}), 4U);

		// Three of the stones are used while the colonist sees to its need.
		inventory.removeItem("Stone", 3);
		decide(colonist);
		ASSERT_TRUE(task.suspended.has_value());
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, stoneId, {}), 1U) << "others see only the stone still held";
		EXPECT_EQ(ai->claimIndex().inboundItems(site.blueprint, stoneId, {}), 1U);

		GoalTaskRegistry::Get().removeGoal(site.umbrella);
		decide(colonist);
		EXPECT_FALSE(task.suspended.has_value()) << "a record toward a goal that is gone is dropped";
		EXPECT_EQ(indexedClaimsOf(colonist), task.claims.size());
	}

	// A carry-in adopts a record only when it delivers where the record was delivering.
	TEST_F(ChainClaimsTest, AdoptionMatchesTheRecordsDestination) {
		const Site site = constructionSite({0.0F, 6.0F}, "Stone", 10);
		Task	   task;
		task.type = TaskType::Haul;
		task.haulFromInventory = true;
		task.haulItemDefName = "Stone";
		task.haulGoalId = site.umbrella;
		task.suspended = SuspendedWork{.instanceId = 3, .claimSeq = 3, .rootGoalId = site.umbrella, .itemDefNameId = stoneId};
		ASSERT_TRUE(task.suspended->claims.push(Claim::inbound(static_cast<uint64_t>(site.blueprint), stoneId, 2)));
		ASSERT_TRUE(task.suspended->claims.push(Claim::inFlight(site.umbrella, stoneId, 2)));

		task.haulTargetStorageId = static_cast<uint64_t>(site.blueprint) + 1;
		EXPECT_FALSE(adoptsSuspended(assets::AssetRegistry::Get(), task));
		task.haulTargetStorageId = static_cast<uint64_t>(site.blueprint);
		EXPECT_TRUE(adoptsSuspended(assets::AssetRegistry::Get(), task));
	}

	TEST_F(ChainClaimsTest, AnySelectionButANeedOrTheCarryInDiscardsTheRecord) {
		const glm::vec2 sitePos{0.0F, 6.0F};
		const Site		site = constructionSite(sitePos, "Stone", 10);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		auto&			inventory = *world->getComponent<Inventory>(colonist);
		inventory.addItem("Stone", 4);
		inventory.addItem("Berry", 1);
		decide(colonist);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_TRUE(task.suspended.has_value());

		// The site no longer wants the goods: the next pick is Wander, and the record goes.
		GoalTaskRegistry::Get().updateGoal(site.umbrella, [](GoalTask& goal) { goal.acceptedDefNameIds.clear(); });
		inventory.removeItem("Berry", 1);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 100.0F;
		task.clear();
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::Wander);
		EXPECT_FALSE(task.suspended.has_value());
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, stoneId, {}), 0U) << "gone from the index in the same update";
		EXPECT_EQ(indexedClaimsOf(colonist), 0U);
	}

	TEST_F(ChainClaimsTest, ADroppedArmfulLeavesNoRecordAndNoClaims) {
		assets::ActionTypeDef eat;
		eat.defName = "Eat";
		eat.needsHands = true;
		assets::ActionTypeRegistry::Get().registerTestAction(std::move(eat));
		int drops = 0;
		ai->setDropResourceCallback([&](const std::string&, float, float, uint32_t) { ++drops; });

		const glm::vec2 sitePos{0.0F, 6.0F};
		const Site		site = constructionSite(sitePos, "Wood", 10);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		auto&			inventory = *world->getComponent<Inventory>(colonist);
		inventory.leftHand = ItemStack{"Wood", 4};
		inventory.rightHand = ItemStack{"Wood", 4};
		inventory.addItem("Berry", 1);
		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);

		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::FulfillNeed);
		EXPECT_EQ(drops, 1);
		EXPECT_FALSE(task.suspended.has_value());
		EXPECT_TRUE(task.claims.empty());
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, woodId, {}), 0U);
	}

	TEST_F(ChainClaimsTest, PlayerControlAndDestructionLeaveNoClaims) {
		const glm::vec2 sitePos{0.0F, 6.0F};
		const Site		site = constructionSite(sitePos, "Stone", 20);
		const EntityID	controlled = createColonist({0.0F, 0.0F});
		const EntityID	destroyed = createColonist({1.0F, 0.0F});
		const EntityID	bystander = createColonist({2.0F, 0.0F});
		for (EntityID colonist : {controlled, destroyed}) {
			world->getComponent<Inventory>(colonist)->addItem("Stone", 3);
		}
		decide(controlled);
		ASSERT_EQ(ai->claimIndex().inFlight(site.umbrella, stoneId, {}), 6U);

		// A stowed haul held as a record goes too.
		world->getComponent<Inventory>(controlled)->addItem("Berry", 1);
		world->getComponent<NeedsComponent>(controlled)->get(NeedType::Hunger).value = 5.0F;
		decide(controlled);
		ASSERT_TRUE(getTask(controlled)->suspended.has_value());

		ai->enterControl(controlled);
		EXPECT_FALSE(getTask(controlled)->isActive());
		EXPECT_FALSE(getTask(controlled)->suspended.has_value());
		EXPECT_EQ(indexedClaimsOf(controlled), 0U);

		world->destroyEntity(destroyed);
		decide(bystander);
		EXPECT_EQ(indexedClaimsOf(controlled), 0U);
		EXPECT_EQ(indexedClaimsOf(destroyed), 0U);
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, stoneId, {}), 0U);
	}

	// =============================================================================
	// Provisions: a colonist's own food is never delivered to a root, by a carry-in or along with a
	// cut. Food it was sent to fetch or cut for a root is that root's cargo, an interruption included.
	// =============================================================================

	// A hungry colonist's only meal is wanted by a craft and by a box: neither is offered it.
	TEST_F(ChainClaimsTest, AColonistsOwnFoodIsNeverDelivered) {
		const uint32_t	berryId = assets::AssetRegistry::Get().getDefNameId("Berry");
		const glm::vec2 stationPos{0.0F, 6.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		craftRoot(station, stationPos, "Berry", 3);
		const glm::vec2 boxPos{0.0F, -6.0F};
		const EntityID	box = world->createEntity();
		world->addComponent<Position>(box, Position{boxPos});
		world->addComponent<Inventory>(box, Inventory::createForStorage());
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Berry", .category = assets::ItemCategory::RawMaterial});
		world->addComponent<StorageConfiguration>(box, std::move(config));
		GoalTask umbrella;
		umbrella.type = TaskType::Haul;
		umbrella.owner = GoalOwner::StorageGoalSystem;
		umbrella.destinationEntity = box;
		umbrella.destinationPosition = boxPos;
		umbrella.acceptedDefNameIds = {berryId};
		GoalTaskRegistry::Get().createGoal(std::move(umbrella));

		const EntityID colonist = createColonist({0.0F, 0.0F});
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 60.0F;
		decide(colonist);
		for (const EvaluatedOption& option : world->getComponent<DecisionTrace>(colonist)->options) {
			EXPECT_FALSE(option.taskType == TaskType::Haul && option.haulFromInventory) << "its berry was offered to " << option.haulGoalId;
		}
		EXPECT_EQ(world->getComponent<Inventory>(colonist)->getQuantity("Berry"), 1U);
	}

	class ProvisionsTest : public ChainClaimsTest {
	  protected:
		/// A box stocking berries toward `minimum` by cutting: its umbrella and the stocking Harvest child.
		struct BerryBox {
			EntityID box;
			uint64_t umbrella;
		};

		void SetUp() override {
			ChainClaimsTest::SetUp();
			registerHarvestable("FullBerryBush", "Berry", 3, 3);
			berryId = assets::AssetRegistry::Get().getDefNameId("Berry");
		}

		BerryBox berryBox(glm::vec2 position, uint32_t minimum) {
			const EntityID box = world->createEntity();
			world->addComponent<Position>(box, Position{position});
			world->addComponent<Inventory>(box, Inventory::createForStorage());
			StorageConfiguration config;
			config.addRule(StorageRule{.defName = "Berry", .category = assets::ItemCategory::RawMaterial, .minAmount = minimum});
			world->addComponent<StorageConfiguration>(box, std::move(config));
			auto&	 goals = GoalTaskRegistry::Get();
			GoalTask umbrella;
			umbrella.type = TaskType::Haul;
			umbrella.owner = GoalOwner::StorageGoalSystem;
			umbrella.destinationEntity = box;
			umbrella.destinationPosition = position;
			umbrella.acceptedDefNameIds = {berryId};
			const uint64_t umbrellaId = goals.createGoal(std::move(umbrella));
			GoalTask harvest;
			harvest.type = TaskType::Harvest;
			harvest.owner = GoalOwner::StorageGoalSystem;
			harvest.destinationEntity = box;
			harvest.destinationPosition = position;
			harvest.acceptedDefNameIds = {berryId};
			harvest.yieldDefNameId = berryId;
			harvest.parentGoalId = umbrellaId;
			goals.createGoal(std::move(harvest));
			return {box, umbrellaId};
		}

		uint32_t berryId = 0;
	};

	// A colonist with two berries of its own cuts three for a box: the three go in, its two stay.
	TEST_F(ProvisionsTest, ACutCarriesInOnlyWhatItLanded) {
		const glm::vec2 boxPos{0.0F, 6.0F};
		const BerryBox	stock = berryBox(boxPos, 10);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		world->getComponent<Inventory>(colonist)->addItem("Berry", 2);
		remember(colonist, {3.0F, 0.0F}, "FullBerryBush", assets::CapabilityType::Harvestable);

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Harvest);
		EXPECT_FALSE(claimOf(task, ClaimKind::Inbound).has_value()) << "its own berries are no cut's cargo";
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), 3U);
		act(colonist, task.targetPosition);
		ASSERT_EQ(task.type, TaskType::Haul);
		EXPECT_EQ(task.haulQuantity, 3U);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), 3U);

		act(colonist, boxPos);
		EXPECT_EQ(world->getComponent<Inventory>(stock.box)->getQuantity("Berry"), 3U);
		EXPECT_EQ(world->getComponent<Inventory>(colonist)->getQuantity("Berry"), 2U) << "its own two stay with it";
	}

	// Berries cut for a box are that box's cargo: a need that interrupts their delivery keeps them as
	// the suspended record, and the carry-in that follows takes the same instance and delivers them.
	TEST_F(ProvisionsTest, FoodCutForABoxIsResumedAfterANeed) {
		const glm::vec2 boxPos{0.0F, 6.0F};
		const BerryBox	stock = berryBox(boxPos, 3);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		remember(colonist, {3.0F, 0.0F}, "FullBerryBush", assets::CapabilityType::Harvestable);

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Harvest);
		act(colonist, task.targetPosition);
		ASSERT_EQ(task.type, TaskType::Haul);
		const uint64_t delivery = task.instanceId;

		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Bladder).value = 5.0F;
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::FulfillNeed);
		ASSERT_TRUE(task.suspended.has_value());
		EXPECT_EQ(ai->claimIndex().inFlight(stock.umbrella, berryId, {}), 3U) << "the berries stay the box's while it sees to the need (sleep would release them)";

		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Bladder).value = 100.0F;
		task.clear();
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		EXPECT_TRUE(task.haulFromInventory);
		EXPECT_EQ(task.instanceId, delivery) << "the carry-in adopted the interrupted delivery";
		EXPECT_EQ(task.haulQuantity, 3U);
		act(colonist, boxPos);
		EXPECT_EQ(world->getComponent<Inventory>(stock.box)->getQuantity("Berry"), 3U);
		EXPECT_EQ(world->getComponent<Inventory>(colonist)->getQuantity("Berry"), 0U);
	}

	// Berries fetched from a pile for a craft are its cargo; the colonist's own meal is not lifted with them.
	TEST_F(ProvisionsTest, FoodFetchedForACraftIsDeliveredAndTheMealKept) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Berry", 3);
		childGoal(TaskType::Haul, root, station, stationPos, berryId);
		const EntityID pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, berryId, pilePos, 5);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		remember(colonist, pilePos, "Berry", assets::CapabilityType::Carryable);

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		ASSERT_FALSE(task.haulFromInventory) << "its own berry is not the craft's";
		EXPECT_EQ(task.haulQuantity, 3U);
		act(colonist, pilePos);
		ASSERT_EQ(task.type, TaskType::Haul);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), 3U);
		act(colonist, stationPos);
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Berry"), 3U);
		EXPECT_EQ(world->getComponent<Inventory>(colonist)->getQuantity("Berry"), 1U) << "its meal stays with it";
	}

	// A colonist holding a full armful of wood, standing at a ready tree, skilled at cutting, with a
	// site that wants far more: the cut would land nothing, so it carries in what it holds instead of
	// cutting in place forever. Real positions and skill scoring, no arrival shortcut.
	TEST_F(ChainClaimsTest, AColonistWithNoRoomDeliversInsteadOfCutting) {
		registerHarvestable("WoodTree", "Wood", 2, 3);
		const glm::vec2 sitePos{0.0F, 10.0F};
		const glm::vec2 treePos{0.5F, 0.0F};
		const Site		site = constructionSite(sitePos, "Wood", 60);
		GoalTask		cut;
		cut.type = TaskType::Harvest;
		cut.owner = GoalOwner::ConstructionGoalSystem;
		cut.destinationEntity = site.blueprint;
		cut.destinationPosition = sitePos;
		cut.yieldDefNameId = woodId;
		cut.parentGoalId = site.umbrella;
		GoalTaskRegistry::Get().createGoal(std::move(cut));

		const EntityID colonist = createColonist({0.0F, 0.0F});
		world->addComponent<Skills>(colonist, Skills{}).setLevel("Farming", 10.0F);
		remember(colonist, treePos, "WoodTree", assets::CapabilityType::Harvestable);
		auto&		   inventory = *world->getComponent<Inventory>(colonist);
		const uint32_t armful = ecs::cargoUnitsPerTrip(assets::AssetRegistry::Get(), "Wood", inventory.carryCapacityKg);
		inventory.leftHand = ItemStack{"Wood", armful};
		inventory.rightHand = ItemStack{"Wood", armful};
		ASSERT_EQ(ecs::pickupUnitsThatFit(inventory, assets::AssetRegistry::Get(), "Wood", ecs::carriedCargoMassKg(inventory, assets::AssetRegistry::Get())), 0U);

		for (int decision = 0; decision < 3; ++decision) {
			decide(colonist);
			const Task& task = *getTask(colonist);
			ASSERT_EQ(task.type, TaskType::Haul) << "decision " << decision;
			EXPECT_TRUE(task.haulFromInventory);
			EXPECT_EQ(task.haulTargetStorageId, static_cast<uint64_t>(site.blueprint));
		}
		const auto& options = world->getComponent<DecisionTrace>(colonist)->options;
		EXPECT_TRUE(std::any_of(options.begin(), options.end(), [](const EvaluatedOption& option) {
			return option.taskType == TaskType::Harvest && option.status == OptionStatus::NoRoom;
		})) << "the cut shows why it is not taken";
	}

	// =============================================================================
	// Whole flows: the goal systems, the AI, and the actions run together. Movement is skipped: a
	// colonist arrives the moment it heads somewhere. Each asserts outcomes, never which colonist won.
	// =============================================================================

	class ChainFlowTest : public ChainClaimsTest {
	  protected:
		void SetUp() override {
			ChainClaimsTest::SetUp();
			world->registerSystem<StorageGoalSystem>();
			world->registerSystem<CraftingGoalSystem>();
		}

		/// Run frames, arriving each colonist at its task target at once, until `done` holds.
		template <typename Done> bool runUntil(Done done, int maxFrames = 6000) {
			for (int frame = 0; frame < maxFrames; ++frame) {
				world->update(0.016F);
				for (auto [entity, task, position] : world->view<Task, Position>()) {
					(void)entity;
					if (task.state == TaskState::Moving) {
						position.value = task.targetPosition;
						task.state = TaskState::Arrived;
					}
				}
				if (done()) {
					return true;
				}
			}
			return false;
		}

		EntityID makeStation(glm::vec2 position, const std::vector<assets::RecipeInput>& inputs, uint32_t units) {
			assets::RecipeDef recipe;
			recipe.defName = "Recipe_Flow";
			recipe.label = recipe.defName;
			recipe.workAmount = 10.0F;
			recipe.inputs = inputs;
			recipe.outputs.push_back({"Berry", 0, 1});
			assets::RecipeRegistry::Get().registerTestRecipe(recipe);
			const EntityID station = world->createEntity();
			world->addComponent<Position>(station, Position{position});
			world->addComponent<WorkQueue>(station).addJob(recipe.defName, units);
			world->addComponent<Inventory>(station, Inventory::createForStorage());
			return station;
		}

		EntityID makePile(glm::vec2 position, uint32_t item, uint32_t quantity) {
			const EntityID pile = world->createEntity();
			world->addComponent<Position>(pile, Position{position});
			makeLoosePile(*world, pile, item, position, quantity);
			return pile;
		}

		uint32_t carried(EntityID colonist, const char* item) {
			return ecs::availableQuantity(*world->getComponent<Inventory>(colonist), item);
		}
	};

	// A two-unit craft provisioned from a fetched input (stone from a pile) and a harvested one (sticks
	// cut from a bush) runs to 2/2: each unit's need returns when the previous one consumes the store
	// (the multi-unit stall), and the fetches never take more stone than the units need (over-fetching).
	TEST_F(ChainFlowTest, MultiUnitCraftWithFetchedAndHarvestedInputsCompletes) {
		const EntityID station = makeStation({0.0F, 6.0F}, {{"Stone", 0, 2}, {"Stick", 0, 2}}, 2);
		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	pile = makePile(pilePos, stoneId, 10);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		remember(colonist, pilePos, "Stone", assets::CapabilityType::Carryable);
		remember(colonist, {-3.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);

		auto& queue = *world->getComponent<WorkQueue>(station);
		ASSERT_TRUE(runUntil([&] { return queue.getNextJob() == nullptr; })) << "both units crafted, no stall";
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 6U) << "exactly the four stones the two units used";
		EXPECT_EQ(carried(colonist, "Stone"), 0U);
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Stone"), 0U);
	}

	// A craft input with a known bush gets a Harvest child; when the bush is regrowing and a pile of the
	// input becomes known (fell overflow, a dropped load), the input offers the fetch as well, and the
	// colonist fetches instead of idling with a need and a known source.
	TEST_F(ChainFlowTest, AnInputOffersBothItsBushAndAKnownPile) {
		world->registerSystem<TimeSystem>();
		ai->setSourceRegrowthCallback([](uint32_t, glm::vec2, double) { return std::optional<double>{1.0e9}; });
		const EntityID station = makeStation({0.0F, 6.0F}, {{"Stick", 0, 2}}, 1);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, {-3.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);

		auto& goals = GoalTaskRegistry::Get();
		ASSERT_TRUE(runUntil([&] { return goals.goalCount(TaskType::Harvest) == 1; }, 200)) << "the bush opens a Harvest";
		runUntil([] { return false; }, 60);
		EXPECT_NE(getTask(colonist)->type, TaskType::Harvest) << "the bush is regrowing";
		EXPECT_EQ(goals.goalCount(TaskType::Haul), 0U);

		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	pile = makePile(pilePos, stickId, 5);
		remember(colonist, pilePos, "Stick", assets::CapabilityType::Carryable);
		ASSERT_TRUE(runUntil([&] { return goals.goalCount(TaskType::Haul) == 1; }, 200)) << "the pile opens a fetch beside the Harvest";
		EXPECT_EQ(goals.goalCount(TaskType::Harvest), 1U) << "both ways stay open";

		ASSERT_TRUE(runUntil([&] { return world->getComponent<WorkQueue>(station)->getNextJob() == nullptr; })) << "the craft is provisioned from the pile";
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 3U);
	}

	// Stocking cuts toward the box's minimum; known loose stock then fills it to its maximum and never past.
	TEST_F(ChainFlowTest, StockingFillsToTheMinimumAndHaulingNeverPassesTheMaximum) {
		const glm::vec2 boxPos{0.0F, 8.0F};
		const EntityID	box = world->createEntity();
		world->addComponent<Position>(box, Position{boxPos});
		world->addComponent<Appearance>(box, Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world->addComponent<Inventory>(box, Inventory::createForStorage());
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Stick", .category = assets::ItemCategory::RawMaterial, .minAmount = 4, .maxAmount = 7});
		world->addComponent<StorageConfiguration>(box, std::move(config));
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, {-3.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);
		const auto stored = [&] { return world->getComponent<Inventory>(box)->getQuantity("Stick"); };

		uint32_t most = 0;
		ASSERT_TRUE(runUntil([&] {
			most = std::max(most, stored());
			return stored() >= 4;
		})) << "cutting stocks the box to its minimum";
		ASSERT_TRUE(runUntil([&] {
			most = std::max(most, stored());
			return GoalTaskRegistry::Get().goalCount(TaskType::Harvest) == 0 && !getTask(colonist)->isActive();
		})) << "the stocking harvest retires once the minimum is met";

		const glm::vec2 pilePos{3.0F, 0.0F};
		makePile(pilePos, stickId, 20);
		remember(colonist, pilePos, "Stick", assets::CapabilityType::Carryable);
		ASSERT_TRUE(runUntil([&] {
			most = std::max(most, stored());
			return stored() == 7;
		})) << "known stock fills the box to its maximum";
		runUntil([&] {
			most = std::max(most, stored());
			return false;
		}, 600);
		EXPECT_EQ(stored(), 7U);
		EXPECT_EQ(most, 7U) << "never past the maximum";
	}

	// A colonist carrying goods to a site is interrupted by hunger, eats, and then carries the same goods in.
	TEST_F(ChainFlowTest, ColonistInterruptedByANeedMidHaulResumesWithTheGoods) {
		const glm::vec2 sitePos{0.0F, 6.0F};
		const Site		site = constructionSite(sitePos, "Stone", 4);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		auto&			inventory = *world->getComponent<Inventory>(colonist);
		inventory.addItem("Stone", 4);
		inventory.addItem("Berry", 1);

		decide(colonist);
		Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		const uint64_t haul = task.instanceId;
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::FulfillNeed);
		ASSERT_TRUE(task.suspended.has_value());

		uint64_t deliveredBy = 0;
		ASSERT_TRUE(runUntil([&] {
			if (task.type == TaskType::Haul) {
				deliveredBy = task.instanceId;
			}
			return world->getComponent<StructureBlueprint>(site.blueprint)->remaining("Stone") == 0;
		})) << "the goods reach the site";
		EXPECT_EQ(deliveredBy, haul) << "the carry-in adopted the interrupted haul";
		EXPECT_EQ(carried(colonist, "Stone"), 0U);
		EXPECT_GT(world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value, 5.0F) << "and it ate";
	}

	// Two colonists serve one craft that needs three stones from a pile both know. In-flight keeps the
	// second from fetching what the first is bringing: the station gets exactly three, the pile loses
	// exactly three, and nobody is left holding a fourth.
	TEST_F(ChainFlowTest, TwoColonistsServingOneGoalNeverOverDeliver) {
		const EntityID	station = makeStation({0.0F, 6.0F}, {{"Stone", 0, 3}}, 1);
		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	pile = makePile(pilePos, stoneId, 10);
		const EntityID	first = createColonist({0.0F, 0.0F});
		const EntityID	second = createColonist({0.5F, 0.0F});
		for (EntityID colonist : {first, second}) {
			remember(colonist, pilePos, "Stone", assets::CapabilityType::Carryable);
		}
		const auto* store = world->getComponent<Inventory>(station);

		// The goal systems poll on a throttle; run until someone takes the fetch.
		ASSERT_TRUE(runUntil([&] { return getTask(first)->type == TaskType::Haul || getTask(second)->type == TaskType::Haul; }, 200));
		const auto fetching = [&](EntityID colonist) { return getTask(colonist)->type == TaskType::Haul ? 1 : 0; };
		EXPECT_EQ(fetching(first) + fetching(second), 1) << "the second sees the first's in-flight and fetches nothing";

		ASSERT_TRUE(runUntil([&] { return store->getQuantity("Stone") >= 3 || world->getComponent<WorkQueue>(station)->getNextJob() == nullptr; }));
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 7U) << "exactly three left the pile";
		EXPECT_EQ(carried(first, "Stone") + carried(second, "Stone"), 0U) << "nobody holds a fourth";
	}

	// Two colonists, one bush, a craft short two sticks: the first cut's expected yield covers the need,
	// so the second does not cut too.
	TEST_F(ChainFlowTest, TwoColonistsDoNotBothCutWhatOneCutCovers) {
		makeStation({0.0F, 6.0F}, {{"Stick", 0, 2}}, 1);
		const EntityID first = createColonist({0.0F, 0.0F});
		const EntityID second = createColonist({0.5F, 0.0F});
		for (EntityID colonist : {first, second}) {
			remember(colonist, {-3.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);
			remember(colonist, {3.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable);
		}
		const auto cutting = [&](EntityID colonist) { return getTask(colonist)->type == TaskType::Harvest ? 1 : 0; };
		ASSERT_TRUE(runUntil([&] { return cutting(first) + cutting(second) > 0; }, 200));
		EXPECT_EQ(cutting(first) + cutting(second), 1);
	}

	// =============================================================================
	// Batching: a colonist gathering for a root fills its trip before walking (task-claims.md,
	// Batching). Colonists walk (MovementSystem and PhysicsSystem, no mesh: direct steering) and every
	// cut starts its plant's regrowth, so nothing teleports and each plant gives once.
	// =============================================================================

	class ChainWalkTest : public ChainClaimsTest {
	  protected:
		void SetUp() override {
			ChainClaimsTest::SetUp();
			world->registerSystem<TimeSystem>();
			world->registerSystem<MovementSystem>();
			world->registerSystem<PhysicsSystem>();
			registerItem("Fibre", 1, kFibreKg);
			assets::AssetDefinition reed;
			reed.defName = "Reed";
			reed.label = "Reed";
			reed.capabilities.harvestable = assets::HarvestableCapability{};
			reed.capabilities.harvestable->yieldDefName = "Fibre";
			reed.capabilities.harvestable->amountMin = 3;
			reed.capabilities.harvestable->amountMax = 3;
			reed.capabilities.harvestable->durability = 1.0F;
			reed.capabilities.harvestable->regrowthTime = 600.0F;
			reed.capabilities.harvestable->destructive = false;
			assets::AssetRegistry::Get().registerTestDefinition(std::move(reed));
			fibreId = assets::AssetRegistry::Get().getDefNameId("Fibre");
			actions->setTakeSourceCallback([this](const std::string&, float x, float y, ActionSystem::SourceTake take, float) {
				const glm::vec2 at{x, y};
				if (std::find(felled.begin(), felled.end(), at) != felled.end()) {
					return ActionSystem::SourceState::Gone;
				}
				if (take == ActionSystem::SourceTake::Remove) {
					felled.push_back(at);
					return ActionSystem::SourceState::Ready;
				}
				const bool wasCut = std::find(cut.begin(), cut.end(), at) != cut.end();
				if (take == ActionSystem::SourceTake::Regrow && !wasCut) {
					cut.push_back(at);
				}
				return wasCut ? ActionSystem::SourceState::OnCooldown : ActionSystem::SourceState::Ready;
			});
			ai->setSourceRegrowthCallback([this](uint32_t, glm::vec2 at, double) {
				return std::find(cut.begin(), cut.end(), at) != cut.end() ? std::optional<double>{1.0e9} : std::nullopt;
			});
			actions->setDropResourceCallback([this](const std::string& defName, float x, float y, uint32_t quantity) {
				drops.push_back({assets::AssetRegistry::Get().getDefNameId(defName), {x, y}, quantity});
			});
		}

		/// Land the drops queued this frame as loose piles, merged into a pile already at the spot, and let
		/// every watcher see them (vision, in the game).
		void landDrops() {
			for (const Drop& drop : drops) {
				EntityID pile = actions->loosePiles().find(*world, drop.item, drop.at);
				if (pile != kInvalidEntity) {
					world->getComponent<ResourceStack>(pile)->quantity += drop.quantity;
				} else {
					pile = world->createEntity();
					world->addComponent<Position>(pile, Position{drop.at});
					makeLoosePile(*world, pile, drop.item, drop.at, drop.quantity);
					piles.emplace_back(pile, drop.item);
				}
				for (EntityID watcher : watchers) {
					remember(watcher, drop.at, assets::AssetRegistry::Get().getDefName(drop.item), assets::CapabilityType::Carryable);
				}
				dropped += drop.quantity;
			}
			drops.clear();
		}

		uint32_t onTheGround(uint32_t item) {
			uint32_t total = 0;
			for (const auto& [pile, pileItem] : piles) {
				if (pileItem == item && world->isAlive(pile)) {
					total += world->getComponent<ResourceStack>(pile)->quantity;
				}
			}
			return total;
		}

		/// A blueprint wanting `needed` fibre and its construction umbrella with a cutting child.
		Site fibreSite(glm::vec2 position, uint32_t needed) {
			const Site site = constructionSite(position, "Fibre", needed);
			childOf(site, position);
			return site;
		}

		void childOf(const Site& site, glm::vec2 position, uint32_t item = 0) {
			GoalTask harvest;
			harvest.type = TaskType::Harvest;
			harvest.owner = GoalOwner::ConstructionGoalSystem;
			harvest.destinationEntity = site.blueprint;
			harvest.destinationPosition = position;
			harvest.yieldDefNameId = item != 0 ? item : fibreId;
			harvest.parentGoalId = site.umbrella;
			GoalTaskRegistry::Get().createGoal(std::move(harvest));
		}

		void rememberReeds(EntityID colonist, std::initializer_list<glm::vec2> reeds) {
			for (const glm::vec2 reed : reeds) {
				remember(colonist, reed, "Reed", assets::CapabilityType::Harvestable);
			}
		}

		uint32_t delivered(const Site& site) {
			const auto* blueprint = world->getComponent<StructureBlueprint>(site.blueprint);
			return blueprint->required.front().second - blueprint->remaining(blueprint->required.front().first);
		}

		/// Run frames until `done` holds; each delivery to the site and each cut the colonist lands is
		/// counted.
		template <typename Done> bool walkUntil(const Site& site, EntityID colonist, Done done, int maxFrames = 4000) {
			uint32_t lastDelivered = delivered(site);
			uint32_t lastCarried = carriedFibre(colonist);
			for (int frame = 0; frame < maxFrames; ++frame) {
				world->update(0.1F);
				landDrops();
				const uint32_t nowDelivered = delivered(site);
				const uint32_t nowCarried = carriedFibre(colonist);
				trips += nowDelivered > lastDelivered ? 1 : 0;
				cuts += nowCarried > lastCarried ? 1 : 0;
				lastDelivered = nowDelivered;
				lastCarried = nowCarried;
				if (done()) {
					return true;
				}
			}
			return false;
		}

		uint32_t carriedFibre(EntityID colonist) { return ecs::availableQuantity(*world->getComponent<Inventory>(colonist), "Fibre"); }

		uint32_t perTrip() { return ecs::cargoUnitsPerTrip(assets::AssetRegistry::Get(), "Fibre", Inventory::createForColonist().carryCapacityKg); }

		static constexpr float kFibreKg = 3.5F;
		uint32_t			   fibreId = 0;
		std::vector<glm::vec2> cut;
		std::vector<glm::vec2> felled;
		struct Drop {
			uint32_t  item = 0;
			glm::vec2 at{0.0F, 0.0F};
			uint32_t  quantity = 0;
		};
		std::vector<Drop>							drops;
		std::vector<std::pair<EntityID, uint32_t>> piles;
		std::vector<EntityID>						watchers;
		uint32_t									dropped = 0;
		int					   trips = 0;
		int					   cuts = 0;
	};

	// Twenty fibre at three a cut: the colonist cuts until its trip is full and only then walks, so the
	// trips are as few as its carry allows, not one per cut. Per trip the cut demand never passes what
	// the trip can still take.
	TEST_F(ChainWalkTest, AColonistFillsItsTripBeforeWalking) {
		const Site	   site = fibreSite({0.0F, 12.0F}, 20);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		rememberReeds(colonist, {{2.0F, 0.0F}, {3.0F, 1.0F}, {4.0F, 0.0F}, {5.0F, 1.0F}, {6.0F, 0.0F}, {7.0F, 1.0F}, {8.0F, 0.0F}, {9.0F, 1.0F}});
		ASSERT_EQ(perTrip(), 10U);

		uint32_t mostCarried = 0;
		uint32_t mostInFlight = 0;
		ASSERT_TRUE(walkUntil(site, colonist, [&] {
			mostCarried = std::max(mostCarried, carriedFibre(colonist));
			mostInFlight = std::max(mostInFlight, ai->claimIndex().inFlight(site.umbrella, fibreId, {}));
			return delivered(site) == 20;
		})) << "delivered " << delivered(site);
		EXPECT_EQ(trips, 3) << "nine, nine, and the last two";
		EXPECT_EQ(cuts, 7);
		EXPECT_EQ(cut.size(), 7U);
		EXPECT_EQ(mostCarried, 9U) << "a tenth unit is no room for a cut of three";
		EXPECT_LE(mostInFlight, perTrip()) << "the cuts of one trip never claim more than it carries";
		EXPECT_EQ(dropped, 0U) << "no cut landed more than fit";
		EXPECT_EQ(delivered(site) + carriedFibre(colonist), 3U * static_cast<uint32_t>(cut.size())) << "every unit the reeds gave is accounted for";
	}

	// A first cut into a pack with room for one unit lands one and drops the other two beside the reed as
	// a pile, which is carried in before another reed is cut: nothing a cut gives is lost.
	TEST_F(ChainWalkTest, WhatACutCannotCarryDropsAsAPile) {
		registerItem("Rock", 1, 31.0F);
		const Site	   site = fibreSite({0.0F, 12.0F}, 3);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		watchers = {colonist};
		auto& inventory = *world->getComponent<Inventory>(colonist);
		inventory.addItem("Rock", 1);
		rememberReeds(colonist, {{2.0F, 0.0F}, {4.0F, 0.0F}});
		ASSERT_EQ(pickupUnitsThatFit(inventory, assets::AssetRegistry::Get(), "Fibre", carriedCargoMassKg(inventory, assets::AssetRegistry::Get())), 1U);

		ASSERT_TRUE(walkUntil(site, colonist, [&] { return dropped > 0; }));
		EXPECT_EQ(dropped, 2U) << "the two that did not fit";
		EXPECT_EQ(carriedFibre(colonist), 1U);
		ASSERT_TRUE(walkUntil(site, colonist, [&] { return delivered(site) == 3; })) << "delivered " << delivered(site);
		EXPECT_EQ(cut.size(), 1U) << "the pile is carried in before the second reed is cut";
		EXPECT_EQ(delivered(site) + carriedFibre(colonist) + onTheGround(fibreId), 3U);
	}

	// A fell gives seven logs; an armful holds five. The two that fall are carried in before another tree
	// is felled, and the second tree falls only once the room left takes all the site still wants.
	TEST_F(ChainWalkTest, AFellsOverflowIsFetchedBeforeAnotherTreeFalls) {
		registerItem("Log", 2, 7.0F);
		assets::AssetDefinition pine;
		pine.defName = "Pine";
		pine.label = "Pine";
		pine.capabilities.harvestable = assets::HarvestableCapability{};
		pine.capabilities.harvestable->yieldDefName = "Log";
		pine.capabilities.harvestable->amountMin = 7;
		pine.capabilities.harvestable->amountMax = 7;
		pine.capabilities.harvestable->durability = 1.0F;
		pine.capabilities.harvestable->destructive = true;
		assets::AssetRegistry::Get().registerTestDefinition(std::move(pine));
		const uint32_t logId = assets::AssetRegistry::Get().getDefNameId("Log");
		ASSERT_EQ(ecs::cargoUnitsPerTrip(assets::AssetRegistry::Get(), "Log", Inventory::createForColonist().carryCapacityKg), 5U);

		const Site site = constructionSite({0.0F, 12.0F}, "Log", 10);
		childOf(site, {0.0F, 12.0F}, logId);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		watchers = {colonist};
		for (const glm::vec2 tree : {glm::vec2{3.0F, 0.0F}, glm::vec2{6.0F, 0.0F}, glm::vec2{9.0F, 0.0F}}) {
			remember(colonist, tree, "Pine", assets::CapabilityType::Harvestable);
		}
		const auto logs = [&] { return ecs::availableQuantity(*world->getComponent<Inventory>(colonist), "Log"); };

		int		 firstPickup = -1;
		int		 secondFell = -1;
		uint32_t lastGround = 0;
		for (int frame = 0; frame < 4000 && delivered(site) < 10; ++frame) {
			world->update(0.1F);
			landDrops();
			const uint32_t ground = onTheGround(logId);
			if (firstPickup < 0 && ground < lastGround) {
				firstPickup = frame;
			}
			lastGround = ground;
			if (secondFell < 0 && felled.size() == 2) {
				secondFell = frame;
			}
		}
		ASSERT_EQ(delivered(site), 10U);
		ASSERT_GE(firstPickup, 0) << "the overflow was never picked up";
		ASSERT_GE(secondFell, 0);
		EXPECT_LT(firstPickup, secondFell) << "the pile is taken up before another tree falls";
		EXPECT_EQ(felled.size(), 2U) << "five, then two from the pile and three from a second tree";
		EXPECT_EQ(delivered(site) + logs() + onTheGround(logId), 7U * static_cast<uint32_t>(felled.size())) << "every log is accounted for";
	}

	// While the first colonist gathers, what it carries and the cut it has accepted are its in-flight:
	// a second colonist sees the need covered and does not cut.
	TEST_F(ChainWalkTest, ASecondColonistDoesNotCutWhatTheFirstCarriesAndHasAccepted) {
		const Site	   site = fibreSite({0.0F, 12.0F}, 6);
		const EntityID first = createColonist({0.0F, 0.0F});
		rememberReeds(first, {{2.0F, 0.0F}, {4.0F, 0.0F}, {6.0F, 0.0F}});

		ASSERT_TRUE(walkUntil(site, first, [&] {
			const Task& task = *getTask(first);
			return task.type == TaskType::Harvest && claimOf(task, ClaimKind::Inbound).has_value();
		})) << "the first never went on to a second cut";
		const Task& gathering = *getTask(first);
		EXPECT_EQ(claimOf(gathering, ClaimKind::Inbound)->quantity(), 3U) << "the first cut's goods ride along";
		EXPECT_EQ(claimOf(gathering, ClaimKind::InFlight)->quantity(), 6U) << "carried plus the accepted cut";

		const EntityID second = createColonist({0.0F, 1.0F});
		rememberReeds(second, {{2.0F, 0.0F}, {4.0F, 0.0F}, {6.0F, 0.0F}});
		decide(second);
		EXPECT_NE(getTask(second)->type, TaskType::Harvest);
		EXPECT_EQ(optionOf(second, TaskType::Harvest, OptionStatus::Available), nullptr);
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, fibreId, {}), 6U);
	}

	// A partly full trip whose other sources are regrowing or reserved is delivered as it is.
	TEST_F(ChainWalkTest, APartlyFullTripIsDeliveredWhenNoOtherSourceIsFree) {
		const Site		site = fibreSite({0.0F, 12.0F}, 20);
		const glm::vec2 own{2.0F, 0.0F};
		const glm::vec2 taken{-6.0F, 0.0F};
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		rememberReeds(colonist, {own, taken});
		const EntityID other = createColonist({-5.0F, 0.0F});
		rememberReeds(other, {taken});
		world->addComponent<Skills>(other, Skills{}).setLevel("Farming", 0.0F);

		decideAll({other, colonist});
		ASSERT_EQ(getTask(other)->harvestTargetEntityId, hashWorldEntity(taken, assets::AssetRegistry::Get().getDefNameId("Reed")));
		// Walk until the colonist has decided again with its first cut in hand.
		ASSERT_TRUE(walkUntil(site, colonist, [&] {
			const EvaluatedOption* chosen = world->getComponent<DecisionTrace>(colonist)->getSelected();
			return carriedFibre(colonist) == 3U && chosen != nullptr && chosen->taskType == TaskType::Haul;
		}));
		const Task& task = *getTask(colonist);
		EXPECT_TRUE(task.haulFromInventory);
		EXPECT_EQ(task.haulQuantity, 3U) << "three of a trip of " << perTrip();
		// Its own reed is regrowing; the far one is the other colonist's (held, or already cut).
		EXPECT_EQ(optionOf(colonist, TaskType::Harvest, OptionStatus::Available), nullptr);
		EXPECT_NE(optionOf(colonist, TaskType::Harvest, OptionStatus::Regrowing), nullptr);
		ASSERT_TRUE(walkUntil(site, colonist, [&] { return carriedFibre(colonist) == 0U; }, 400));
		EXPECT_GE(delivered(site), 3U);
	}

	// =============================================================================
	// The AI respects claims: what others hold is subtracted while a colonist decides, and a target
	// taken whole is shown Reserved, naming its holder. Positions are real and nothing teleports; each
	// update runs the validator.
	// =============================================================================

	// A cut and a whole-pile fetch, each re-decided while the colonist walks: its own claims are left
	// out of what it reads, so it never sees its own target as taken and drops it.
	TEST_F(ChainClaimsTest, AClaimantKeepsItsTargetAcrossReevaluations) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 bushPos{4.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stick", 20);
		childGoal(TaskType::Harvest, root, station, stationPos, stickId);
		const EntityID cutter = createColonist({0.0F, 0.0F});
		remember(cutter, bushPos, "StickBush", assets::CapabilityType::Harvestable);

		const glm::vec2 boxPos{0.0F, -10.0F};
		const glm::vec2 pilePos{-4.0F, 0.0F};
		const EntityID	box = makeBox(boxPos, StoragePriority::Medium);
		storageGoal(box, boxPos);
		const EntityID pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, stoneId, pilePos, 8);
		const EntityID hauler = createColonist({0.0F, 0.0F});
		remember(hauler, pilePos, "Stone", assets::CapabilityType::Carryable);

		decideAll({cutter, hauler});
		const Task& cut = *getTask(cutter);
		const Task& haul = *getTask(hauler);
		ASSERT_EQ(cut.type, TaskType::Harvest);
		ASSERT_EQ(haul.type, TaskType::Haul);
		ASSERT_EQ(haul.haulQuantity, 8U) << "the whole pile";
		const uint64_t cutInstance = cut.instanceId;
		const uint64_t haulInstance = haul.instanceId;

		for (int step = 1; step <= 4; ++step) {
			const float t = static_cast<float>(step) / 5.0F;
			world->getComponent<Position>(cutter)->value = bushPos * t;
			world->getComponent<Position>(hauler)->value = pilePos * t;
			decideAll({cutter, hauler});
			EXPECT_EQ(cut.instanceId, cutInstance) << "step " << step;
			EXPECT_EQ(haul.instanceId, haulInstance) << "step " << step;
			EXPECT_EQ(optionOf(cutter, TaskType::Harvest, OptionStatus::Reserved), nullptr);
			EXPECT_EQ(optionOf(hauler, TaskType::Haul, OptionStatus::Reserved), nullptr);
			const EvaluatedOption* own = optionOf(hauler, TaskType::Haul, OptionStatus::Selected);
			ASSERT_NE(own, nullptr);
			EXPECT_EQ(own->haulQuantity, 8U) << "its own claim on the pile is not subtracted";
		}
	}

	// Two colonists, one bush, a craft that needs far more than one cut: in-flight would let both cut,
	// the Use claim does not. The second sees the bush Reserved by the first and takes other work.
	TEST_F(ChainClaimsTest, TwoColonistsOneTreeOneCutsTheOtherWorksElsewhere) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stick", 20);
		childGoal(TaskType::Harvest, root, station, stationPos, stickId);

		const glm::vec2 boxPos{0.0F, -8.0F};
		const glm::vec2 pilePos{-3.0F, 0.0F};
		const EntityID	box = makeBox(boxPos, StoragePriority::Medium);
		storageGoal(box, boxPos);
		const EntityID pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, stoneId, pilePos, 4);

		const EntityID first = createColonist({0.0F, 0.0F});
		const EntityID second = createColonist({0.5F, 0.0F});
		for (EntityID colonist : {first, second}) {
			remember(colonist, bushPos, "StickBush", assets::CapabilityType::Harvestable);
			remember(colonist, pilePos, "Stone", assets::CapabilityType::Carryable);
		}

		decideAll({first, second});
		const auto cutting = [&](EntityID colonist) { return getTask(colonist)->type == TaskType::Harvest; };
		ASSERT_EQ(cutting(first) + cutting(second), 1) << "exactly one cuts";
		const EntityID cutter = cutting(first) ? first : second;
		const EntityID other = cutter == first ? second : first;
		EXPECT_EQ(getTask(other)->type, TaskType::Haul) << "the other takes other work";
		const EvaluatedOption* reserved = optionOf(other, TaskType::Harvest, OptionStatus::Reserved);
		ASSERT_NE(reserved, nullptr) << "the bush is shown, not hidden";
		EXPECT_EQ(reserved->reservedBy, cutter);
		EXPECT_EQ(optionOf(other, TaskType::Harvest, OptionStatus::Available), nullptr);
	}

	// A craft short three stones, one loose stone known to two colonists: the fetch is sized to the one
	// unit there is, and the other colonist sees it Reserved instead of walking to an empty pile.
	TEST_F(ChainClaimsTest, TheLastUnitOfAnIngredientIsFetchedOnce) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stone", 3);
		childGoal(TaskType::Haul, root, station, stationPos, stoneId);
		const EntityID pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, stoneId, pilePos, 1);
		const EntityID first = createColonist({0.0F, 0.0F});
		const EntityID second = createColonist({0.5F, 0.0F});
		for (EntityID colonist : {first, second}) {
			remember(colonist, pilePos, "Stone", assets::CapabilityType::Carryable);
		}

		decideAll({first, second});
		const auto fetching = [&](EntityID colonist) { return getTask(colonist)->type == TaskType::Haul; };
		ASSERT_EQ(fetching(first) + fetching(second), 1) << "exactly one fetches";
		const EntityID fetcher = fetching(first) ? first : second;
		const EntityID other = fetcher == first ? second : first;
		EXPECT_EQ(getTask(fetcher)->haulQuantity, 1U) << "no more than the pile holds";
		EXPECT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 1U);
		const EvaluatedOption* reserved = optionOf(other, TaskType::Haul, OptionStatus::Reserved);
		ASSERT_NE(reserved, nullptr);
		EXPECT_EQ(reserved->reservedBy, fetcher);

		// Later decisions agree.
		decide(other);
		EXPECT_NE(getTask(other)->type, TaskType::Haul);
	}

	// Three colonists, three different items, a box with one free slot: inbound slots are shared, so
	// only one haul is accepted.
	TEST_F(ChainClaimsTest, ThreeHaulsOfThreeItemsShareOneFreeSlot) {
		registerItem("Clay", 1, 0.5F, 5);
		const uint32_t clayId = assets::AssetRegistry::Get().getDefNameId("Clay");
		const glm::vec2 boxPos{0.0F, 10.0F};
		const EntityID	box = world->createEntity();
		world->addComponent<Position>(box, Position{boxPos});
		world->addComponent<Appearance>(box, Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		auto store = Inventory::createForStorage();
		store.maxCapacity = 1;
		world->addComponent<Inventory>(box, std::move(store));
		StorageConfiguration config;
		for (const char* item : {"Stone", "Stick", "Clay"}) {
			config.addRule(StorageRule{.defName = item, .category = assets::ItemCategory::RawMaterial});
		}
		world->addComponent<StorageConfiguration>(box, std::move(config));
		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = box;
		goal.destinationPosition = boxPos;
		goal.acceptedDefNameIds = {stoneId, stickId, clayId};
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		std::vector<EntityID> haulers;
		const std::pair<const char*, uint32_t> items[] = {{"Stone", stoneId}, {"Stick", stickId}, {"Clay", clayId}};
		for (size_t i = 0; i < 3; ++i) {
			const glm::vec2 pilePos{-4.0F + 4.0F * static_cast<float>(i), 0.0F};
			const EntityID	pile = world->createEntity();
			world->addComponent<Position>(pile, Position{pilePos});
			makeLoosePile(*world, pile, items[i].second, pilePos, 5);
			const EntityID colonist = createColonist({pilePos.x, -1.0F});
			remember(colonist, pilePos, items[i].first, assets::CapabilityType::Carryable);
			haulers.push_back(colonist);
		}

		decideAll({haulers[0], haulers[1], haulers[2]});
		int hauling = 0;
		for (EntityID colonist : haulers) {
			hauling += getTask(colonist)->type == TaskType::Haul ? 1 : 0;
		}
		EXPECT_EQ(hauling, 1) << "one free slot takes one item's haul";
		EXPECT_EQ(ai->claimIndex().inboundSlots(box, {}, [](uint32_t) { return InboundStackRoom{.partialRoom = 0, .stackSize = 40}; }), 1U);
	}

	// A pile of 100 and two haulers, each carrying at most 70 a trip: the second's fetch is sized to
	// what the first left, and together they never claim more than the pile holds.
	TEST_F(ChainClaimsTest, TwoHaulersSplitAPileUpToItsQuantity) {
		const glm::vec2 boxPos{0.0F, 10.0F};
		const glm::vec2 pilePos{4.0F, 0.0F};
		const EntityID	box = makeBox(boxPos, StoragePriority::Medium);
		storageGoal(box, boxPos);
		const EntityID pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, stoneId, pilePos, 100);
		const EntityID first = createColonist({0.0F, 0.0F});
		const EntityID second = createColonist({0.5F, 0.0F});
		const EntityID third = createColonist({1.0F, 0.0F});
		for (EntityID colonist : {first, second, third}) {
			remember(colonist, pilePos, "Stone", assets::CapabilityType::Carryable);
		}

		decide(first);
		const uint32_t firstTakes = getTask(first)->haulQuantity;
		ASSERT_LT(firstTakes, 100U) << "one trip can't take the whole pile";
		decide(second);
		ASSERT_EQ(getTask(second)->type, TaskType::Haul);
		EXPECT_EQ(getTask(second)->haulQuantity, 100U - firstTakes) << "sized to what the first left";
		const ClaimTarget source{.kind = TargetKind::WorldKey, .key = hashWorldEntity(pilePos, stoneId)};
		EXPECT_EQ(ai->claimIndex().sourceClaimed(source, stoneId, {}), 100U);

		decide(third);
		EXPECT_NE(getTask(third)->type, TaskType::Haul) << "nothing left to claim";
		ASSERT_NE(optionOf(third, TaskType::Haul, OptionStatus::Reserved), nullptr);
	}

	// One cut covers the craft; while the cutter carries it in, the source's Use has dropped but the
	// in-flight still covers the need, so nobody cuts again.
	TEST_F(ChainClaimsTest, NobodyRecutsWhileACutIsCarriedIn) {
		registerHarvestable("FullBush", "Stick", 3, 3);
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stick", 3);
		childGoal(TaskType::Harvest, root, station, stationPos, stickId);
		const EntityID cutter = createColonist({0.0F, 0.0F});
		const EntityID other = createColonist({0.0F, 1.0F});
		for (EntityID colonist : {cutter, other}) {
			remember(colonist, bushPos, "FullBush", assets::CapabilityType::Harvestable);
			remember(colonist, {-3.0F, 0.0F}, "FullBush", assets::CapabilityType::Harvestable);
		}

		decide(cutter);
		const Task& task = *getTask(cutter);
		ASSERT_EQ(task.type, TaskType::Harvest);
		act(cutter, task.targetPosition);
		ASSERT_EQ(task.type, TaskType::Haul) << "the cut is being carried in";
		ASSERT_FALSE(claimOf(task, ClaimKind::Use).has_value()) << "the source is free, the need is not";
		ASSERT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), 3U);

		for (int frame = 0; frame < 3; ++frame) {
			decide(other);
			EXPECT_NE(getTask(other)->type, TaskType::Harvest) << "frame " << frame;
			EXPECT_EQ(optionOf(other, TaskType::Harvest, OptionStatus::Available), nullptr);
		}
	}

	// A bush held by a colonist who then takes player control, or dies, is offered again at once.
	TEST_F(ChainClaimsTest, ASourceIsFreedWhenItsHolderTakesControlOrDies) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stick", 20);
		childGoal(TaskType::Harvest, root, station, stationPos, stickId);
		const EntityID first = createColonist({0.0F, 0.0F});
		const EntityID second = createColonist({0.5F, 0.0F});
		for (EntityID colonist : {first, second}) {
			remember(colonist, bushPos, "StickBush", assets::CapabilityType::Harvestable);
		}

		decide(first);
		ASSERT_EQ(getTask(first)->type, TaskType::Harvest);
		decide(second);
		ASSERT_NE(getTask(second)->type, TaskType::Harvest);

		ai->enterControl(first);
		decide(second);
		ASSERT_EQ(getTask(second)->type, TaskType::Harvest) << "free once its holder is under player control";

		ai->releaseControl(first);
		decide(first);
		ASSERT_NE(getTask(first)->type, TaskType::Harvest);
		world->destroyEntity(second);
		decide(first);
		EXPECT_EQ(getTask(first)->type, TaskType::Harvest) << "free once its holder is gone";
	}

	// A colonist whose route to its claimed bush has failed (in another region, where no verdict is
	// remembered, so it keeps re-selecting the same cut) holds nothing while it stays failed: another
	// colonist is offered the bush and takes it.
	TEST_F(ChainClaimsTest, AColonistWhoseRouteFailedHoldsNoClaims) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stick", 20);
		childGoal(TaskType::Harvest, root, station, stationPos, stickId);
		const EntityID stuck = createColonist({0.0F, 0.0F});
		const EntityID other = createColonist({0.5F, 0.0F});
		for (EntityID colonist : {stuck, other}) {
			remember(colonist, bushPos, "StickBush", assets::CapabilityType::Harvestable);
		}
		decide(stuck);
		ASSERT_EQ(getTask(stuck)->type, TaskType::Harvest);
		const uint64_t instance = getTask(stuck)->instanceId;

		// No route under the reach it stands in now (no mesh here: stamp 0), so a re-decision keeps the task.
		getTask(stuck)->navState = NavState::CantFindWayTo; // as requestNavPath leaves a Blocked route: stopped
		world->getComponent<MovementTarget>(stuck)->active = false;
		getTask(stuck)->blockedReachStamp = 0;
		decide(stuck);
		ASSERT_EQ(getTask(stuck)->instanceId, instance) << "it keeps re-selecting the same cut";
		EXPECT_EQ(indexedClaimsOf(stuck), 0U);

		decide(other);
		EXPECT_EQ(getTask(other)->type, TaskType::Harvest) << "the stranded colonist's bush is offered";
		EXPECT_EQ(getTask(other)->harvestTargetEntityId, hashWorldEntity(bushPos, assets::AssetRegistry::Get().getDefNameId("StickBush")));
		EXPECT_NE(getTask(stuck)->type, TaskType::Harvest) << "and the stranded colonist, seeing it taken, lets it go";
	}

	// The same stranded colonist, with nobody else wanting the bush: once its route succeeds (the reach it
	// failed under has changed, so it picks again and is routed), its claims are back and others see the
	// bush taken.
	TEST_F(ChainClaimsTest, AFailedRouteThatSucceedsTakesItsClaimsBack) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stick", 20);
		childGoal(TaskType::Harvest, root, station, stationPos, stickId);
		const EntityID stuck = createColonist({0.0F, 0.0F});
		remember(stuck, bushPos, "StickBush", assets::CapabilityType::Harvestable);
		decide(stuck);
		ASSERT_EQ(getTask(stuck)->type, TaskType::Harvest);
		getTask(stuck)->navState = NavState::CantFindWayTo; // as requestNavPath leaves a Blocked route: stopped
		world->getComponent<MovementTarget>(stuck)->active = false;
		getTask(stuck)->blockedReachStamp = 0;
		decide(stuck);
		ASSERT_EQ(indexedClaimsOf(stuck), 0U);

		getTask(stuck)->blockedReachStamp = 1; // failed under a reach it no longer stands in
		decide(stuck);
		ASSERT_EQ(getTask(stuck)->type, TaskType::Harvest);
		EXPECT_EQ(getTask(stuck)->navState, NavState::Traveling);
		EXPECT_GT(indexedClaimsOf(stuck), 0U);

		const EntityID other = createColonist({0.5F, 0.0F});
		remember(other, bushPos, "StickBush", assets::CapabilityType::Harvestable);
		decide(other);
		EXPECT_NE(getTask(other)->type, TaskType::Harvest);
		const EvaluatedOption* reserved = optionOf(other, TaskType::Harvest, OptionStatus::Reserved);
		ASSERT_NE(reserved, nullptr);
		EXPECT_EQ(reserved->reservedBy, stuck);
	}

	// Two colonists at one station with every input in its store: the first's Craft selection holds the
	// station's slot, and the second sees it in use.
	TEST_F(ChainClaimsTest, AStationsSlotIsHeldByTheCrafterThatSelectedIt) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		craftRoot(station, stationPos, "Stone", 2);
		world->getComponent<Inventory>(station)->addItem("Stone", 2);
		const EntityID first = createColonist({0.0F, 0.0F});
		const EntityID second = createColonist({0.5F, 0.0F});

		decideAll({first, second});
		const auto crafting = [&](EntityID colonist) { return getTask(colonist)->type == TaskType::Craft; };
		ASSERT_EQ(crafting(first) + crafting(second), 1);
		const EntityID crafter = crafting(first) ? first : second;
		const EntityID other = crafter == first ? second : first;
		const EvaluatedOption* reserved = optionOf(other, TaskType::Craft, OptionStatus::Reserved);
		ASSERT_NE(reserved, nullptr);
		EXPECT_EQ(reserved->reservedBy, crafter);
	}

	// A hungry colonist's food is held only by someone who will eat it. A bush a worker is cutting for a
	// craft is still its food (no worse off than before claims); a bush another hungry colonist is
	// walking to is Reserved, and it looks for other food.
	TEST_F(ChainClaimsTest, OnlyAnotherEaterHoldsAHungryColonistsFood) {
		const glm::vec2 stationPos{0.0F, 8.0F};
		const glm::vec2 bushPos{3.0F, 0.0F};
		const uint32_t	berryId = assets::AssetRegistry::Get().getDefNameId("Berry");
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Berry", 20);
		childGoal(TaskType::Harvest, root, station, stationPos, berryId);

		const EntityID worker = createColonist({0.0F, 0.0F});
		const EntityID hungry = createColonist({0.0F, 1.0F});
		const EntityID alsoHungry = createColonist({0.0F, -1.0F});
		for (EntityID colonist : {worker, hungry, alsoHungry}) {
			remember(colonist, bushPos, "BerryBush", assets::CapabilityType::Harvestable);
		}
		decide(worker);
		ASSERT_EQ(getTask(worker)->type, TaskType::Harvest);

		world->getComponent<NeedsComponent>(hungry)->get(NeedType::Hunger).value = 30.0F;
		decide(hungry);
		ASSERT_EQ(getTask(hungry)->type, TaskType::FulfillNeed);
		EXPECT_EQ(getTask(hungry)->needSourceKey, hashWorldEntity(bushPos, assets::AssetRegistry::Get().getDefNameId("BerryBush")))
			<< "a work claim never keeps a hungry colonist from known food";

		world->getComponent<NeedsComponent>(alsoHungry)->get(NeedType::Hunger).value = 30.0F;
		decide(alsoHungry);
		EXPECT_NE(getTask(alsoHungry)->type, TaskType::FulfillNeed) << "the only bush is someone else's meal";
		const EvaluatedOption* reserved = optionOf(alsoHungry, TaskType::FulfillNeed, OptionStatus::Reserved);
		ASSERT_NE(reserved, nullptr);
		EXPECT_EQ(reserved->reservedBy, hungry);

		// Starving now, it is the more urgent eater: the routine meal's claim no longer holds the bush.
		world->getComponent<NeedsComponent>(alsoHungry)->get(NeedType::Hunger).value = 5.0F;
		decide(alsoHungry);
		ASSERT_EQ(getTask(alsoHungry)->type, TaskType::FulfillNeed);
		EXPECT_EQ(getTask(alsoHungry)->needSourceKey, hashWorldEntity(bushPos, assets::AssetRegistry::Get().getDefNameId("BerryBush")));
	}

	// A worker cutting the nearest bush for a craft doesn't keep food from a hungry colonist, but a free
	// bush a little farther is preferred over walking into the worker; the idle gather likewise takes
	// the next free bush instead of showing the held one Reserved.
	TEST_F(ChainClaimsTest, FoodNobodyHoldsIsPreferredToFoodAWorkerHolds) {
		const glm::vec2 stationPos{0.0F, 8.0F};
		const glm::vec2 heldBush{3.0F, 0.0F};
		const glm::vec2 freeBush{-5.0F, 0.0F};
		const uint32_t	berryId = assets::AssetRegistry::Get().getDefNameId("Berry");
		const uint32_t	bushId = assets::AssetRegistry::Get().getDefNameId("BerryBush");
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Berry", 20);
		childGoal(TaskType::Harvest, root, station, stationPos, berryId);

		const EntityID worker = createColonist({0.0F, 0.0F});
		remember(worker, heldBush, "BerryBush", assets::CapabilityType::Harvestable);
		decide(worker);
		ASSERT_EQ(getTask(worker)->harvestTargetEntityId, hashWorldEntity(heldBush, bushId));

		const EntityID hungry = createColonist({0.0F, 1.0F});
		const EntityID idle = createColonist({0.0F, -1.0F});
		for (EntityID colonist : {hungry, idle}) {
			remember(colonist, heldBush, "BerryBush", assets::CapabilityType::Harvestable);
			remember(colonist, freeBush, "BerryBush", assets::CapabilityType::Harvestable);
		}
		world->getComponent<NeedsComponent>(hungry)->get(NeedType::Hunger).value = 5.0F;
		decide(hungry);
		ASSERT_EQ(getTask(hungry)->type, TaskType::FulfillNeed);
		EXPECT_EQ(getTask(hungry)->needSourceKey, hashWorldEntity(freeBush, bushId));

		// The idle colonist knows the worker's bush and another free one farther off.
		const glm::vec2 otherFree{-7.0F, 0.0F};
		world->getComponent<Memory>(idle)->forgetWorldEntity(freeBush, bushId);
		remember(idle, otherFree, "BerryBush", assets::CapabilityType::Harvestable);
		decide(idle);
		bool gathersFree = false;
		for (const EvaluatedOption& option : world->getComponent<DecisionTrace>(idle)->options) {
			gathersFree |= option.gathersFood &&
						   option.targetPosition == otherFree && option.status != OptionStatus::Reserved;
		}
		EXPECT_TRUE(gathersFree) << "the gather goes to the next free bush";
	}

	// =============================================================================
	// Hungry colonists walking to food:MovementSystem and PhysicsSystem move them (no mesh: direct
	// steering), and a berry bush regrows once cut, so each bush feeds once.
	// =============================================================================

	class HungerWalkTest : public ChainClaimsTest {
	  protected:
		void SetUp() override {
			ChainClaimsTest::SetUp();
			world->registerSystem<TimeSystem>();
			world->registerSystem<MovementSystem>();
			world->registerSystem<PhysicsSystem>();
			assets::AssetDefinition bush;
			bush.defName = "RipeBush";
			bush.label = "RipeBush";
			bush.capabilities.harvestable = assets::HarvestableCapability{};
			bush.capabilities.harvestable->yieldDefName = "Berry";
			bush.capabilities.harvestable->amountMin = 3;
			bush.capabilities.harvestable->amountMax = 3;
			bush.capabilities.harvestable->durability = 1.0F;
			bush.capabilities.harvestable->regrowthTime = 600.0F;
			bush.capabilities.harvestable->destructive = false;
			assets::AssetRegistry::Get().registerTestDefinition(std::move(bush));
			berryId = assets::AssetRegistry::Get().getDefNameId("Berry");
			bushId = assets::AssetRegistry::Get().getDefNameId("RipeBush");
			// Only bushes stand in this world; a pile is answered by the pile index, and a pickup whose
			// pile has gone finds nothing.
			actions->setTakeSourceCallback([this](const std::string& defName, float x, float y, ActionSystem::SourceTake take, float) {
				if (defName != "RipeBush") {
					return ActionSystem::SourceState::Gone;
				}
				const glm::vec2 at{x, y};
				const bool		wasCut = std::find(cut.begin(), cut.end(), at) != cut.end();
				if (take == ActionSystem::SourceTake::Regrow && !wasCut) {
					cut.push_back(at);
				}
				return wasCut ? ActionSystem::SourceState::OnCooldown : ActionSystem::SourceState::Ready;
			});
			ai->setSourceRegrowthCallback([this](uint32_t, glm::vec2 at, double) {
				return std::find(cut.begin(), cut.end(), at) != cut.end() ? std::optional<double>{1.0e9} : std::nullopt;
			});
		}

		EntityID hungryColonist(glm::vec2 position, float hunger) {
			const EntityID colonist = createColonist(position);
			world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = hunger;
			return colonist;
		}

		void rememberBush(EntityID colonist, glm::vec2 position) { remember(colonist, position, "RipeBush", assets::CapabilityType::Harvestable); }

		float hunger(EntityID colonist) { return world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value; }

		/// Run 0.1 s frames until `done` holds, calling `each` after every frame.
		template <typename Done, typename Each>
		bool walkUntil(Done done, Each each, int maxFrames = 3000) {
			for (int frame = 0; frame < maxFrames; ++frame) {
				world->update(0.1F);
				each();
				if (done()) {
					return true;
				}
			}
			return false;
		}
		template <typename Done> bool walkUntil(Done done, int maxFrames = 3000) {
			return walkUntil(done, [] {}, maxFrames);
		}

		/// A storage box holding `berries`, which every colonist named remembers (colony knowledge).
		EntityID foodBox(glm::vec2 position, uint32_t berries, std::initializer_list<EntityID> knownBy) {
			const EntityID box = world->createEntity();
			world->addComponent<Position>(box, Position{position});
			world->addComponent<Appearance>(box, Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			auto inventory = Inventory::createForStorage();
			if (berries > 0) {
				inventory.addItem("Berry", berries);
			}
			world->addComponent<Inventory>(box, std::move(inventory));
			StorageConfiguration config;
			config.addRule(StorageRule{.defName = "Berry", .category = assets::ItemCategory::RawMaterial, .priority = StoragePriority::Medium});
			world->addComponent<StorageConfiguration>(box, std::move(config));
			for (EntityID colonist : knownBy) {
				remember(colonist, position, "Box", assets::CapabilityType::Storage);
			}
			return box;
		}

		EntityID berryPile(glm::vec2 position, uint32_t quantity, std::initializer_list<EntityID> knownBy) {
			const EntityID pile = world->createEntity();
			world->addComponent<Position>(pile, Position{position});
			makeLoosePile(*world, pile, berryId, position, quantity);
			for (EntityID colonist : knownBy) {
				remember(colonist, position, "Berry", assets::CapabilityType::Carryable);
			}
			return pile;
		}

		uint32_t boxBerries(EntityID box) { return world->getComponent<Inventory>(box)->getQuantity("Berry"); }

		uint32_t			   berryId = 0;
		uint32_t			   bushId = 0;
		std::vector<glm::vec2> cut;
	};

	// A routinely hungry colonist far off claims the only bush first; a starving one beside it is the
	// more urgent eater and goes anyway. It eats. The routine eater sees the bush taken by someone at
	// least as urgent, gives it up, and once the bush is regrowing waits instead of walking back to it.
	TEST_F(HungerWalkTest, AStarvingColonistBeatsARoutineEaterThatClaimedFirst) {
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	routine = hungryColonist({30.0F, 0.0F}, 30.0F);
		const EntityID	starving = hungryColonist({0.0F, 0.0F}, 5.0F);
		rememberBush(routine, bushPos);
		rememberBush(starving, bushPos);
		const uint64_t bushKey = hashWorldEntity(bushPos, bushId);

		decide(routine);
		ASSERT_EQ(getTask(routine)->needSourceKey, bushKey);
		decide(starving);
		ASSERT_EQ(getTask(starving)->type, TaskType::FulfillNeed);
		ASSERT_EQ(getTask(starving)->needSourceKey, bushKey) << "a less urgent eater's claim does not hold the bush";

		std::vector<uint64_t> routineMealsAtTheBush;
		const auto			  noteRoutine = [&] {
			const Task& task = *getTask(routine);
			if (task.type == TaskType::FulfillNeed && task.needSourceKey == bushKey &&
				std::find(routineMealsAtTheBush.begin(), routineMealsAtTheBush.end(), task.instanceId) == routineMealsAtTheBush.end()) {
				routineMealsAtTheBush.push_back(task.instanceId);
			}
		};
		ASSERT_TRUE(walkUntil([&] { return hunger(starving) >= 50.0F; }, noteRoutine)) << "the starving colonist ate";
		walkUntil([] { return false; }, noteRoutine, 300);
		EXPECT_EQ(routineMealsAtTheBush.size(), 1U) << "the routine eater never went back for the bush";
		EXPECT_NE(getTask(routine)->needSourceKey, bushKey);
		EXPECT_NE(optionOf(routine, TaskType::FulfillNeed, OptionStatus::Regrowing), nullptr) << "it waits for the bush to regrow";
	}

	// =============================================================================
	// Stored food: a hungry colonist eats from a box the colony knows or a loose pile it remembers,
	// nearest reachable first among every edible source. The pickup or withdraw puts one meal in its
	// pack and the next decision eats it.
	// =============================================================================

	TEST_F(HungerWalkTest, AnEmptyPackEatsFromAKnownBox) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		const EntityID box = foodBox({4.0F, 0.0F}, 3, {colonist});

		decide(colonist);
		const Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::FulfillNeed);
		EXPECT_EQ(task.needSourceStorageId, static_cast<uint64_t>(box));
		ASSERT_EQ(task.claims.size(), 1);
		EXPECT_EQ(task.claims[0].kind(), ClaimKind::SourceQuantity);
		EXPECT_EQ(task.claims[0].targetKind(), TargetKind::Entity);
		EXPECT_EQ(task.claims[0].quantity(), 1U) << "one meal: a berry takes hunger from 30 past 50";
		EXPECT_NE(task.reason.find("from storage"), std::string::npos) << task.reason;

		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
		EXPECT_EQ(boxBerries(box), 2U);
		EXPECT_EQ(world->getComponent<Inventory>(colonist)->getQuantity("Berry"), 0U) << "it took no more than the meal";
	}

	TEST_F(HungerWalkTest, AnEmptyPackEatsFromAKnownPile) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 5.0F);
		const EntityID pile = berryPile({4.0F, 0.0F}, 5, {colonist});

		decide(colonist);
		ASSERT_EQ(getTask(colonist)->type, TaskType::FulfillNeed);
		EXPECT_EQ(getTask(colonist)->needItemDefNameId, berryId);
		EXPECT_EQ(getTask(colonist)->needQuantity, 2U) << "two berries take hunger from 5 past 50";
		EXPECT_NE(getTask(colonist)->reason.find("from a pile"), std::string::npos) << getTask(colonist)->reason;

		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 3U);
	}

	// Every bush it knows is regrowing, so it waits on them; a box it knows then gains berries, and it
	// eats from the box instead of waiting out the regrowth.
	TEST_F(HungerWalkTest, ABoxThatGainsFoodEndsTheWaitOnRegrowingBushes) {
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		rememberBush(colonist, bushPos);
		cut.push_back(bushPos);
		const EntityID box = foodBox({-5.0F, 0.0F}, 0, {colonist});

		decide(colonist);
		ASSERT_NE(optionOf(colonist, TaskType::FulfillNeed, OptionStatus::Regrowing), nullptr);
		ASSERT_TRUE(world->getComponent<SourceReachMemo>(colonist)->foodWait.has_value()) << "it waits on the bush";

		world->getComponent<Inventory>(box)->addItem("Berry", 2);
		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
		EXPECT_EQ(boxBerries(box), 1U);
	}

	// One search over every kind, nearest reachable first: the bush, then (once it is cut) the box, then
	// (once the box is empty) the pile.
	TEST_F(HungerWalkTest, TheNearestReachableFoodWinsWhateverItsKind) {
		const glm::vec2 bushPos{3.0F, 0.0F};
		const glm::vec2 pilePos{-7.0F, 0.0F};
		const EntityID	colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		rememberBush(colonist, bushPos);
		const EntityID box = foodBox({0.0F, 5.0F}, 2, {colonist});
		berryPile(pilePos, 2, {colonist});

		decide(colonist);
		EXPECT_EQ(getTask(colonist)->needSourceKey, hashWorldEntity(bushPos, bushId));
		EXPECT_EQ(getTask(colonist)->needItemDefNameId, 0U);

		cut.push_back(bushPos);
		decide(colonist);
		EXPECT_EQ(getTask(colonist)->needSourceStorageId, static_cast<uint64_t>(box));

		world->getComponent<Inventory>(box)->removeItem("Berry", 2);
		decide(colonist);
		EXPECT_EQ(getTask(colonist)->needSourceStorageId, 0U);
		EXPECT_EQ(getTask(colonist)->needItemDefNameId, berryId);
		EXPECT_EQ(getTask(colonist)->needSourceKey, hashWorldEntity(pilePos, berryId));
	}

	// One berry left in a box and two routinely hungry colonists: the first to claim it keeps it from the
	// other, who looks elsewhere. The second then turns critical: now it is the more urgent and the berry
	// is its meal. Only one of them eats it.
	TEST_F(HungerWalkTest, TheLastBerryInABoxGoesToTheMoreUrgentEater) {
		const EntityID first = hungryColonist({10.0F, 0.0F}, 30.0F);
		const EntityID second = hungryColonist({-10.0F, 0.0F}, 30.0F);
		const EntityID box = foodBox({0.0F, 0.0F}, 1, {first, second});

		decide(first);
		ASSERT_EQ(getTask(first)->needSourceStorageId, static_cast<uint64_t>(box));
		decide(second);
		EXPECT_NE(getTask(second)->type, TaskType::FulfillNeed) << "the berry is the first's meal";
		const EvaluatedOption* reserved = optionOf(second, TaskType::FulfillNeed, OptionStatus::Reserved);
		ASSERT_NE(reserved, nullptr);
		EXPECT_EQ(reserved->reservedBy, first);

		world->getComponent<NeedsComponent>(second)->get(NeedType::Hunger).value = 5.0F;
		decide(second);
		ASSERT_EQ(getTask(second)->needSourceStorageId, static_cast<uint64_t>(box)) << "the critical eater outranks the routine one";

		ASSERT_TRUE(walkUntil([&] { return boxBerries(box) == 0; }));
		walkUntil([] { return false; }, 200);
		EXPECT_EQ(hunger(first), 30.0F) << "the routine eater gave the berry up";
		EXPECT_EQ(hunger(second), 35.0F) << "the critical one ate it";
	}

	// A hauler has claimed a pile of berries for a storage box; a starving colonist takes them anyway (a
	// work claim never holds food against a hungry colonist). The hauler arrives to nothing and its haul
	// ends.
	TEST_F(HungerWalkTest, AStarvingColonistTakesBerriesAHaulerClaimed) {
		const glm::vec2 storePos{0.0F, 12.0F};
		const EntityID	store = world->createEntity();
		world->addComponent<Position>(store, Position{storePos});
		world->addComponent<Inventory>(store, Inventory::createForStorage());
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Berry", .category = assets::ItemCategory::RawMaterial, .priority = StoragePriority::Medium});
		world->addComponent<StorageConfiguration>(store, std::move(config));
		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = store;
		goal.destinationPosition = storePos;
		goal.acceptedDefNameIds = {berryId};
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		const EntityID hauler = createColonist({9.0F, 0.0F});
		const EntityID starving = hungryColonist({0.0F, 0.0F}, 5.0F);
		const EntityID pile = berryPile({3.0F, 0.0F}, 2, {hauler, starving});

		decide(hauler);
		ASSERT_EQ(getTask(hauler)->type, TaskType::Haul);
		decide(starving);
		ASSERT_EQ(getTask(starving)->type, TaskType::FulfillNeed);
		EXPECT_EQ(getTask(starving)->needQuantity, 2U) << "the hauler's claim doesn't hold the berries";

		bool		haulEnded = false;
		const auto	noteHauler = [&] { haulEnded |= getTask(hauler)->type != TaskType::Haul; };
		ASSERT_TRUE(walkUntil([&] { return hunger(starving) >= 50.0F; }, noteHauler));
		walkUntil([&] { return haulEnded; }, noteHauler, 300);
		EXPECT_TRUE(haulEnded);
		EXPECT_FALSE(world->isAlive(pile) && world->getComponent<ResourceStack>(pile)->quantity > 0) << "the pile was eaten";
		EXPECT_EQ(world->getComponent<Inventory>(store)->getQuantity("Berry"), 0U);
		EXPECT_EQ(world->getComponent<Inventory>(hauler)->getQuantity("Berry"), 0U) << "the hauler came away with nothing";
	}

	// A pack with no room for a berry still eats: the colonist puts away what it carries that is not
	// food, then takes its meal.
	TEST_F(HungerWalkTest, AFullPackStillEatsStoredFood) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		auto&		   pack = *world->getComponent<Inventory>(colonist);
		pack.maxCapacity = 1;
		pack.addItem("Stone", 3);
		const auto& registry = assets::AssetRegistry::Get();
		ASSERT_EQ(ecs::pickupUnitsThatFit(pack, registry, "Berry", ecs::carriedCargoMassKg(pack, registry)), 0U);
		berryPile({4.0F, 0.0F}, 3, {colonist});

		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
		EXPECT_EQ(world->getComponent<Inventory>(colonist)->getQuantity("Stone"), 0U) << "the stone was put away";
	}

	// The same race decided at the pile: the starving colonist lifts the berries before the hauler gets
	// there, and the hauler's pickup collects nothing, which ends its haul instead of walking an empty
	// pack to the box.
	TEST_F(HungerWalkTest, AHaulerArrivingAfterAHungryColonistAteEndsItsHaul) {
		const glm::vec2 storePos{0.0F, 12.0F};
		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	store = world->createEntity();
		world->addComponent<Position>(store, Position{storePos});
		world->addComponent<Inventory>(store, Inventory::createForStorage());
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Berry", .category = assets::ItemCategory::RawMaterial, .priority = StoragePriority::Medium});
		world->addComponent<StorageConfiguration>(store, std::move(config));
		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = store;
		goal.destinationPosition = storePos;
		goal.acceptedDefNameIds = {berryId};
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		const EntityID hauler = createColonist({9.0F, 0.0F});
		const EntityID starving = hungryColonist({0.0F, 0.0F}, 5.0F);
		berryPile(pilePos, 2, {hauler, starving});
		decide(hauler);
		ASSERT_EQ(getTask(hauler)->type, TaskType::Haul);
		decide(starving);
		ASSERT_EQ(getTask(starving)->needQuantity, 2U);

		act(starving, pilePos);
		EXPECT_EQ(world->getComponent<Inventory>(starving)->getQuantity("Berry"), 2U);
		ASSERT_EQ(getTask(hauler)->type, TaskType::Haul);
		act(hauler, pilePos);
		EXPECT_FALSE(getTask(hauler)->isActive()) << "the haul ends at an empty pile";
		EXPECT_EQ(world->getComponent<Inventory>(hauler)->getQuantity("Berry"), 0U);
	}

	// =============================================================================
	// Sleep releases the hold: a colonist that falls asleep carrying goods for a root keeps them as its
	// suspended record, but while it sleeps the record holds no in-flight or inbound, so another colonist
	// is offered the fetch. Awake, the record is adopted if the root still wants the goods, and discarded
	// otherwise.
	// =============================================================================

	class SleepReleaseTest : public ChainClaimsTest {
	  protected:
		void SetUp() override {
			ChainClaimsTest::SetUp();
			station = world->createEntity();
			world->addComponent<Position>(station, Position{stationPos});
			root = craftRoot(station, stationPos, "Stone", 3);
			childGoal(TaskType::Haul, root, station, stationPos, stoneId);
			sleeper = createColonist({0.0F, 0.0F});
			world->getComponent<Inventory>(sleeper)->addItem("Stone", 3);
		}

		/// The sleeper takes the carry-in, then falls asleep with the stones.
		void carryThenSleep() {
			decide(sleeper);
			ASSERT_EQ(getTask(sleeper)->type, TaskType::Haul);
			ASSERT_TRUE(getTask(sleeper)->haulFromInventory);
			carryIn = getTask(sleeper)->instanceId;
			world->getComponent<NeedsComponent>(sleeper)->get(NeedType::Energy).value = 5.0F;
			decide(sleeper);
			ASSERT_EQ(getTask(sleeper)->needToFulfill, NeedType::Energy);
			ASSERT_TRUE(getTask(sleeper)->suspended.has_value());
			ASSERT_EQ(getTask(sleeper)->suspended->instanceId, carryIn);
		}

		/// The sleep ends as ActionSystem ends it, and the colonist decides awake.
		void wake() {
			getTask(sleeper)->clear();
			world->getComponent<NeedsComponent>(sleeper)->get(NeedType::Energy).value = 100.0F;
			decide(sleeper);
		}

		const glm::vec2 stationPos{0.0F, 6.0F};
		EntityID		station = kInvalidEntity;
		EntityID		sleeper = kInvalidEntity;
		uint64_t		root = 0;
		uint64_t		carryIn = 0;
	};

	TEST_F(SleepReleaseTest, AnotherColonistIsOfferedWhatASleeperCarries) {
		carryThenSleep();
		EXPECT_EQ(indexedClaimsOf(sleeper), 0U) << "asleep, its record holds nothing";
		EXPECT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 0U);

		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	other = createColonist({0.5F, 0.0F});
		remember(other, pilePos, "Stone", assets::CapabilityType::Carryable);
		decide(other);
		ASSERT_EQ(getTask(other)->type, TaskType::Haul);
		EXPECT_EQ(getTask(other)->haulQuantity, 3U) << "the whole need is offered";

		// Awake, the need is covered by the other's fetch: no carry-in is offered and the record goes.
		wake();
		EXPECT_FALSE(getTask(sleeper)->suspended.has_value());
		EXPECT_NE(getTask(sleeper)->instanceId, carryIn);
		EXPECT_EQ(world->getComponent<Inventory>(sleeper)->getQuantity("Stone"), 3U) << "the surplus stays carried";
	}

	TEST_F(SleepReleaseTest, AWokenColonistDeliversWhatIsStillWanted) {
		carryThenSleep();
		wake();
		const Task& task = *getTask(sleeper);
		ASSERT_EQ(task.type, TaskType::Haul);
		EXPECT_TRUE(task.haulFromInventory);
		EXPECT_EQ(task.instanceId, carryIn) << "the carry-in adopted the record";
		EXPECT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 3U);
	}

	// A need other than sleep keeps the record holding, as before.
	TEST_F(SleepReleaseTest, ARecordKeptByEatingStillHolds) {
		decide(sleeper);
		ASSERT_EQ(getTask(sleeper)->type, TaskType::Haul);
		world->getComponent<Inventory>(sleeper)->addItem("Berry", 1);
		world->getComponent<NeedsComponent>(sleeper)->get(NeedType::Hunger).value = 5.0F;
		decide(sleeper);
		ASSERT_EQ(getTask(sleeper)->needToFulfill, NeedType::Hunger);
		ASSERT_TRUE(getTask(sleeper)->suspended.has_value());
		EXPECT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 3U);
	}

} // namespace ecs::test
