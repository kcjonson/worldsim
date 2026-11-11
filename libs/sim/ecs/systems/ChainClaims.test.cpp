// The claims each task chain holds at each stage, recorded by AIDecisionSystem at acceptance and
// rewritten by ActionSystem as the chain advances (docs/technical/task-claims.md, Chain claims). Each
// test drives the two systems one at a time, so it can read the claims between stages; every AI
// update here also runs the development-build validator, which aborts on a violation.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "ChainClaims.h"
#include "CraftingGoalSystem.h"
#include "StorageGoalSystem.h"
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
		ASSERT_GE(q, 8U) << "the box's room and one trip; the live pile caps it at pickup";
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

} // namespace ecs::test
