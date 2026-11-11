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

		/// A blueprint wanting `needed` fibre and its construction umbrella with a cutting child.
		Site fibreSite(glm::vec2 position, uint32_t needed) {
			const Site site = constructionSite(position, "Fibre", needed);
			childOf(site, position);
			return site;
		}

		void childOf(const Site& site, glm::vec2 position) {
			GoalTask harvest;
			harvest.type = TaskType::Harvest;
			harvest.owner = GoalOwner::ConstructionGoalSystem;
			harvest.destinationEntity = site.blueprint;
			harvest.destinationPosition = position;
			harvest.yieldDefNameId = fibreId;
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
			return blueprint->required.front().second - blueprint->remaining("Fibre");
		}

		/// Run frames until `done` holds; each delivery to the site and each cut the colonist lands is
		/// counted.
		template <typename Done> bool walkUntil(const Site& site, EntityID colonist, Done done, int maxFrames = 4000) {
			uint32_t lastDelivered = delivered(site);
			uint32_t lastCarried = carriedFibre(colonist);
			for (int frame = 0; frame < maxFrames; ++frame) {
				world->update(0.1F);
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
		EXPECT_EQ(trips, 2) << "two full trips of ten, not one per cut";
		EXPECT_GE(cuts, 7);
		EXPECT_EQ(mostCarried, perTrip()) << "each trip filled before it was walked";
		EXPECT_LE(mostInFlight, perTrip()) << "the cuts of one trip never claim more than it carries";
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

		// A second bush, farther off: the second hungry colonist, now starving, goes there instead (the
		// work order would outrank a routine meal).
		const glm::vec2 farBush{-6.0F, 0.0F};
		remember(alsoHungry, farBush, "BerryBush", assets::CapabilityType::Harvestable);
		world->getComponent<NeedsComponent>(alsoHungry)->get(NeedType::Hunger).value = 5.0F;
		decide(alsoHungry);
		ASSERT_EQ(getTask(alsoHungry)->type, TaskType::FulfillNeed);
		EXPECT_EQ(getTask(alsoHungry)->needSourceKey, hashWorldEntity(farBush, assets::AssetRegistry::Get().getDefNameId("BerryBush")));
	}

} // namespace ecs::test
