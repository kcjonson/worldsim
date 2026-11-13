// The claims each task chain holds at each stage, recorded by AIDecisionSystem at acceptance and
// rewritten by ActionSystem as the chain advances (docs/technical/task-claims.md, Chain claims). Each
// test drives the two systems one at a time, so it can read the claims between stages; every AI
// update here also runs the development-build validator, which aborts on a violation.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "ChainClaims.h"
#include "CollisionSystem.h"
#include "CraftingGoalSystem.h"
#include "MovementSystem.h"
#include "NavigationSystem.h"
#include "PhysicsSystem.h"
#include "PlacedNothing.test.h"
#include "StorageGoalSystem.h"
#include "TimeSystem.h"
#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "../WorldSource.h"
#include "../components/Action.h"
#include "../components/AgentRadius.h"
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
#include "assets/ConstructionRegistry.h"
#include "assets/PriorityConfig.h"
#include "assets/RecipeRegistry.h"

#include <construction/ConstructionWorld.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
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
			world->addComponent<AgentRadius>(entity, AgentRadius{});
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
		EXPECT_EQ(use->target(), task.harvestTargetKey);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->target(), root);
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), 3U) << "amountMax, under the need and the carry";
		EXPECT_EQ(ai->claimIndex().useHolder({.kind = TargetKind::WorldKey, .key = task.harvestTargetKey}, {})->colonist, colonist);

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

	// Idling by gathering food (ranked above a wander) lets go of an armful nobody wants just as a wander
	// does: he does not pick berries with his arms full of wood.
	TEST_F(ChainClaimsTest, AnArmfulNobodyWantsIsPutDownToGatherFood) {
		uint32_t dropped = 0;
		ai->setDropResourceCallback([&](const std::string&, float, float, uint32_t quantity, glm::vec2) { dropped += quantity; });
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, {4.0F, 0.0F}, "BerryBush", assets::CapabilityType::Harvestable);
		auto& inventory = *world->getComponent<Inventory>(colonist);
		ASSERT_EQ(ecs::addArmful(inventory, assets::AssetRegistry::Get(), "Wood", 4), 4U);

		decide(colonist);
		const EvaluatedOption* chosen = world->getComponent<DecisionTrace>(colonist)->getSelected();
		ASSERT_NE(chosen, nullptr);
		ASSERT_TRUE(chosen->gathersFood);
		EXPECT_EQ(dropped, 4U);
		EXPECT_EQ(ecs::availableQuantity(inventory, "Wood"), 0U);
	}

	// Furniture whose placement is cancelled while it is carried is set down where the colonist stands, as
	// the packaged item it is: never dropped as a loose pile of its name, never left marked carried with
	// empty hands. Placed again, it is picked up from there.
	TEST_F(ChainClaimsTest, FurnitureWhosePlacementIsCancelledIsSetDownPackaged) {
		registerItem("Shelf", 2, 12.0F);
		uint32_t loosePiles = 0;
		ai->setDropResourceCallback([&](const std::string&, float, float, uint32_t quantity, glm::vec2) { loosePiles += quantity; });
		const glm::vec2 itemPos{3.0F, 0.0F};
		const EntityID	shelf = world->createEntity();
		world->addComponent<Position>(shelf, Position{itemPos});
		world->addComponent<Appearance>(shelf, Appearance{"Shelf", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world->addComponent<Packaged>(shelf, Packaged{.targetPosition = glm::vec2{8.0F, 0.0F}});
		const EntityID colonist = createColonist({0.0F, 0.0F});
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->type, TaskType::PlacePackaged);
		act(colonist, itemPos);
		auto& inventory = *world->getComponent<Inventory>(colonist);
		ASSERT_EQ(inventory.carryingPackagedEntity, static_cast<uint64_t>(shelf));
		ASSERT_TRUE(world->getComponent<Packaged>(shelf)->beingCarried);

		const glm::vec2 stoodAt{5.0F, 1.0F};
		world->getComponent<Position>(colonist)->value = stoodAt;
		world->getComponent<Packaged>(shelf)->targetPosition.reset();
		decide(colonist);
		EXPECT_EQ(getTask(colonist)->type, TaskType::Wander);
		EXPECT_FALSE(inventory.carryingPackagedEntity.has_value());
		EXPECT_FALSE(inventory.leftHand.has_value());
		EXPECT_FALSE(inventory.rightHand.has_value());
		EXPECT_EQ(loosePiles, 0U) << "no loose pile of a shelf";
		EXPECT_FALSE(world->getComponent<Packaged>(shelf)->beingCarried);
		EXPECT_EQ(world->getComponent<Position>(shelf)->value, stoodAt);

		world->getComponent<Packaged>(shelf)->targetPosition = glm::vec2{8.0F, 0.0F};
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->type, TaskType::PlacePackaged);
		EXPECT_EQ(getTask(colonist)->placeSourcePosition, stoodAt) << "picked up where it was set down";
	}

	// Furniture set down where a site will be built goes to the nearest valid ground off every site
	// instead, by the loose-drop rule the world answers with.
	TEST_F(ChainClaimsTest, FurnitureSetDownOnASiteGoesToGroundOffIt) {
		registerItem("Shelf", 2, 12.0F);
		const FootprintBounds site{{4.0F, 0.0F}, {7.0F, 3.0F}};
		const auto			  onSite = [site](glm::vec2 p) { return site.contains(p); };
		const auto			  isGround = [](glm::vec2 p) { return p.x < 9.0F; };
		ai->setSetDownSpotCallback([&](glm::vec2 at) {
			return resourcePileDrops(1, 1, at, 0.6F, {}, isGround, onSite).spawns.front().first;
		});
		const glm::vec2 itemPos{3.0F, 0.0F};
		const EntityID	shelf = world->createEntity();
		world->addComponent<Position>(shelf, Position{itemPos});
		world->addComponent<Appearance>(shelf, Appearance{"Shelf", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world->addComponent<Packaged>(shelf, Packaged{.targetPosition = glm::vec2{8.0F, 0.0F}});
		const EntityID colonist = createColonist({0.0F, 0.0F});
		decide(colonist);
		act(colonist, itemPos);
		ASSERT_EQ(world->getComponent<Inventory>(colonist)->carryingPackagedEntity, static_cast<uint64_t>(shelf));

		const glm::vec2 stoodAt{5.0F, 1.0F};
		ASSERT_TRUE(onSite(stoodAt));
		world->getComponent<Position>(colonist)->value = stoodAt;
		world->getComponent<Packaged>(shelf)->targetPosition.reset();
		decide(colonist);
		ASSERT_FALSE(world->getComponent<Packaged>(shelf)->beingCarried);
		const glm::vec2 setDown = world->getComponent<Position>(shelf)->value;
		EXPECT_FALSE(onSite(setDown)) << "set down at (" << setDown.x << ", " << setDown.y << ")";
		EXPECT_TRUE(isGround(setDown));
	}

	// A colonist who picks the placement again while still holding the furniture (after a need kept it
	// in hand, a release from control) heads straight for the spot, not back to where he picked it up.
	TEST_F(ChainClaimsTest, ReselectingAPlacementWhileCarryingGoesStraightToTheSpot) {
		registerItem("Shelf", 2, 12.0F);
		const glm::vec2 itemPos{3.0F, 0.0F};
		const glm::vec2 spot{8.0F, 0.0F};
		const EntityID	shelf = world->createEntity();
		world->addComponent<Position>(shelf, Position{itemPos});
		world->addComponent<Appearance>(shelf, Appearance{"Shelf", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world->addComponent<Packaged>(shelf, Packaged{.targetPosition = spot});
		const EntityID colonist = createColonist({0.0F, 0.0F});
		decide(colonist);
		act(colonist, itemPos);
		ASSERT_EQ(world->getComponent<Inventory>(colonist)->carryingPackagedEntity, static_cast<uint64_t>(shelf));

		getTask(colonist)->clear();
		world->getComponent<Position>(colonist)->value = {5.0F, 4.0F};
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->type, TaskType::PlacePackaged);
		EXPECT_EQ(getTask(colonist)->targetPosition, spot);
		EXPECT_EQ(getTask(colonist)->chainStep, 1U);
		EXPECT_EQ(world->getComponent<MovementTarget>(colonist)->target, spot);

		act(colonist, spot);
		EXPECT_FALSE(world->hasComponent<Packaged>(shelf)) << "installed at the spot";
		EXPECT_EQ(world->getComponent<Position>(shelf)->value, spot);
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
		task.haulItemDefNameId = stoneId;
		task.haulGoalId = site.umbrella;
		task.suspended = SuspendedWork{.instanceId = 3, .claimSeq = 3, .rootGoalId = site.umbrella, .itemDefNameId = stoneId};
		ASSERT_TRUE(task.suspended->claims.push(Claim::inbound(static_cast<uint64_t>(site.blueprint), stoneId, 2)));
		ASSERT_TRUE(task.suspended->claims.push(Claim::inFlight(site.umbrella, stoneId, 2)));

		task.haulDestinationId = static_cast<uint64_t>(site.blueprint) + 1;
		EXPECT_FALSE(adoptsSuspended(task));
		task.haulDestinationId = static_cast<uint64_t>(site.blueprint);
		EXPECT_TRUE(adoptsSuspended(task));
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
		ai->setDropResourceCallback([&](const std::string&, float, float, uint32_t, glm::vec2) { ++drops; });

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

	// A colonist interrupted beside a wood box puts its armful away there inside an AI update; a second
	// colonist deciding later in that update sizes its haul to the room the box has left, not what it had
	// when the first colonist decided.
	TEST_F(ChainClaimsTest, GoodsPutAwayInsideAnUpdateShowInLaterDecisions) {
		assets::ActionTypeDef eat;
		eat.defName = "Eat";
		eat.needsHands = true;
		assets::ActionTypeRegistry::Get().registerTestAction(std::move(eat));

		const glm::vec2 boxPos{1.0F, 0.0F};
		const EntityID	box = world->createEntity();
		world->addComponent<Position>(box, Position{boxPos});
		world->addComponent<Appearance>(box, Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world->addComponent<Inventory>(box, Inventory::createForStorage());
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Wood", .category = assets::ItemCategory::RawMaterial, .priority = StoragePriority::Medium, .maxAmount = 10});
		world->addComponent<StorageConfiguration>(box, std::move(config));
		GoalTask umbrella;
		umbrella.type = TaskType::Haul;
		umbrella.owner = GoalOwner::StorageGoalSystem;
		umbrella.destinationEntity = box;
		umbrella.destinationPosition = boxPos;
		umbrella.acceptedDefNameIds = {woodId};
		GoalTaskRegistry::Get().createGoal(std::move(umbrella));

		constructionSite({0.0F, 6.0F}, "Wood", 10);
		const EntityID carrier = createColonist({0.0F, 0.0F});
		auto&		   inventory = *world->getComponent<Inventory>(carrier);
		inventory.leftHand = ItemStack{"Wood", 8};
		inventory.rightHand = ItemStack{"Wood", 8};
		inventory.addItem("Berry", 1);
		decide(carrier);
		ASSERT_EQ(getTask(carrier)->type, TaskType::Haul);

		const glm::vec2 pilePos{0.0F, -5.0F};
		const EntityID	pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, woodId, pilePos, 10);
		const EntityID hauler = createColonist({0.0F, -3.0F});
		remember(hauler, pilePos, "Wood", assets::CapabilityType::Carryable);

		world->getComponent<NeedsComponent>(carrier)->get(NeedType::Hunger).value = 5.0F;
		decideAll({carrier, hauler});
		ASSERT_EQ(getTask(carrier)->type, TaskType::FulfillNeed);
		ASSERT_EQ(world->getComponent<Inventory>(box)->getQuantity("Wood"), 8U) << "put away in the box within reach";
		const EvaluatedOption* toBox = nullptr;
		for (const EvaluatedOption& option : world->getComponent<DecisionTrace>(hauler)->options) {
			if (option.taskType == TaskType::Haul && option.haulDestinationId == static_cast<uint64_t>(box)) {
				toBox = &option;
			}
		}
		ASSERT_NE(toBox, nullptr);
		EXPECT_EQ(toBox->haulQuantity, 2U) << "the box takes ten, and holds eight now";
	}

	// A full armful of wood nobody wants fills the colonist's carry, so every haul reads no room. It puts
	// the armful down before it wanders, takes the stone haul at its next decision, and the wood pile is
	// hauled once a box wants wood.
	TEST_F(ChainClaimsTest, AnArmfulNobodyWantsIsPutDown) {
		struct Drop {
			glm::vec2 at;
			uint32_t  quantity;
		};
		std::vector<Drop> drops;
		ai->setDropResourceCallback([&](const std::string&, float x, float y, uint32_t quantity, glm::vec2) { drops.push_back({{x, y}, quantity}); });
		const glm::vec2 boxPos{0.0F, 10.0F};
		const EntityID	box = makeBox(boxPos, StoragePriority::Medium);
		storageGoal(box, boxPos);
		const glm::vec2 stonePos{5.0F, 0.0F};
		const EntityID	stone = world->createEntity();
		world->addComponent<Position>(stone, Position{stonePos});
		makeLoosePile(*world, stone, stoneId, stonePos, 4);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, stonePos, "Stone", assets::CapabilityType::Carryable);
		auto& inventory = *world->getComponent<Inventory>(colonist);
		ASSERT_EQ(ecs::addArmful(inventory, assets::AssetRegistry::Get(), "Wood", 14), 14U);

		decide(colonist);
		EXPECT_EQ(getTask(colonist)->type, TaskType::Wander);
		ASSERT_EQ(drops.size(), 1U);
		EXPECT_EQ(drops.front().quantity, 14U);
		EXPECT_EQ(ecs::availableQuantity(inventory, "Wood"), 0U);

		getTask(colonist)->clear();
		decide(colonist);
		EXPECT_EQ(getTask(colonist)->type, TaskType::Haul);
		EXPECT_EQ(assets::AssetRegistry::Get().getDefName(getTask(colonist)->haulItemDefNameId), "Stone") << "free hands take other work";

		const EntityID wood = world->createEntity();
		world->addComponent<Position>(wood, Position{drops.front().at});
		makeLoosePile(*world, wood, woodId, drops.front().at, drops.front().quantity);
		const EntityID other = createColonist({0.0F, 1.0F});
		remember(other, drops.front().at, "Wood", assets::CapabilityType::Carryable);
		world->getComponent<StorageConfiguration>(box)->addRule(
			StorageRule{.defName = "Wood", .category = assets::ItemCategory::RawMaterial, .priority = StoragePriority::Medium}
		);
		GoalTaskRegistry::Get().updateGoal(GoalTaskRegistry::Get().getGoalByDestination(box)->id, [&](GoalTask& goal) {
			goal.acceptedDefNameIds.push_back(woodId);
		});
		decide(other);
		EXPECT_EQ(getTask(other)->type, TaskType::Haul);
		EXPECT_EQ(assets::AssetRegistry::Get().getDefName(getTask(other)->haulItemDefNameId), "Wood") << "the pile is hauled once a root wants it";
	}

	// Walking an armful of wood to a box, he is offered a site that wants it (a higher tier). The carry-in
	// to the site delivers what he holds, so it needs no free hands: the wood goes with him, not onto the
	// ground to be fetched again.
	TEST_F(ChainClaimsTest, AnArmfulBoundForABoxGoesToASiteWithoutBeingDropped) {
		assets::ActionTypeDef pickup;
		pickup.defName = "Pickup";
		pickup.needsHands = true;
		assets::ActionTypeRegistry::Get().registerTestAction(std::move(pickup));
		uint32_t dropped = 0;
		ai->setDropResourceCallback([&](const std::string&, float, float, uint32_t quantity, glm::vec2) { dropped += quantity; });
		const glm::vec2 boxPos{0.0F, 10.0F};
		const EntityID	box = makeBox(boxPos, StoragePriority::Medium);
		world->getComponent<StorageConfiguration>(box)->addRule(
			StorageRule{.defName = "Wood", .category = assets::ItemCategory::RawMaterial, .priority = StoragePriority::Medium}
		);
		const uint64_t umbrella = storageGoal(box, boxPos);
		GoalTaskRegistry::Get().updateGoal(umbrella, [&](GoalTask& goal) { goal.acceptedDefNameIds.push_back(woodId); });
		const EntityID colonist = createColonist({0.0F, 0.0F});
		auto&		   inventory = *world->getComponent<Inventory>(colonist);
		ASSERT_EQ(ecs::addArmful(inventory, assets::AssetRegistry::Get(), "Wood", 14), 14U);
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->type, TaskType::Haul);
		ASSERT_EQ(getTask(colonist)->haulGoalId, umbrella);

		const glm::vec2 sitePos{10.0F, 0.0F};
		const Site		site = constructionSite(sitePos, "Wood", 14);
		decide(colonist);
		const Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		EXPECT_EQ(task.haulGoalId, site.umbrella);
		EXPECT_TRUE(task.haulFromInventory);
		EXPECT_EQ(ecs::availableQuantity(inventory, "Wood"), 14U);
		EXPECT_EQ(dropped, 0U);

		act(colonist, sitePos);
		EXPECT_EQ(world->getComponent<StructureBlueprint>(site.blueprint)->remaining("Wood"), 0U);
	}

	// A haul claims of a pile what he can lift now, not what an empty-handed colonist could: the rest stays
	// free for others from the moment he accepts.
	TEST_F(ChainClaimsTest, AHaulClaimsWhatHeCanLiftNow) {
		registerItem("Rock", 1, 30.0F);
		const glm::vec2 boxPos{0.0F, 10.0F};
		const EntityID	box = makeBox(boxPos, StoragePriority::Medium);
		storageGoal(box, boxPos);
		const glm::vec2 stonePos{5.0F, 0.0F};
		const EntityID	stone = world->createEntity();
		world->addComponent<Position>(stone, Position{stonePos});
		makeLoosePile(*world, stone, stoneId, stonePos, 40);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, stonePos, "Stone", assets::CapabilityType::Carryable);
		auto& inventory = *world->getComponent<Inventory>(colonist);
		inventory.addItem("Rock", 1);
		const auto&	   registry = assets::AssetRegistry::Get();
		const uint32_t liftNow = ecs::pickupUnitsThatFit(inventory, registry, "Stone", ecs::carriedCargoMassKg(inventory, registry));
		ASSERT_GT(liftNow, 0U);
		ASSERT_LT(liftNow, ecs::cargoUnitsPerTrip(registry, "Stone", inventory.carryCapacityKg));

		decide(colonist);
		const Task& task = *getTask(colonist);
		ASSERT_EQ(task.type, TaskType::Haul);
		EXPECT_EQ(task.haulQuantity, liftNow);
		const auto source = claimOf(task, ClaimKind::SourceQuantity);
		ASSERT_TRUE(source.has_value());
		EXPECT_EQ(source->quantity(), liftNow);
	}

	// Loose stone lies beside a Low box and far from a High one that also wants it: he carries it to the
	// High box, so it is never stocked into the Low box only to be pulled out again.
	TEST_F(ChainClaimsTest, LooseStockGoesToTheHigherPriorityBoxFirst) {
		const glm::vec2 lowPos{4.0F, 0.0F};
		const glm::vec2 highPos{-40.0F, 0.0F};
		const EntityID	low = makeBox(lowPos, StoragePriority::Low);
		const EntityID	high = makeBox(highPos, StoragePriority::High);
		storageGoal(low, lowPos);
		storageGoal(high, highPos);
		const glm::vec2 stonePos{5.0F, 0.0F};
		const EntityID	stone = world->createEntity();
		world->addComponent<Position>(stone, Position{stonePos});
		makeLoosePile(*world, stone, stoneId, stonePos, 4);
		const EntityID colonist = createColonist({6.0F, 0.0F});
		remember(colonist, stonePos, "Stone", assets::CapabilityType::Carryable);

		decide(colonist);
		ASSERT_EQ(getTask(colonist)->type, TaskType::Haul);
		EXPECT_EQ(getTask(colonist)->haulDestinationId, static_cast<uint64_t>(high));
	}

	// The task panel lists a storage goal only while a colonist is offered work toward it: a box that
	// wants stone when no loose stone is known, or one with room for a category nobody has any of, has
	// nothing to do.
	TEST_F(ChainClaimsTest, AStorageGoalHasWorkOnlyWhileAColonistIsOfferedSome) {
		const glm::vec2 boxPos{0.0F, 10.0F};
		const EntityID	box = makeBox(boxPos, StoragePriority::Medium);
		const uint64_t	umbrella = storageGoal(box, boxPos);
		const EntityID	furnitureBox = makeBox({10.0F, 10.0F}, StoragePriority::Medium);
		world->getComponent<StorageConfiguration>(furnitureBox)->rules = StorageConfiguration::createAcceptEverything().rules;
		GoalTask anyFurniture;
		anyFurniture.type = TaskType::Haul;
		anyFurniture.owner = GoalOwner::StorageGoalSystem;
		anyFurniture.destinationEntity = furnitureBox;
		anyFurniture.destinationPosition = {10.0F, 10.0F};
		anyFurniture.acceptedCategory = assets::ItemCategory::Furniture;
		const uint64_t furnitureUmbrella = GoalTaskRegistry::Get().createGoal(std::move(anyFurniture));
		const auto	   hasWork = [&](uint64_t goal) {
			return goalHasWork(*GoalTaskRegistry::Get().getGoal(goal), goalsWithOfferedWork(*world));
		};
		const EntityID colonist = createColonist({0.0F, 0.0F});

		decide(colonist);
		EXPECT_FALSE(hasWork(umbrella)) << "no stone is known";
		EXPECT_FALSE(hasWork(furnitureUmbrella));

		const glm::vec2 stonePos{5.0F, 0.0F};
		const EntityID	stone = world->createEntity();
		world->addComponent<Position>(stone, Position{stonePos});
		makeLoosePile(*world, stone, stoneId, stonePos, 4);
		remember(colonist, stonePos, "Stone", assets::CapabilityType::Carryable);
		decide(colonist);
		EXPECT_TRUE(hasWork(umbrella));
		EXPECT_FALSE(hasWork(furnitureUmbrella)) << "nobody knows any furniture";

		ai->enterControl(colonist);
		decide(colonist);
		EXPECT_FALSE(hasWork(umbrella)) << "a controlled colonist is offered nothing";
		ai->releaseControl(colonist);
		decide(colonist);
		EXPECT_TRUE(hasWork(umbrella)) << "offered again once released";

		getTask(colonist)->clear();
		world->getComponent<StorageConfiguration>(box)->rules.front().maxAmount = 3;
		world->getComponent<Inventory>(box)->addItem("Stone", 3);
		decide(colonist);
		EXPECT_FALSE(hasWork(umbrella)) << "the box is at its max";
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
			EXPECT_EQ(task.haulDestinationId, static_cast<uint64_t>(site.blueprint));
		}
		const auto& options = world->getComponent<DecisionTrace>(colonist)->options;
		EXPECT_TRUE(std::any_of(options.begin(), options.end(), [](const EvaluatedOption& option) {
			return option.taskType == TaskType::Harvest && option.status == OptionStatus::NoRoom;
		})) << "the cut shows why it is not taken";
	}

	// A full pack and free hands: wood rides in the hands, so the cut is offered. Its sources are listed by
	// the same room rule the pickup and the evaluator use, not by the pack's free slots.
	TEST_F(ChainClaimsTest, AFullPackStillCutsWoodIntoFreeHands) {
		registerHarvestable("WoodTree", "Wood", 2, 3);
		const glm::vec2 sitePos{0.0F, 10.0F};
		const Site		site = constructionSite(sitePos, "Wood", 10);
		GoalTask		cut;
		cut.type = TaskType::Harvest;
		cut.owner = GoalOwner::ConstructionGoalSystem;
		cut.destinationEntity = site.blueprint;
		cut.destinationPosition = sitePos;
		cut.yieldDefNameId = woodId;
		cut.parentGoalId = site.umbrella;
		GoalTaskRegistry::Get().createGoal(std::move(cut));
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, {2.0F, 0.0F}, "WoodTree", assets::CapabilityType::Harvestable);
		auto& inventory = *world->getComponent<Inventory>(colonist);
		inventory.maxCapacity = 1;
		inventory.addItem("Stick", 1);
		ASSERT_EQ(inventory.addableCount("Wood"), 0U) << "no pack slot";

		decide(colonist);
		EXPECT_EQ(getTask(colonist)->type, TaskType::Harvest);
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

	/// A colonist between two cuts of a gathering for `site`: its task is the next cut, toward a tree it
	/// has not reached, and it holds an armful of three wood under that cut's cargo claims.
	class ArmfulBetweenCutsTest : public ChainClaimsTest {
	  protected:
		void SetUp() override {
			ChainClaimsTest::SetUp();
			ai->setDropResourceCallback([this](const std::string&, float, float, uint32_t quantity, glm::vec2) { dropped += quantity; });
			gathered = constructionSite({0.0F, 30.0F}, "Wood", 10);
			colonist = createColonist({0.0F, 0.0F});
			auto& inventory = *world->getComponent<Inventory>(colonist);
			ASSERT_EQ(ecs::addArmful(inventory, assets::AssetRegistry::Get(), "Wood", 3), 3U);
			Task& task = *getTask(colonist);
			task.type = TaskType::Harvest;
			task.state = TaskState::Moving;
			task.targetPosition = {50.0F, 50.0F};
			task.harvestTargetKey = 1;
			task.harvestYieldDefNameId = woodId;
			task.priorityTier = 4;
			task.instanceId = GoalTaskRegistry::Get().allocateClaimSeq();
			task.claimSeq = task.instanceId;
			ASSERT_TRUE(task.claims.push(Claim::inbound(static_cast<uint64_t>(gathered.blueprint), woodId, 3)));
			ASSERT_TRUE(task.claims.push(Claim::inFlight(gathered.umbrella, woodId, 3)));
		}

		uint32_t heldWood() { return ecs::availableQuantity(*world->getComponent<Inventory>(colonist), "Wood"); }

		Site	 gathered{};
		EntityID colonist = kInvalidEntity;
		uint32_t dropped = 0;
	};

	// Another site nearer by wants the same wood (its cut, or the armful carried there) and is picked: no
	// leg of this gathering, so the colonist keeps its armful and its leg rather than dropping the wood.
	TEST_F(ArmfulBetweenCutsTest, AnotherSitesPickDoesNotDropTheArmful) {
		registerHarvestable("Oak", "Wood", 2, 2);
		const Site other = constructionSite({3.0F, 0.0F}, "Wood", 10);
		GoalTask cut;
		cut.type = TaskType::Harvest;
		cut.owner = GoalOwner::ConstructionGoalSystem;
		cut.destinationEntity = other.blueprint;
		cut.destinationPosition = {3.0F, 0.0F};
		cut.yieldDefNameId = woodId;
		cut.parentGoalId = other.umbrella;
		GoalTaskRegistry::Get().createGoal(std::move(cut));
		remember(colonist, {2.0F, 0.0F}, "Oak", assets::CapabilityType::Harvestable);

		decide(colonist);
		const EvaluatedOption* chosen = world->getComponent<DecisionTrace>(colonist)->getSelected();
		ASSERT_NE(chosen, nullptr);
		ASSERT_TRUE(chosen->taskType == TaskType::Harvest || chosen->haulGoalId == other.umbrella) << "a pick for the other site";
		EXPECT_EQ(getTask(colonist)->targetPosition, glm::vec2(50.0F, 50.0F)) << "the leg it was on is kept";
		EXPECT_EQ(heldWood(), 3U);
		EXPECT_EQ(dropped, 0U);
	}

	// The root is gone and nothing else is on offer: no delivery is left to protect, so the cut is let go
	// and the armful nobody wants is put down rather than walked off with on a wander.
	TEST_F(ArmfulBetweenCutsTest, ALostRootPutsTheArmfulDown) {
		GoalTaskRegistry::Get().removeGoal(gathered.umbrella);
		decide(colonist);
		EXPECT_EQ(getTask(colonist)->type, TaskType::Wander);
		EXPECT_EQ(heldWood(), 0U);
		EXPECT_EQ(dropped, 3U);
	}

	// A top-up cut that arrives to find its tree gone from memory starts nothing and ends there, holding
	// no Arrived task the two-hand guard could keep; the armful is carried in at the next decision.
	TEST_F(ArmfulBetweenCutsTest, ACutWhoseTreeIsGoneEndsAndTheArmfulIsCarriedIn) {
		act(colonist, {50.0F, 50.0F});
		EXPECT_FALSE(getTask(colonist)->isActive());
		EXPECT_EQ(heldWood(), 3U);

		decide(colonist);
		const Task& task = *getTask(colonist);
		EXPECT_EQ(task.type, TaskType::Haul);
		EXPECT_TRUE(task.haulFromInventory);
		EXPECT_EQ(task.haulGoalId, gathered.umbrella);
		EXPECT_EQ(dropped, 0U);
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
			actions->setDropResourceCallback([this](const std::string& defName, float x, float y, uint32_t quantity, glm::vec2) {
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

		/// Run 0.1 s frames, landing drops, until `done` holds.
		template <typename Done> bool run(Done done, int maxFrames = 4000) {
			for (int frame = 0; frame < maxFrames; ++frame) {
				world->update(0.1F);
				landDrops();
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
	// Once the colonist has chosen to carry its cut in, a reed that becomes ready on the way does not turn
	// it back: the chosen carry-in outranks a top-up cut for the same site.
	TEST_F(ChainWalkTest, AChosenCarryInIsNotTurnedBackByAReadySource) {
		const Site		site = fibreSite({0.0F, 12.0F}, 20);
		const glm::vec2 later{4.0F, 0.0F};
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		rememberReeds(colonist, {{2.0F, 0.0F}, later});
		cut.push_back(later); // regrowing until the colonist is on its way
		ASSERT_TRUE(walkUntil(site, colonist, [&] {
			const Task& task = *getTask(colonist);
			return task.type == TaskType::Haul && task.deliveryChosen;
		}));
		std::erase(cut, later);
		decide(colonist);
		const Task& task = *getTask(colonist);
		EXPECT_EQ(task.type, TaskType::Haul);
		EXPECT_TRUE(task.haulFromInventory);
		EXPECT_EQ(task.targetPosition, glm::vec2(0.0F, 12.0F));
		const EvaluatedOption* ready = optionOf(colonist, TaskType::Harvest, OptionStatus::Available);
		ASSERT_NE(ready, nullptr) << "the ready reed is on offer";
		EXPECT_EQ(ready->gatheringRank, 0) << "but no longer as a leg of the trip";
	}

	// A need between two cuts keeps the cut's goods as its suspended record, and the next cut adopts them;
	// taking the colonist under player control then leaves no claim, and the goods with the colonist.
	TEST_F(ChainWalkTest, ANeedBetweenCutsKeepsTheGoodsForTheNextCut) {
		const Site	   site = fibreSite({0.0F, 12.0F}, 20);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		rememberReeds(colonist, {{2.0F, 0.0F}, {4.0F, 0.0F}, {6.0F, 0.0F}});
		ASSERT_TRUE(walkUntil(site, colonist, [&] {
			const Task& task = *getTask(colonist);
			return task.type == TaskType::Harvest && claimOf(task, ClaimKind::Inbound).has_value();
		}));
		Task&		   task = *getTask(colonist);
		const uint64_t gathering = task.instanceId;

		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::FulfillNeed);
		ASSERT_TRUE(task.suspended.has_value());
		EXPECT_EQ(task.suspended->instanceId, gathering);
		EXPECT_EQ(cargoOnClaims(task.suspended->claims, site.umbrella, fibreId), 3U) << "the cut's goods, not its uncut yield";
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, fibreId, {}), 3U);

		world->getComponent<Inventory>(colonist)->removeItem("Berry", 1);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 100.0F;
		task.clear();
		decide(colonist);
		ASSERT_EQ(task.type, TaskType::Harvest);
		EXPECT_EQ(task.instanceId, gathering) << "the next cut adopts the record";
		EXPECT_FALSE(task.suspended.has_value());
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->quantity(), 3U);

		ai->enterControl(colonist);
		EXPECT_EQ(indexedClaimsOf(colonist), 0U);
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, fibreId, {}), 0U);
		EXPECT_EQ(carriedFibre(colonist), 3U) << "the goods stay with the colonist";
	}

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
		ASSERT_EQ(getTask(other)->harvestTargetKey, hashWorldEntity(taken, assets::AssetRegistry::Get().getDefNameId("Reed")));
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

	// Two colonists gather for one site and their cuts interleave until its need is met exactly: what they
	// claim together never passes the need, no reed is cut beyond it, and every unit cut is delivered.
	TEST_F(ChainWalkTest, TwoGatherersMeetANeedExactly) {
		const Site					 site = fibreSite({0.0F, 12.0F}, 12);
		const EntityID				 first = createColonist({0.0F, 0.0F});
		const EntityID				 second = createColonist({1.0F, 0.0F});
		const std::vector<glm::vec2> reeds{{2.0F, 0.0F}, {3.0F, 1.0F}, {4.0F, 0.0F}, {5.0F, 1.0F}, {-2.0F, 0.0F}, {-3.0F, 1.0F}, {-4.0F, 0.0F}};
		for (const EntityID colonist : {first, second}) {
			for (const glm::vec2 reed : reeds) {
				remember(colonist, reed, "Reed", assets::CapabilityType::Harvestable);
			}
		}

		std::array<int, 2>		cutsBy{};
		std::array<uint32_t, 2> lastCarried{};
		uint32_t				mostInFlight = 0;
		const auto				settled = [&] {
			 return delivered(site) == 12 && carriedFibre(first) + carriedFibre(second) == 0 &&
					getTask(first)->type != TaskType::Harvest && getTask(second)->type != TaskType::Harvest;
		};
		bool done = false;
		for (int frame = 0; frame < 6000 && !done; ++frame) {
			world->update(0.1F);
			landDrops();
			const std::array<EntityID, 2> both{first, second};
			for (size_t i = 0; i < both.size(); ++i) {
				const uint32_t now = carriedFibre(both[i]);
				cutsBy[i] += now > lastCarried[i] ? 1 : 0;
				lastCarried[i] = now;
			}
			mostInFlight = std::max(mostInFlight, ai->claimIndex().inFlight(site.umbrella, fibreId, {}));
			done = settled();
		}
		ASSERT_TRUE(done) << "delivered " << delivered(site);
		EXPECT_GT(cutsBy[0], 0);
		EXPECT_GT(cutsBy[1], 0) << "both cut";
		EXPECT_LE(mostInFlight, 12U) << "together they never claim past the need";
		EXPECT_EQ(cut.size(), 4U) << "four cuts of three and none beyond";
		EXPECT_EQ(dropped, 0U);
		walkUntil(site, first, [] { return false; }, 300);
		EXPECT_EQ(cut.size(), 4U);
		EXPECT_EQ(delivered(site), 12U);
	}

	// Gathering, his route to the next reed fails. What he carries stays claimed (nobody re-cuts it); the
	// reed and the uncut yield are free, so another colonist takes them, and he delivers what he has
	// rather than standing on the stranded cut.
	TEST_F(ChainWalkTest, ARouteFailureMidGatheringDeliversWhatHeHas) {
		const Site		site = fibreSite({0.0F, 12.0F}, 6);
		const glm::vec2 next{4.0F, 0.0F};
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		rememberReeds(colonist, {{2.0F, 0.0F}, next});
		ASSERT_TRUE(walkUntil(site, colonist, [&] {
			const Task& task = *getTask(colonist);
			return task.type == TaskType::Harvest && claimOf(task, ClaimKind::Inbound).has_value();
		}));
		const uint64_t nextKey = hashWorldEntity(next, assets::AssetRegistry::Get().getDefNameId("Reed"));
		ASSERT_EQ(getTask(colonist)->harvestTargetKey, nextKey);
		const uint64_t gathering = getTask(colonist)->instanceId;

		// No nav in this headless world, so the failed route is set by hand, as failRoute leaves it: stopped,
		// its prompt decision had. The claim index the next decider builds holds only what it carries.
		getTask(colonist)->navState = NavState::CantFindWayTo;
		getTask(colonist)->routeFailureDecided = true;
		getTask(colonist)->timeSinceEvaluation = 0.0F;
		world->getComponent<MovementTarget>(colonist)->active = false;
		const EntityID other = createColonist({5.0F, 0.0F});
		rememberReeds(other, {next});
		decide(other);
		ASSERT_EQ(getTask(colonist)->instanceId, gathering);
		const ClaimExclusion notOther = ClaimExclusion::of(*getTask(other));
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, fibreId, notOther), 3U) << "what he carries, not the uncut yield";
		EXPECT_EQ(ai->claimIndex().inboundItems(site.blueprint, fibreId, notOther), 3U);
		ASSERT_EQ(getTask(other)->type, TaskType::Harvest);
		EXPECT_EQ(getTask(other)->harvestTargetKey, nextKey);

		ASSERT_TRUE(walkUntil(site, colonist, [&] { return delivered(site) == 6 && carriedFibre(colonist) + carriedFibre(other) == 0; }, 1500))
			<< "delivered " << delivered(site);
		EXPECT_EQ(cut.size(), 2U);
		EXPECT_EQ(dropped, 0U);
	}

	// The box a colonist gathers for loses its rules between two cuts, and its goal system retires the
	// root on its own poll. The gathering ends: no further cut, no claim toward the gone root, and the
	// goods he cut stay with him.
	TEST_F(ChainWalkTest, ARootRemovedBetweenCutsEndsTheGathering) {
		world->registerSystem<StorageGoalSystem>();
		const glm::vec2 boxPos{0.0F, 12.0F};
		const EntityID	box = world->createEntity();
		world->addComponent<Position>(box, Position{boxPos});
		world->addComponent<Appearance>(box, Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world->addComponent<Inventory>(box, Inventory::createForStorage());
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Fibre", .category = assets::ItemCategory::RawMaterial, .minAmount = 9, .maxAmount = 9});
		world->addComponent<StorageConfiguration>(box, std::move(config));
		const EntityID colonist = createColonist({0.0F, 0.0F});
		rememberReeds(colonist, {{2.0F, 0.0F}, {4.0F, 0.0F}, {6.0F, 0.0F}, {8.0F, 0.0F}});

		uint64_t   root = 0;
		const auto onTheSecondCut = [&] {
			const Task& task = *getTask(colonist);
			if (task.type != TaskType::Harvest || !claimOf(task, ClaimKind::Inbound).has_value()) {
				return false;
			}
			root = GoalTaskRegistry::Get().rootOf(task.harvestGoalId);
			return true;
		};
		for (int frame = 0; frame < 2000 && !onTheSecondCut(); ++frame) {
			world->update(0.1F);
			landDrops();
		}
		ASSERT_NE(root, 0U) << "he never went on to a second cut";
		world->getComponent<StorageConfiguration>(box)->rules.clear();
		const size_t cutsBefore = cut.size();

		for (int frame = 0; frame < 600; ++frame) {
			world->update(0.1F);
			landDrops();
		}
		EXPECT_EQ(GoalTaskRegistry::Get().getGoal(root), nullptr) << "the goal system retired the root";
		EXPECT_NE(getTask(colonist)->type, TaskType::Harvest);
		EXPECT_LE(cut.size(), cutsBefore + 1) << "at most the cut under way finishes";
		EXPECT_EQ(ai->claimIndex().inFlight(root, fibreId, {}), 0U);
		EXPECT_EQ(indexedClaimsOf(colonist), 0U);
		EXPECT_EQ(world->getComponent<Inventory>(box)->getQuantity("Fibre"), 0U);
		EXPECT_EQ(carriedFibre(colonist) + onTheGround(fibreId), 3U * static_cast<uint32_t>(cut.size())) << "nothing cut is lost";
	}

	// A box one unit short of full asks for many more by its minimum: a reed gives three, so a cut for the
	// box would leave two carried about. It is not taken, and the trace says why.
	TEST_F(ChainWalkTest, NoCutForABoxWithRoomForLessThanItLands) {
		const glm::vec2 boxPos{0.0F, 12.0F};
		const EntityID	box = world->createEntity();
		world->addComponent<Position>(box, Position{boxPos});
		world->addComponent<Appearance>(box, Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		auto store = Inventory::createForStorage();
		store.maxCapacity = 1;
		store.addItem("Fibre", 39);
		world->addComponent<Inventory>(box, std::move(store));
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Fibre", .category = assets::ItemCategory::RawMaterial, .minAmount = 50});
		world->addComponent<StorageConfiguration>(box, std::move(config));
		GoalTask umbrella;
		umbrella.type = TaskType::Haul;
		umbrella.owner = GoalOwner::StorageGoalSystem;
		umbrella.destinationEntity = box;
		umbrella.destinationPosition = boxPos;
		umbrella.acceptedDefNameIds = {fibreId};
		const uint64_t umbrellaId = GoalTaskRegistry::Get().createGoal(std::move(umbrella));
		GoalTask cut;
		cut.type = TaskType::Harvest;
		cut.owner = GoalOwner::StorageGoalSystem;
		cut.destinationEntity = box;
		cut.destinationPosition = boxPos;
		cut.yieldDefNameId = fibreId;
		cut.parentGoalId = umbrellaId;
		GoalTaskRegistry::Get().createGoal(std::move(cut));
		const EntityID colonist = createColonist({0.0F, 0.0F});
		rememberReeds(colonist, {{2.0F, 0.0F}});

		decide(colonist);
		EXPECT_NE(getTask(colonist)->type, TaskType::Harvest);
		EXPECT_NE(optionOf(colonist, TaskType::Harvest, OptionStatus::NoRoom), nullptr);
	}

	// Nine fibre carried toward a site that wants more, room for one: a remembered pile of five is no leg
	// worth walking for one unit, the top-up cut's rule, so he delivers what he has.
	TEST_F(ChainWalkTest, APileIsNoLegForTheLastUnitOfRoom) {
		const Site	   site = fibreSite({0.0F, 12.0F}, 20);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		ASSERT_EQ(world->getComponent<Inventory>(colonist)->addItem("Fibre", 9), 9U);
		Task& task = *getTask(colonist);
		task.type = TaskType::Harvest;
		task.state = TaskState::Moving;
		task.targetPosition = {50.0F, 50.0F};
		task.harvestTargetKey = 1;
		task.harvestYieldDefNameId = fibreId;
		task.priorityTier = 4;
		task.instanceId = GoalTaskRegistry::Get().allocateClaimSeq();
		task.claimSeq = task.instanceId;
		ASSERT_TRUE(task.claims.push(Claim::inbound(static_cast<uint64_t>(site.blueprint), fibreId, 9)));
		ASSERT_TRUE(task.claims.push(Claim::inFlight(site.umbrella, fibreId, 9)));
		const glm::vec2 pilePos{30.0F, 0.0F};
		const EntityID	pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, fibreId, pilePos, 5);
		remember(colonist, pilePos, "Fibre", assets::CapabilityType::Carryable);

		decide(colonist);
		EXPECT_EQ(task.type, TaskType::Haul);
		EXPECT_TRUE(task.haulFromInventory) << "the carry-in, not the pile";
		EXPECT_EQ(task.haulGoalId, site.umbrella);
	}

	// With an armful of wood from his first cut he walks to a second tree, which is felled by someone else
	// on the way: he carries in what he has instead of standing at the stump.
	TEST_F(ChainWalkTest, ATreeGoneOnTheWayToATopUpCutSendsTheArmfulIn) {
		assets::AssetDefinition pine;
		pine.defName = "Pine";
		pine.label = "Pine";
		pine.capabilities.harvestable = assets::HarvestableCapability{};
		pine.capabilities.harvestable->yieldDefName = "Wood";
		pine.capabilities.harvestable->amountMin = 3;
		pine.capabilities.harvestable->amountMax = 3;
		pine.capabilities.harvestable->durability = 1.0F;
		pine.capabilities.harvestable->regrowthTime = 600.0F;
		assets::AssetRegistry::Get().registerTestDefinition(std::move(pine));
		const Site site = constructionSite({0.0F, 12.0F}, "Wood", 10);
		childOf(site, {0.0F, 12.0F}, woodId);
		const EntityID	colonist = createColonist({0.0F, 0.0F});
		const glm::vec2 second{6.0F, 0.0F};
		remember(colonist, {2.0F, 0.0F}, "Pine", assets::CapabilityType::Harvestable);
		remember(colonist, second, "Pine", assets::CapabilityType::Harvestable);
		const auto heldWood = [&] { return ecs::availableQuantity(*world->getComponent<Inventory>(colonist), "Wood"); };

		ASSERT_TRUE(run([&] {
			const Task& task = *getTask(colonist);
			return heldWood() == 3 && task.type == TaskType::Harvest && task.state == TaskState::Moving &&
				   glm::distance(task.targetPosition, second) < 1.0F;
		})) << "he never went on to the second tree";
		world->getComponent<Memory>(colonist)->forgetWorldEntity(second, assets::AssetRegistry::Get().getDefNameId("Pine"));
		felled.push_back(second);

		ASSERT_TRUE(run([&] { return delivered(site) == 3; }, 600)) << "delivered " << delivered(site);
		EXPECT_EQ(dropped, 0U);
		EXPECT_EQ(heldWood(), 0U);
	}

	// His cut for a site has landed and a meal interrupts the carry-in, so the goods wait in his suspended
	// record. Fed, he picks a clear of the same site at his feet over the walk to the site: a clear brings
	// nothing toward the need, so it never takes the goods over as the next leg (no handover, no adopted
	// record). It is a task of its own, and the goods are carried in after.
	TEST_F(ChainWalkTest, ABuildSiteClearIsNeverTheNextLegOfAGathering) {
		const Site	   site = fibreSite({0.0F, 30.0F}, 3);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		rememberReeds(colonist, {{1.0F, 0.0F}});
		// A skilled cutter, so a clear at his feet outscores the long walk to the site.
		world->addComponent<Skills>(colonist, Skills{}).setLevel("Farming", 10.0F);
		ASSERT_TRUE(run([&] { return getTask(colonist)->type == TaskType::Haul && carriedFibre(colonist) == 3U; }));
		const uint64_t gathering = getTask(colonist)->instanceId;

		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->type, TaskType::FulfillNeed);
		ASSERT_TRUE(getTask(colonist)->suspended.has_value());
		ASSERT_EQ(getTask(colonist)->suspended->instanceId, gathering);

		// A slow plant, so the clear lasts long enough to be seen. It yields the gathered item, as the cut
		// did, so only the clear rule keeps it from taking the goods on.
		assets::AssetDefinition thistle;
		thistle.defName = "Thistle";
		thistle.label = "Thistle";
		thistle.capabilities.harvestable = assets::HarvestableCapability{};
		thistle.capabilities.harvestable->yieldDefName = "Fibre";
		thistle.capabilities.harvestable->amountMin = 1;
		thistle.capabilities.harvestable->amountMax = 1;
		thistle.capabilities.harvestable->durability = 30.0F;
		thistle.capabilities.harvestable->destructive = true;
		assets::AssetRegistry::Get().registerTestDefinition(std::move(thistle));
		const glm::vec2 weed = world->getComponent<Position>(colonist)->value + glm::vec2{0.4F, 0.4F};
		GoalTask		clear;
		clear.type = TaskType::Harvest;
		clear.owner = GoalOwner::ConstructionGoalSystem;
		clear.destinationEntity = site.blueprint;
		clear.destinationPosition = {0.0F, 30.0F};
		clear.clearFootprint = FootprintBounds{.min = weed - 0.2F, .max = weed + 0.2F};
		clear.parentGoalId = site.umbrella;
		const uint64_t clearGoal = GoalTaskRegistry::Get().createGoal(std::move(clear));
		remember(colonist, weed, "Thistle", assets::CapabilityType::Harvestable);

		bool cleared = false;
		bool clearTookTheGoods = false;
		ASSERT_TRUE(run([&] {
			const Task& task = *getTask(colonist);
			if (!cleared && task.type == TaskType::Harvest && task.harvestGoalId == clearGoal) {
				cleared = true;
				clearTookTheGoods = task.instanceId == gathering || claimOf(task, ClaimKind::Inbound).has_value();
			}
			return delivered(site) == 3U;
		})) << "delivered " << delivered(site);
		ASSERT_TRUE(cleared) << "the clear at his feet was never taken, so this test shows nothing";
		EXPECT_FALSE(clearTookTheGoods) << "the clear is its own task, holding no cargo";
	}

	// Critically hungry in the middle of tier-4 work, with food only in a box: he leaves the work, takes a
	// meal from the box, eats, and goes back to finish the work.
	TEST_F(ChainWalkTest, CriticalHungerLeavesWorkForABoxAndReturns) {
		const Site		site = fibreSite({0.0F, 12.0F}, 6);
		const glm::vec2 boxPos{-6.0F, 0.0F};
		const EntityID	box = world->createEntity();
		world->addComponent<Position>(box, Position{boxPos});
		world->addComponent<Appearance>(box, Appearance{"Box", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		auto store = Inventory::createForStorage();
		store.addItem("Berry", 5);
		world->addComponent<Inventory>(box, std::move(store));
		StorageConfiguration config;
		config.addRule(StorageRule{.defName = "Berry", .category = assets::ItemCategory::RawMaterial, .priority = StoragePriority::Medium});
		world->addComponent<StorageConfiguration>(box, std::move(config));
		const EntityID colonist = createColonist({0.0F, 0.0F});
		remember(colonist, boxPos, "Box", assets::CapabilityType::Storage);
		rememberReeds(colonist, {{2.0F, 0.0F}, {4.0F, 0.0F}});
		ASSERT_TRUE(walkUntil(site, colonist, [&] {
			const Task& task = *getTask(colonist);
			return task.type == TaskType::Harvest && claimOf(task, ClaimKind::Inbound).has_value();
		}));
		ASSERT_EQ(getTask(colonist)->priorityTier, 4);
		const uint64_t gathering = getTask(colonist)->instanceId;

		auto& hunger = world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger);
		hunger.value = 5.0F;
		bool	 leftTheWork = false;
		uint64_t resumedBy = 0;
		ASSERT_TRUE(walkUntil(site, colonist, [&] {
			const Task& task = *getTask(colonist);
			leftTheWork |= task.type == TaskType::FulfillNeed && task.needSourceStorageId == static_cast<uint64_t>(box);
			if (leftTheWork && task.type == TaskType::Harvest) {
				resumedBy = task.instanceId;
			}
			return delivered(site) == 6;
		})) << "delivered " << delivered(site);
		EXPECT_TRUE(leftTheWork) << "the critical need took him off tier-4 work to the box";
		EXPECT_LT(world->getComponent<Inventory>(box)->getQuantity("Berry"), 5U);
		EXPECT_GE(hunger.value, 10.0F) << "he ate";
		EXPECT_EQ(resumedBy, gathering) << "the next cut took his gathering back up";
		EXPECT_EQ(cut.size(), 2U);
	}

	// A sleeper carrying stones a craft wants holds no claim on them while it sleeps, so another colonist
	// fetches and delivers the same need from a pile. Awake, the sleeper's record is dropped, nobody
	// delivers past the need, and the stones stay in his pack.
	TEST_F(ChainWalkTest, AWokenSleeperWhoseGoodsWereFetchedByAnotherDropsHisRecord) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 pilePos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stone", 3);
		childGoal(TaskType::Haul, root, station, stationPos, stoneId);
		const EntityID pile = world->createEntity();
		world->addComponent<Position>(pile, Position{pilePos});
		makeLoosePile(*world, pile, stoneId, pilePos, 5);
		const EntityID sleeper = createColonist({-3.0F, 0.0F});
		world->getComponent<Inventory>(sleeper)->addItem("Stone", 3);
		decide(sleeper);
		ASSERT_EQ(getTask(sleeper)->type, TaskType::Haul);
		const uint64_t carryIn = getTask(sleeper)->instanceId;
		world->getComponent<NeedsComponent>(sleeper)->get(NeedType::Energy).value = 5.0F;
		decide(sleeper);
		ASSERT_EQ(getTask(sleeper)->needToFulfill, NeedType::Energy);
		ASSERT_TRUE(getTask(sleeper)->suspended.has_value());

		const EntityID other = createColonist({3.5F, 0.0F});
		remember(other, pilePos, "Stone", assets::CapabilityType::Carryable);
		const auto* store = world->getComponent<Inventory>(station);
		uint32_t	mostStored = 0;
		const auto	energy = [&] { return world->getComponent<NeedsComponent>(sleeper)->get(NeedType::Energy).value; };
		ASSERT_TRUE(run([&] {
			mostStored = std::max(mostStored, store->getQuantity("Stone"));
			return energy() > 5.0F && getTask(sleeper)->needToFulfill != NeedType::Energy;
		})) << "he never woke";
		run([&] {
			mostStored = std::max(mostStored, store->getQuantity("Stone"));
			return false;
		}, 200);
		EXPECT_FALSE(getTask(sleeper)->suspended.has_value()) << "the record is dropped";
		EXPECT_NE(getTask(sleeper)->instanceId, carryIn);
		EXPECT_EQ(mostStored, 3U) << "the other delivered the need, and nobody past it";
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 2U) << "fetched once";
		EXPECT_EQ(world->getComponent<Inventory>(sleeper)->getQuantity("Stone"), 3U) << "his stones stay in his pack";
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

	// A colonist whose route to its claimed bush has failed holds nothing it has not reached while it stays
	// failed, neither its Use of the bush nor the cut's expected yield: another colonist deciding meanwhile
	// is offered the bush and takes it, and the failed colonist, seeing it taken, lets it go.
	TEST_F(ChainClaimsTest, AFailedRouteHoldsNothingItHasNotReached) {
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

		// No nav in this headless world, so the failed route is set by hand, as failRoute leaves it, its
		// prompt decision had.
		getTask(stuck)->navState = NavState::CantFindWayTo;
		getTask(stuck)->routeFailureDecided = true;
		getTask(stuck)->timeSinceEvaluation = 0.0F;
		world->getComponent<MovementTarget>(stuck)->active = false;
		decide(other);
		ASSERT_EQ(getTask(stuck)->instanceId, instance);
		EXPECT_EQ(indexedClaimsOf(stuck), 0U);
		EXPECT_EQ(getTask(other)->type, TaskType::Harvest) << "the stranded colonist's bush is offered";
		EXPECT_EQ(getTask(other)->harvestTargetKey, hashWorldEntity(bushPos, assets::AssetRegistry::Get().getDefNameId("StickBush")));
		decide(stuck);
		EXPECT_NE(getTask(stuck)->type, TaskType::Harvest) << "and the stranded colonist, seeing it taken, lets it go at his next decision";
	}

	// The same stranded colonist, with nobody else wanting the bush: once its route succeeds (here at once:
	// with no nav there is no verdict, so he picks the cut again and is routed), its claims are back and
	// others see the bush taken.
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
		getTask(stuck)->navState = NavState::CantFindWayTo; // as failRoute leaves it: stopped
		world->getComponent<MovementTarget>(stuck)->active = false;
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

	// A hauler past its pickup whose route to the station fails keeps the claims on what it carries: the
	// root still reads those stones as on their way, so another colonist is not offered a fetch for them,
	// and once the route recovers the station gets exactly what it needs.
	TEST_F(ChainClaimsTest, AFailedRoutePastThePickupKeepsItsCargoClaimed) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 pilePos{3.0F, 0.0F};
		const glm::vec2 otherPilePos{-4.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stone", 3);
		childGoal(TaskType::Haul, root, station, stationPos, stoneId);
		for (const glm::vec2 at : {pilePos, otherPilePos}) {
			const EntityID pile = world->createEntity();
			world->addComponent<Position>(pile, Position{at});
			makeLoosePile(*world, pile, stoneId, at, 3);
		}
		const EntityID hauler = createColonist({0.0F, 0.0F});
		remember(hauler, pilePos, "Stone", assets::CapabilityType::Carryable);
		decide(hauler);
		ASSERT_EQ(getTask(hauler)->type, TaskType::Haul);
		act(hauler, pilePos);
		ASSERT_EQ(getTask(hauler)->chainStep, 1U);
		ASSERT_EQ(ecs::availableQuantity(*world->getComponent<Inventory>(hauler), "Stone"), 3U);

		// No nav in this headless world, so the failed route to the station is set by hand, as failRoute
		// leaves it: stopped, its prompt decision had.
		getTask(hauler)->navState = NavState::CantFindWayTo;
		getTask(hauler)->routeFailureDecided = true;
		getTask(hauler)->timeSinceEvaluation = 0.0F;
		world->getComponent<MovementTarget>(hauler)->active = false;
		const EntityID other = createColonist({-3.0F, 0.0F});
		remember(other, otherPilePos, "Stone", assets::CapabilityType::Carryable);
		decide(other);
		EXPECT_NE(getTask(other)->type, TaskType::Haul) << "nobody fetches what the hauler carries";
		EXPECT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 3U) << "the carried stones are still on their way";
		EXPECT_EQ(ai->claimIndex().inboundItems(station, stoneId, {}), 3U);

		// With no nav there is no verdict on the station, so his next decision routes the delivery again.
		decide(hauler);
		ASSERT_EQ(getTask(hauler)->type, TaskType::Haul);
		EXPECT_EQ(getTask(hauler)->navState, NavState::Traveling);
		EXPECT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 3U) << "never more than it carries";
		act(hauler, stationPos);
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Stone"), 3U);
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
		ASSERT_EQ(getTask(worker)->harvestTargetKey, hashWorldEntity(heldBush, bushId));

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

	// Bushes are his only food and his pack has no room for a berry: the cut makes room as a pile or box
	// would, so he eats from the first bush instead of coming back to it while it stands ready.
	TEST_F(HungerWalkTest, AFullPackStillEatsFromABush) {
		const glm::vec2 bushPos{4.0F, 0.0F};
		const EntityID	colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		auto&			pack = *world->getComponent<Inventory>(colonist);
		pack.maxCapacity = 1;
		pack.addItem("Stone", 3);
		rememberBush(colonist, bushPos);
		rememberBush(colonist, {-6.0F, 0.0F});

		decide(colonist);
		ASSERT_EQ(getTask(colonist)->needSourceKey, hashWorldEntity(bushPos, bushId));
		EXPECT_GT(getTask(colonist)->needQuantity, 0U) << "a meal is sized whatever the pack holds";

		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
		EXPECT_EQ(cut, std::vector<glm::vec2>{bushPos}) << "one bush cut, once";
	}

	// Making room to eat frees what limits the pack and no more, and never costs a tool.
	class MakeRoomToEatTest : public HungerWalkTest {
	  protected:
		void SetUp() override {
			HungerWalkTest::SetUp();
			assets::AssetDefinition axe;
			axe.defName = "Axe";
			axe.label = "Axe";
			axe.handsRequired = 1;
			axe.category = assets::ItemCategory::Tool;
			axe.itemProperties = assets::ItemProperties{};
			axe.itemProperties->stackSize = 1;
			axe.itemProperties->massKg = 2.0F;
			assets::AssetRegistry::Get().registerTestDefinition(std::move(axe));
		}

		Inventory& pack(EntityID colonist) { return *world->getComponent<Inventory>(colonist); }
		bool	   holdsAxe(EntityID colonist) {
			  const Inventory& inventory = pack(colonist);
			  return inventory.hasItem("Axe") || std::any_of(inventory.belt.begin(), inventory.belt.end(), [](const auto& slot) {
						 return slot.has_value() && slot->defName == "Axe";
					 }) || inventory.isHolding("Axe");
		}
	};

	// No slot for a berry: the stone's slot is freed, not the axe's.
	TEST_F(MakeRoomToEatTest, AFullPackGivesUpGoodsNotItsTool) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		pack(colonist).maxCapacity = 2;
		pack(colonist).addItem("Stone", 3);
		pack(colonist).addItem("Axe", 1);
		berryPile({4.0F, 0.0F}, 3, {colonist});

		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
		EXPECT_TRUE(pack(colonist).hasItem("Axe")) << "the axe stays packed";
		EXPECT_EQ(pack(colonist).getQuantity("Stone"), 0U);
	}

	// Every slot holds a tool: one moves to the belt, so the meal fits and the tool is kept.
	TEST_F(MakeRoomToEatTest, APackOfToolsMovesOneToTheBelt) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		pack(colonist).maxCapacity = 1;
		pack(colonist).addItem("Axe", 1);
		berryPile({4.0F, 0.0F}, 3, {colonist});

		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
		EXPECT_TRUE(holdsAxe(colonist));
	}

	// Weight is the limit: one stone is enough to free for one berry, so the rest stay, and the axe,
	// which weighs nothing as cargo, is not touched.
	TEST_F(MakeRoomToEatTest, OverweightGivesUpJustEnoughForTheMeal) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		pack(colonist).carryCapacityKg = 5.0F;
		pack(colonist).addItem("Stone", 10);
		pack(colonist).addItem("Axe", 1);
		const auto& registry = assets::AssetRegistry::Get();
		ASSERT_EQ(ecs::pickupUnitsThatFit(pack(colonist), registry, "Berry", ecs::carriedCargoMassKg(pack(colonist), registry)), 0U);
		const EntityID pile = berryPile({4.0F, 0.0F}, 3, {colonist});

		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 2U) << "a meal of one";
		EXPECT_EQ(pack(colonist).getQuantity("Stone"), 9U) << "one stone freed the weight one berry needs";
		EXPECT_TRUE(pack(colonist).hasItem("Axe"));
	}

	// Goods under no claim go first: a critically hungry colonist carrying stones toward a craft keeps
	// them (its suspended record's cargo) and puts away the stick, though the stones are the last stack.
	TEST_F(MakeRoomToEatTest, ClaimedCargoGoesLast) {
		const glm::vec2 stationPos{0.0F, 30.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stone", 3);
		childGoal(TaskType::Haul, root, station, stationPos, stoneId);
		const EntityID colonist = createColonist({0.0F, 0.0F});
		pack(colonist).maxCapacity = 2;
		pack(colonist).addItem("Stick", 1);
		pack(colonist).addItem("Stone", 3);
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->type, TaskType::Haul);
		ASSERT_TRUE(getTask(colonist)->haulFromInventory);
		const EntityID pile = berryPile({4.0F, 0.0F}, 3, {colonist});
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;

		ASSERT_TRUE(walkUntil([&] { return pack(colonist).hasItem("Berry"); }));
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 1U) << "a meal of two";
		EXPECT_EQ(pack(colonist).getQuantity("Stick"), 0U) << "the stick was under no claim";
		EXPECT_EQ(pack(colonist).getQuantity("Stone"), 3U) << "the stones the carry-in claims stay";
		ASSERT_TRUE(walkUntil([&] { return world->getComponent<Inventory>(station)->getQuantity("Stone") == 3U; }))
			<< "and are delivered once it has eaten";
	}

	// Turning critical on the way to a pile, he keeps the same task but its meal grows to what the
	// critical hunger needs, and the share of the pile it claims grows with it.
	TEST_F(HungerWalkTest, AMealGrowsWhenHungerTurnsCriticalOnTheWay) {
		const EntityID	colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		const glm::vec2 pilePos{8.0F, 0.0F};
		berryPile(pilePos, 5, {colonist});
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->needQuantity, 1U);
		const uint64_t instance = getTask(colonist)->instanceId;

		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = 5.0F;
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->instanceId, instance) << "the same meal, kept";
		EXPECT_EQ(getTask(colonist)->priorityTier, 2);
		EXPECT_EQ(getTask(colonist)->needQuantity, 2U) << "two berries take hunger from 5 past 50";
		EXPECT_EQ(ai->claimIndex().sourceClaimed({.kind = TargetKind::WorldKey, .key = hashWorldEntity(pilePos, berryId)}, berryId, {}), 2U);
	}

	// A critically hungry colonist whose only food is regrowing goes on with the work he has, deciding on
	// the re-decide interval rather than every frame, and doesn't freeze: food that turns up in a box is
	// his meal within one interval. (With no work he wanders, and a walking wander decides every frame.)
	TEST_F(HungerWalkTest, ACriticalColonistWithNoFoodDecidesOnTheInterval) {
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	colonist = hungryColonist({0.0F, 0.0F}, 5.0F);
		rememberBush(colonist, bushPos);
		cut.push_back(bushPos);
		const EntityID box = foodBox({-5.0F, 0.0F}, 0, {colonist});
		const glm::vec2 stationPos{0.0F, 6.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		childGoal(TaskType::Harvest, craftRoot(station, stationPos, "Stick", 20), station, stationPos, stickId);
		remember(colonist, {40.0F, 0.0F}, "StickBush", assets::CapabilityType::Harvestable); // far: he is still walking at the end

		constexpr float kFrame = 0.02F;
		constexpr int	kFramesPerInterval = static_cast<int>(0.5F / kFrame) + 2; // kReEvalInterval of frames, the deciding frame, and float rounding
		world->update(kFrame);
		ASSERT_NE(optionOf(colonist, TaskType::FulfillNeed, OptionStatus::Regrowing), nullptr) << "he waits on the bush";
		ASSERT_EQ(getTask(colonist)->type, TaskType::Harvest) << "and works meanwhile";

		const auto* trace = world->getComponent<DecisionTrace>(colonist);
		float		lastDecision = trace->lastEvaluationTime;
		int			decisions = 0;
		for (int frame = 0; frame < 150; ++frame) { // 3 s
			world->update(kFrame);
			if (trace->lastEvaluationTime != lastDecision) {
				lastDecision = trace->lastEvaluationTime;
				++decisions;
			}
		}
		ASSERT_LT(hunger(colonist), 10.0F) << "still critical";
		ASSERT_EQ(getTask(colonist)->state, TaskState::Moving) << "still on his way to the cut";
		EXPECT_GE(decisions, 5) << "he never freezes";
		EXPECT_LE(decisions, 7) << "about one decision per 0.5 s interval over 3 s, not one per frame";

		world->getComponent<Inventory>(box)->addItem("Berry", 2);
		int framesToChoose = 0;
		while (framesToChoose < 10 * kFramesPerInterval && getTask(colonist)->needSourceStorageId != static_cast<uint64_t>(box)) {
			world->update(kFrame);
			++framesToChoose;
		}
		EXPECT_LE(framesToChoose, kFramesPerInterval) << "the box's food is his meal within one interval";
		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }));
	}

	// A box holding two kinds of food hands out the same one whatever the platform's heap order: ties
	// between items in one box (which share its key) go to the lower item id.
	TEST_F(HungerWalkTest, ABoxWithTwoFoodsHandsOutTheLowerItemId) {
		registerItem("Nut", 1, 0.1F, 1, /*edible=*/true);
		const uint32_t nutId = assets::AssetRegistry::Get().getDefNameId("Nut");
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		const EntityID box = foodBox({4.0F, 0.0F}, 2, {colonist});
		world->getComponent<Inventory>(box)->addItem("Nut", 2);

		decide(colonist);
		ASSERT_EQ(getTask(colonist)->needSourceStorageId, static_cast<uint64_t>(box));
		EXPECT_EQ(getTask(colonist)->needItemDefNameId, std::min(berryId, nutId));
	}

	// A box with no appearance has no world key, so no colonist knows it, and it is never a food source.
	TEST_F(HungerWalkTest, ABoxWithNoAppearanceIsNoFoodSource) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		const EntityID box = foodBox({4.0F, 0.0F}, 2, {colonist});
		world->removeComponent<Appearance>(box);

		decide(colonist);
		EXPECT_NE(getTask(colonist)->type, TaskType::FulfillNeed);
		EXPECT_NE(optionOf(colonist, TaskType::FulfillNeed, OptionStatus::NoSource), nullptr);
	}

	// The box he walks to is packaged to be moved before he gets there: he decides again, eats from the
	// pile he also knows, and never goes back to the box.
	TEST_F(HungerWalkTest, ABoxPackagedOnTheWaySendsHimToOtherFood) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		const EntityID box = foodBox({8.0F, 0.0F}, 3, {colonist});
		const EntityID pile = berryPile({-12.0F, 0.0F}, 2, {colonist});
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->needSourceStorageId, static_cast<uint64_t>(box));
		world->addComponent<Packaged>(box, Packaged{});

		std::vector<uint64_t> boxTasks;
		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }, [&] {
			const Task& task = *getTask(colonist);
			if (task.isActive() && task.needSourceStorageId == static_cast<uint64_t>(box) &&
				std::find(boxTasks.begin(), boxTasks.end(), task.instanceId) == boxTasks.end()) {
				boxTasks.push_back(task.instanceId);
			}
		}));
		EXPECT_EQ(boxBerries(box), 3U);
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 1U) << "he ate from the pile";
		EXPECT_EQ(boxTasks.size(), 1U) << "no second trip to the packaged box";
	}

	// The last berry in the box he walks to is taken by a more urgent eater before he gets there: he
	// decides again and eats from the pile, and never loops on the empty box.
	TEST_F(HungerWalkTest, ABoxEmptiedByAnotherEaterSendsHimToOtherFood) {
		const EntityID colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		const EntityID starving = hungryColonist({9.0F, 0.0F}, 5.0F);
		const EntityID box = foodBox({8.0F, 0.0F}, 1, {colonist, starving});
		const EntityID pile = berryPile({-12.0F, 0.0F}, 2, {colonist});
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->needSourceStorageId, static_cast<uint64_t>(box));
		decide(starving);
		ASSERT_EQ(getTask(starving)->needSourceStorageId, static_cast<uint64_t>(box)) << "the more urgent eater outranks him";

		std::vector<uint64_t> boxTasks;
		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }, [&] {
			const Task& task = *getTask(colonist);
			if (task.isActive() && task.needSourceStorageId == static_cast<uint64_t>(box) &&
				std::find(boxTasks.begin(), boxTasks.end(), task.instanceId) == boxTasks.end()) {
				boxTasks.push_back(task.instanceId);
			}
		}));
		EXPECT_EQ(boxBerries(box), 0U);
		EXPECT_GT(hunger(starving), 5.0F) << "the starving one ate the berry";
		EXPECT_EQ(world->getComponent<ResourceStack>(pile)->quantity, 1U) << "he ate from the pile";
		EXPECT_EQ(boxTasks.size(), 1U) << "no second trip to the empty box";
	}

	// A pile he remembers was taken since he saw it. He goes to look (nobody else claims it), forgets it
	// when he finds nothing, and eats from a bush instead.
	TEST_F(HungerWalkTest, AGonePileIsForgottenAndOtherFoodFound) {
		const glm::vec2 pilePos{3.0F, 0.0F};
		const glm::vec2 bushPos{-6.0F, 0.0F};
		const EntityID	colonist = hungryColonist({0.0F, 0.0F}, 30.0F);
		const EntityID	pile = berryPile(pilePos, 2, {colonist});
		destroyWorldEntity(*world, pile);
		rememberBush(colonist, bushPos);
		const uint64_t pileKey = hashWorldEntity(pilePos, berryId);
		decide(colonist);
		ASSERT_EQ(getTask(colonist)->needSourceKey, pileKey) << "a stale pile is still worth a look";

		std::vector<uint64_t> pileTasks;
		ASSERT_TRUE(walkUntil([&] { return hunger(colonist) >= 50.0F; }, [&] {
			const Task& task = *getTask(colonist);
			if (task.isActive() && task.needSourceKey == pileKey && std::find(pileTasks.begin(), pileTasks.end(), task.instanceId) == pileTasks.end()) {
				pileTasks.push_back(task.instanceId);
			}
		}));
		EXPECT_EQ(world->getComponent<Memory>(colonist)->getWorldEntity(pileKey), nullptr) << "he forgot the pile";
		EXPECT_EQ(cut, std::vector<glm::vec2>{bushPos}) << "and ate from the bush";
		EXPECT_EQ(pileTasks.size(), 1U) << "one look, no loop";
	}

	// Gathering food is idle work and makes no room: with a pack that has none for a berry, it is not
	// offered, so he never walks to a bush he would cut nothing from.
	TEST_F(HungerWalkTest, GatheringFoodNeedsRoomForTheYield) {
		const EntityID colonist = createColonist({0.0F, 0.0F});
		auto&		   pack = *world->getComponent<Inventory>(colonist);
		pack.maxCapacity = 1;
		pack.addItem("Stone", 3);
		rememberBush(colonist, {4.0F, 0.0F});

		decide(colonist);
		EXPECT_NE(getTask(colonist)->type, TaskType::FulfillNeed);
		const EvaluatedOption* gather = optionOf(colonist, TaskType::FulfillNeed, OptionStatus::NoRoom);
		ASSERT_NE(gather, nullptr);
		EXPECT_TRUE(gather->gathersFood);
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

	// The failed-route rule (ClaimIndex::addCurrent) on routes a NavigationSystem plans over real ground,
	// where the tests above set the nav state by hand.
	class ClaimsOnRealRoutesTest : public ChainClaimsTest {
	  protected:
		EmptyPlacement emptyPlacement;

		void SetUp() override {
			assets::ConstructionRegistry::Get().clear();
			const std::filesystem::path root =
				std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load((root / "assets" / "config" / "construction").string()));
			ChainClaimsTest::SetUp();
			world->registerSystem<TimeSystem>();
			nav = &world->registerSystem<NavigationSystem>();
			nav->setChunkManager(openLand());
			nav->setPlacementData(&emptyPlacement.executor, &emptyPlacement.processed);
			nav->setConstructionWorld(&construction);
			ai->setNavigationSystem(nav);
			world->registerSystem<MovementSystem>();
			world->registerSystem<PhysicsSystem>();
		}

		void TearDown() override {
			ChainClaimsTest::TearDown();
			assets::ConstructionRegistry::Get().clear();
		}

		// Generated once per process: nothing here writes to it.
		static world::ChunkManager* openLand() {
			static const std::unique_ptr<world::ChunkManager> chunks = [] {
				auto manager = std::make_unique<world::ChunkManager>(std::make_unique<world::SyntheticWorldSampler>(7U, world::SyntheticMap{}));
				manager->setLoadRadius(2);
				manager->setUnloadRadius(4);
				manager->update({0.0F, 0.0F});
				manager->finishPendingGeneration();
				return manager;
			}();
			return chunks.get();
		}

		// Built walls around [min, max] (millimetres) that `colonist` knows. Returns the west side's segments.
		std::vector<construction::SegmentId> wallIn(geometry::Vec2i64 min, geometry::Vec2i64 max, EntityID colonist) {
			const std::array<geometry::Vec2i64, 4> corners{{{max.x, min.y}, max, {min.x, max.y}, min}};
			std::vector<construction::SegmentId>	 west;
			geometry::Vec2i64						 from = min;
			for (const geometry::Vec2i64 to : corners) {
				const auto commit = construction.commitSegment(from, to, "Wood", "Standard", construction::kInvalidFoundation);
				EXPECT_TRUE(commit.ok());
				for (const auto id : commit.createdSegments) {
					construction.setSegmentState(id, construction::FoundationState::Built);
					world->getComponent<Memory>(colonist)->rememberSegment(static_cast<std::uint64_t>(id));
					if (to == min) {
						west.push_back(id);
					}
				}
				from = to;
			}
			return west;
		}

		size_t carried(EntityID colonist, const char* item) {
			return ecs::availableQuantity(*world->getComponent<Inventory>(colonist), item);
		}

		bool delivering(EntityID colonist, EntityID destination) {
			const Task& task = *getTask(colonist);
			return task.type == TaskType::Haul && task.haulDestinationId == static_cast<uint64_t>(destination);
		}

		// A hauler at (-7, 0) who knows a pile of `count` `item` at (-6, 0); runs until he has lifted all of
		// it and is walking it to `destination`.
		EntityID haulerWalking(EntityID destination, uint32_t item, uint32_t count) {
			const glm::vec2 pilePos{-6.0F, 0.0F};
			const EntityID	pile = world->createEntity();
			world->addComponent<Position>(pile, Position{pilePos});
			const std::string name = assets::AssetRegistry::Get().getDefName(item);
			makeLoosePile(*world, pile, item, pilePos, count);
			const EntityID hauler = createColonist({-7.0F, 0.0F});
			remember(hauler, pilePos, name, assets::CapabilityType::Carryable);
			nav->finishPendingBuilds();
			EXPECT_TRUE(nav->hasMesh()) << "the hauler meshed nothing";
			for (int tick = 0; tick < 200 && carried(hauler, name.c_str()) < count; ++tick) {
				world->update(kTick);
			}
			EXPECT_EQ(carried(hauler, name.c_str()), count) << "he never lifted the pile";
			EXPECT_TRUE(delivering(hauler, destination));
			EXPECT_EQ(getTask(hauler)->chainStep, 1U);
			EXPECT_EQ(getTask(hauler)->navState, NavState::Traveling);
			return hauler;
		}

		// Runs until the walled-off hauler's route to `destination` fails and he decides again. While his
		// failed task stands he holds Inbound and InFlight for his cargo and nothing else, and it stands for
		// at most one update: his next decision lets the delivery go.
		void runUntilHeLetsTheDeliveryGo(EntityID hauler, EntityID destination, uint32_t item) {
			int failedUpdates = 0;
			for (int tick = 0; tick < 100 && delivering(hauler, destination); ++tick) {
				world->update(kTick);
				if (!delivering(hauler, destination) || getTask(hauler)->navState != NavState::CantFindWayTo) {
					continue;
				}
				++failedUpdates;
				ai->claimIndex().forEachClaim([&](const Claim& claim, const ClaimHolder& holder) {
					if (holder.colonist == hauler) {
						EXPECT_TRUE(claim.kind() == ClaimKind::Inbound || claim.kind() == ClaimKind::InFlight);
						EXPECT_EQ(claim.itemDefNameId(), item);
					}
				});
			}
			ASSERT_FALSE(delivering(hauler, destination)) << "the route through the known walls never failed";
			EXPECT_LE(failedUpdates, 1) << "the failed delivery outlived his next decision";
		}

		static constexpr float kTick = 0.1F;
		// Taking a wall down moves the mesh's connectivity epoch, not his belief, so his no-known-way
		// verdict stands until it is kVerdictMinTicks AI updates old; then 30 s to walk round and deliver.
		static constexpr int			kTicksForTheWayToOpen = static_cast<int>(SourceReachMemo::kVerdictMinTicks) + 300;
		construction::ConstructionWorld construction;
		NavigationSystem*				nav = nullptr;
	};

	// A hauler past his pickup is walled off from the station mid-walk by walls he knows. His next decision
	// lets the delivery go: it is not offered while he believes he cannot reach the station, his claims
	// toward the root are released, and the stones stay in his pack as uncommitted goods. Once a wall comes
	// down and the verdict ages out, he is offered the delivery again, and delivers exactly what the
	// station wants. Every AI update's validator passes throughout (it aborts on a violation).
	TEST_F(ClaimsOnRealRoutesTest, AWalledOffHaulerLetsTheDeliveryGoAndTakesItUpOnceTheWayOpens) {
		const glm::vec2 stationPos{8.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stone", 3);
		childGoal(TaskType::Haul, root, station, stationPos, stoneId);
		const EntityID hauler = haulerWalking(station, stoneId, 3);
		ASSERT_FALSE(HasFailure());

		const auto west = wallIn({6000, -2000}, {10000, 2000}, hauler);
		ASSERT_FALSE(west.empty());
		runUntilHeLetsTheDeliveryGo(hauler, station, stoneId);
		ASSERT_FALSE(HasFailure());

		for (int tick = 0; tick < 50; ++tick) {
			world->update(kTick);
			ASSERT_FALSE(delivering(hauler, station)) << "tick " << tick;
			ASSERT_EQ(carried(hauler, "Stone"), 3U) << "tick " << tick;
			ASSERT_EQ(indexedClaimsOf(hauler), 0U) << "tick " << tick;
			ASSERT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 0U) << "tick " << tick;
		}

		for (const auto id : west) {
			ASSERT_TRUE(construction.removeSegment(id));
		}
		for (int tick = 0; tick < kTicksForTheWayToOpen && world->getComponent<Inventory>(station)->getQuantity("Stone") < 3; ++tick) {
			world->update(kTick);
		}
		EXPECT_EQ(world->getComponent<Inventory>(station)->getQuantity("Stone"), 3U) << "he never delivered once the wall was down";
		EXPECT_EQ(carried(hauler, "Stone"), 0U);
	}

	// While the first hauler is walled off, a colonist on the station's side of the wall is offered the
	// fetch and makes it. When the wall comes down the first hauler's stones are surplus there: the
	// station is never over-supplied, and he keeps them.
	TEST_F(ClaimsOnRealRoutesTest, AColonistInsideTheWallsFetchesAndTheFreedHaulerDoesNotOverDeliver) {
		const glm::vec2 stationPos{8.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos, "Stone", 3);
		childGoal(TaskType::Haul, root, station, stationPos, stoneId);
		const EntityID hauler = haulerWalking(station, stoneId, 3);
		ASSERT_FALSE(HasFailure());

		const auto west = wallIn({6000, -2000}, {10000, 2000}, hauler);
		runUntilHeLetsTheDeliveryGo(hauler, station, stoneId);
		ASSERT_FALSE(HasFailure());
		ASSERT_EQ(ai->claimIndex().inFlight(root, stoneId, {}), 0U);

		const glm::vec2 innerPilePos{9.0F, 1.0F};
		const EntityID	innerPile = world->createEntity();
		world->addComponent<Position>(innerPile, Position{innerPilePos});
		makeLoosePile(*world, innerPile, stoneId, innerPilePos, 3);
		const EntityID inside = createColonist({7.0F, -1.0F});
		remember(inside, innerPilePos, "Stone", assets::CapabilityType::Carryable);

		const auto stationStone = [&] { return world->getComponent<Inventory>(station)->getQuantity("Stone"); };
		bool	   insideFetched = false;
		for (int tick = 0; tick < 300 && stationStone() < 3; ++tick) {
			world->update(kTick);
			insideFetched = insideFetched || delivering(inside, station);
			ASSERT_EQ(carried(hauler, "Stone"), 3U) << "tick " << tick;
		}
		EXPECT_TRUE(insideFetched);
		ASSERT_EQ(stationStone(), 3U) << "the colonist inside never fetched";

		for (const auto id : west) {
			ASSERT_TRUE(construction.removeSegment(id));
		}
		for (int tick = 0; tick < kTicksForTheWayToOpen; ++tick) {
			world->update(kTick);
			ASSERT_LE(stationStone(), 3U) << "tick " << tick;
			ASSERT_FALSE(delivering(hauler, station)) << "tick " << tick;
		}
		EXPECT_EQ(carried(hauler, "Stone"), 3U) << "his stones are surplus at the station";
		EXPECT_EQ(indexedClaimsOf(hauler), 0U);
	}

	// A two-hand armful bound for a site he is walled off from is put down where he stands when he lets
	// the delivery go, not carried around: a pile anyone can fetch.
	TEST_F(ClaimsOnRealRoutesTest, AWalledOffHaulerPutsHisArmfulDown) {
		std::vector<uint32_t> drops;
		ai->setDropResourceCallback([&](const std::string& name, float, float, uint32_t quantity, glm::vec2) {
			EXPECT_EQ(name, "Wood");
			drops.push_back(quantity);
		});
		const glm::vec2 sitePos{8.0F, 0.0F};
		const Site		site = constructionSite(sitePos, "Wood", 4);
		const EntityID	hauler = haulerWalking(site.blueprint, woodId, 4);
		ASSERT_FALSE(HasFailure());

		wallIn({6000, -2000}, {10000, 2000}, hauler);
		runUntilHeLetsTheDeliveryGo(hauler, site.blueprint, woodId);
		ASSERT_FALSE(HasFailure());

		ASSERT_EQ(drops.size(), 1U) << "the armful he cannot deliver is carried around";
		EXPECT_EQ(drops.front(), 4U);
		EXPECT_EQ(carried(hauler, "Wood"), 0U);
		EXPECT_EQ(indexedClaimsOf(hauler), 0U);
		EXPECT_EQ(ai->claimIndex().inFlight(site.umbrella, woodId, {}), 0U);
	}

	// =============================================================================
	// Colonists bound for one point: separation keeps them apart, so no more than one of them can stand
	// on it. Each must still act once it is within reach of the point, or both stand there for good.
	// =============================================================================

	class CrowdedTargetTest : public ChainClaimsTest {
	  protected:
		void SetUp() override {
			ChainClaimsTest::SetUp();
			time = &world->registerSystem<TimeSystem>();
			world->registerSystem<MovementSystem>();
			world->registerSystem<PhysicsSystem>();
			world->registerSystem<CollisionSystem>();
		}

		EntityID walker(glm::vec2 position) {
			const EntityID colonist = createColonist(position);
			return colonist;
		}

		template <typename Done> bool runUntil(Done done, int maxFrames = 600) {
			for (int frame = 0; frame < maxFrames; ++frame) {
				world->update(0.1F);
				if (done()) {
					return true;
				}
			}
			return false;
		}

		TimeSystem* time = nullptr;
	};

	TEST_F(CrowdedTargetTest, TwoCarriersBothDepositAtOneStation) {
		for (const GameSpeed speed : {GameSpeed::Normal, GameSpeed::Fast}) {
			SCOPED_TRACE(static_cast<int>(speed));
			time->setSpeed(speed);
			GoalTaskRegistry::Get().clear();
			assets::RecipeRegistry::Get().clear();
			const glm::vec2 stationPos{0.0F, 6.0F};
			const EntityID	station = world->createEntity();
			world->addComponent<Position>(station, Position{stationPos});
			assets::RecipeDef recipe;
			recipe.defName = "Recipe_Kit";
			recipe.label = recipe.defName;
			recipe.inputs.push_back({"Stone", 0, 2});
			recipe.inputs.push_back({"Stick", 0, 1});
			assets::RecipeRegistry::Get().registerTestRecipe(recipe);
			world->addComponent<WorkQueue>(station).addJob(recipe.defName, 1);
			world->addComponent<Inventory>(station, Inventory::createForStorage());
			GoalTask craft;
			craft.type = TaskType::Craft;
			craft.owner = GoalOwner::CraftingGoalSystem;
			craft.destinationEntity = station;
			craft.destinationPosition = stationPos;
			craft.status = GoalStatus::Blocked;
			GoalTaskRegistry::Get().createGoal(std::move(craft));

			// Equally far on either side, so they come in together.
			const EntityID stoneCarrier = walker({-4.0F, 6.0F});
			const EntityID stickCarrier = walker({4.0F, 6.0F});
			world->getComponent<Inventory>(stoneCarrier)->addItem("Stone", 2);
			world->getComponent<Inventory>(stickCarrier)->addItem("Stick", 1);

			const auto* store = world->getComponent<Inventory>(station);
			const bool	delivered =
				runUntil([&] { return store->getQuantity("Stone") == 2 && store->getQuantity("Stick") == 1; });
			EXPECT_TRUE(delivered) << "station holds " << store->getQuantity("Stone") << " stone and " << store->getQuantity("Stick")
								   << " stick; the carriers stand "
								   << glm::distance(world->getComponent<Position>(stoneCarrier)->value, stationPos) << " m and "
								   << glm::distance(world->getComponent<Position>(stickCarrier)->value, stationPos) << " m from it";
			world->destroyEntity(stoneCarrier);
			world->destroyEntity(stickCarrier);
			world->destroyEntity(station);
		}
	}

	TEST_F(CrowdedTargetTest, ThreeThirstyColonistsAllDrinkAtOneShorePoint) {
		assets::AssetDefinition water;
		water.defName = "Water";
		water.label = "Water";
		assets::AssetRegistry::Get().registerTestDefinition(std::move(water));
		for (const GameSpeed speed : {GameSpeed::Normal, GameSpeed::Fast}) {
			SCOPED_TRACE(static_cast<int>(speed));
			time->setSpeed(speed);
			const glm::vec2		   shore{0.0F, 6.0F};
			std::vector<EntityID> drinkers;
			for (const glm::vec2 start : {glm::vec2{-4.0F, 6.0F}, glm::vec2{4.0F, 6.0F}, glm::vec2{0.0F, 2.0F}}) {
				const EntityID colonist = walker(start);
				world->getComponent<NeedsComponent>(colonist)->get(NeedType::Thirst).value = 8.0F;
				remember(colonist, shore, "Water", assets::CapabilityType::Drinkable);
				drinkers.push_back(colonist);
			}

			const bool allDrank = runUntil([&] {
				return std::all_of(drinkers.begin(), drinkers.end(), [&](EntityID colonist) {
					return world->getComponent<NeedsComponent>(colonist)->get(NeedType::Thirst).value > 50.0F;
				});
			});
			EXPECT_TRUE(allDrank);
			for (const EntityID colonist : drinkers) {
				EXPECT_GT(world->getComponent<NeedsComponent>(colonist)->get(NeedType::Thirst).value, 50.0F)
					<< "colonist " << colonist << " stands " << glm::distance(world->getComponent<Position>(colonist)->value, shore)
					<< " m from the shore point";
				world->destroyEntity(colonist);
			}
		}
	}

} // namespace ecs::test
