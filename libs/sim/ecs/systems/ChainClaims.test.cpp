// The claims each task chain holds at each stage, recorded by AIDecisionSystem at acceptance and
// rewritten by ActionSystem as the chain advances (docs/technical/task-claims.md, Chain claims). Each
// test drives the two systems one at a time, so it can read the claims between stages; every AI
// update here also runs the development-build validator, which aborts on a violation.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "ChainClaims.h"
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
#include "../components/SourceReachMemo.h"
#include "../components/StorageConfiguration.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"
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
			goal.targetAmount = 10;
			return GoalTaskRegistry::Get().createGoal(std::move(goal));
		}

		/// A blueprint wanting `wood` Wood, its construction umbrella, and the material's Haul child.
		struct Site {
			EntityID blueprint;
			uint64_t umbrella;
			uint64_t haul;
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
			umbrella.targetAmount = 1;
			umbrella.status = GoalStatus::Blocked;
			const uint64_t umbrellaId = goals.createGoal(std::move(umbrella));

			GoalTask haul;
			haul.type = TaskType::Haul;
			haul.owner = GoalOwner::ConstructionGoalSystem;
			haul.destinationEntity = blueprint;
			haul.destinationPosition = position;
			haul.acceptedDefNameIds = {assets::AssetRegistry::Get().getDefNameId(item)};
			haul.targetAmount = needed;
			haul.parentGoalId = umbrellaId;
			haul.chainId = goals.allocateChainId();
			return {blueprint, umbrellaId, goals.createGoal(std::move(haul))};
		}

		/// A crafting station's Craft root.
		uint64_t craftRoot(EntityID station, glm::vec2 position) {
			GoalTask craft;
			craft.type = TaskType::Craft;
			craft.owner = GoalOwner::CraftingGoalSystem;
			craft.destinationEntity = station;
			craft.destinationPosition = position;
			craft.targetAmount = 5;
			craft.status = GoalStatus::Blocked;
			return GoalTaskRegistry::Get().createGoal(std::move(craft));
		}

		uint64_t childGoal(TaskType type, uint64_t root, EntityID destination, glm::vec2 position, uint32_t item, uint32_t amount) {
			GoalTask child;
			child.type = type;
			child.owner = GoalOwner::CraftingGoalSystem;
			child.destinationEntity = destination;
			child.destinationPosition = position;
			child.acceptedDefNameIds = {item};
			child.yieldDefNameId = type == TaskType::Harvest ? item : 0;
			child.targetAmount = amount;
			child.parentGoalId = root;
			child.chainId = GoalTaskRegistry::Get().allocateChainId();
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

		// The pickup lifts the asset's 5: the source claim is spent and the rest follows the goods.
		act(colonist, pilePos);
		ASSERT_EQ(task.type, TaskType::Haul);
		ASSERT_EQ(ecs::availableQuantity(*world->getComponent<Inventory>(colonist), "Stone"), 5U);
		EXPECT_FALSE(claimOf(task, ClaimKind::SourceQuantity).has_value());
		EXPECT_EQ(claimOf(task, ClaimKind::Inbound)->quantity(), std::min(q, 5U));
		EXPECT_EQ(claimOf(task, ClaimKind::InFlight)->quantity(), std::min(q, 5U));

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
		const uint64_t root = craftRoot(station, stationPos);
		childGoal(TaskType::Haul, root, station, stationPos, stoneId, 2);
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

	// A cut claims its source and expects its largest yield toward the root; the claims end with the
	// task when the cut completes (the deposit leg is not part of this step).
	TEST_F(ChainClaimsTest, HarvestForAGoalClaimsTheSourceAndItsExpectedYield) {
		const glm::vec2 stationPos{0.0F, 6.0F};
		const glm::vec2 bushPos{3.0F, 0.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationPos});
		const uint64_t root = craftRoot(station, stationPos);
		childGoal(TaskType::Harvest, root, station, stationPos, stickId, 5);
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
		EXPECT_GT(ecs::availableQuantity(*world->getComponent<Inventory>(colonist), "Stick"), 0U);
		EXPECT_FALSE(task.isActive());
		EXPECT_TRUE(task.claims.empty());
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
		clear.targetAmount = 1;
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
		GoalTaskRegistry::Get().removeGoal(site.haul);
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

} // namespace ecs::test
