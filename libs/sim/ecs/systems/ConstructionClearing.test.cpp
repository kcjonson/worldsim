// Foundation footprint clearing, end to end: ConstructionSystem emits the clear goal from the real
// footprint and placement index, AIDecisionSystem picks the work, ActionSystem harvests and removes
// the blocker from the index. Movement is skipped: a colonist arrives the moment it takes a task.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "ConstructionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "../components/Action.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Structure.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>
#include <assets/PriorityConfig.h>
#include <assets/RecipeRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <construction/ConstructionWorld.h>
#include <world/chunk/ChunkCoordinate.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

namespace ecs::test {

	namespace {
		constexpr const char* kTree = "Flora_ClearTestTree";

		class FootprintClearingTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				GoalTaskRegistry::Get().clear();
				assets::PriorityConfig::Get().clear();
				auto& registry = assets::AssetRegistry::Get();
				registry.clearDefinitions();

				// Wood as the game has it: a two-hand armful, 2.5 kg a unit, so 14 fill the arms.
				assets::AssetDefinition wood;
				wood.defName = "Wood";
				wood.label = "Wood";
				wood.handsRequired = 2;
				wood.category = assets::ItemCategory::RawMaterial;
				wood.itemProperties = assets::ItemProperties{};
				wood.itemProperties->stackSize = 40;
				wood.itemProperties->massKg = 2.5F;
				registry.registerTestDefinition(std::move(wood));

				assets::AssetDefinition tree;
				tree.defName = kTree;
				tree.label = "Clear Test Tree";
				assets::HarvestableCapability cap;
				cap.yieldDefName = "Wood";
				cap.amountMin = 20; // more than one armful, as an oak's 20-30
				cap.amountMax = 20;
				cap.durability = 10.0F;
				cap.destructive = true;
				tree.capabilities.harvestable = cap;
				registry.registerTestDefinition(std::move(tree));

				world = std::make_unique<World>();
				world->registerSystem<AIDecisionSystem>(registry, assets::RecipeRegistry::Get(), 42);
				auto& action = world->registerSystem<ActionSystem>();
				action.setRemoveEntityCallback([this](const std::string& defName, float x, float y) {
					executor.removeEntity(world::worldToChunk({x, y}), {x, y}, defName);
				});
				action.setDropResourceCallback([this](const std::string& /*defName*/, float /*x*/, float /*y*/, uint32_t /*qty*/) {
					++piles;
				});
				construction = &world->registerSystem<ConstructionSystem>();
				construction->setConstructionWorld(&topology);
				construction->setPlacementData(&executor, &processed);

				// An 8 m square foundation blueprint, CCW in integer mm.
				geometry::Ring					   ring{{0, 0}, {8000, 0}, {8000, 8000}, {0, 8000}};
				const construction::CommitResult foundation = topology.commitFoundation(std::move(ring), "wood");
				EXPECT_TRUE(foundation.ok());
				site = world->createEntity();
				world->addComponent<Position>(site, Position{{4.0F, 4.0F}});
				world->addComponent<Structure>(site, Structure{StructureKind::Foundation, foundation.id});
				StructureBlueprint bp;
				bp.phase = StructureBlueprint::BuildPhase::Clearing;
				bp.required = {{"Wood", 10}};
				bp.workTotal = 50.0F;
				world->addComponent<StructureBlueprint>(site, std::move(bp));
			}

			void TearDown() override {
				world.reset();
				GoalTaskRegistry::Get().clear();
				assets::AssetRegistry::Get().clearDefinitions();
			}

			/// Plant trees in the placement index, as world generation would.
			void plantTrees(const std::vector<glm::vec2>& at) {
				const world::ChunkCoordinate coord = world::worldToChunk({at.front().x, at.front().y});
				assets::SpatialIndex		 index;
				for (const glm::vec2& pos : at) {
					assets::PlacedEntity tree;
					tree.defName = kTree;
					tree.position = pos;
					index.insert(tree);
				}
				assets::AsyncChunkPlacementResult result;
				result.coord = coord;
				result.spatialIndex = std::move(index);
				executor.storeChunkResult(std::move(result));
				processed.insert(coord);
			}

			EntityID createColonist(glm::vec2 position, const std::vector<glm::vec2>& knownTrees) {
				auto entity = world->createEntity();
				world->addComponent<Position>(entity, Position{position});
				world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
				world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
				world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
				world->addComponent<Inventory>(entity, Inventory::createForColonist());
				world->addComponent<Task>(entity, Task{});
				world->addComponent<Action>(entity, Action{});
				world->addComponent<DecisionTrace>(entity, DecisionTrace{});
				Memory memory;
				for (const glm::vec2& pos : knownTrees) {
					remember(memory, pos);
				}
				world->addComponent<Memory>(entity, std::move(memory));
				return entity;
			}

			static void remember(Memory& memory, glm::vec2 pos) {
				auto&		   registry = assets::AssetRegistry::Get();
				const uint32_t id = registry.getDefNameId(kTree);
				memory.rememberWorldEntity(pos, id, registry.getCapabilityMask(id));
			}

			bool treeStands(glm::vec2 pos) const {
				const auto* index = executor.getChunkIndex(world::worldToChunk({pos.x, pos.y}));
				return index != nullptr && index->hasNearby(pos, 0.1F, kTree);
			}

			/// Run frames, arriving each colonist at its task target at once, until `done` holds.
			template <typename Done> bool runUntil(Done done, int maxFrames = 3000) {
				for (int frame = 0; frame < maxFrames; ++frame) {
					world->update(0.016F);
					for (auto [entity, task, pos] : world->view<Task, Position>()) {
						if (task.state == TaskState::Moving) {
							pos.value = task.targetPosition;
							task.state = TaskState::Arrived;
						}
					}
					if (done()) {
						return true;
					}
				}
				return false;
			}

			StructureBlueprint::BuildPhase phase() { return world->getComponent<StructureBlueprint>(site)->phase; }

			std::unique_ptr<World>				   world;
			ConstructionSystem*					   construction = nullptr;
			construction::ConstructionWorld		   topology;
			assets::PlacementExecutor			   executor{assets::AssetRegistry::Get()};
			std::unordered_set<world::ChunkCoordinate> processed;
			EntityID							   site = kInvalidEntity;
			int									   piles = 0;
		};
	} // namespace

	// Felling the first tree fills the colonist's arms (14 of its 20 Wood, the rest drops). The second
	// clear must still be offered and still fell the tree, or the site never leaves Clearing.
	TEST_F(FootprintClearingTest, ColonistWithFullArmsClearsTheWholeFootprint) {
		const std::vector<glm::vec2> trees{{2.0F, 2.0F}, {6.0F, 6.0F}};
		plantTrees(trees);
		auto colonist = createColonist({1.0F, 1.0F}, trees);

		ASSERT_TRUE(runUntil([&] { return !treeStands(trees[0]) || !treeStands(trees[1]); })) << "the first clear fells a tree";
		ASSERT_EQ(ecs::cargoUnitsThatFit(*world->getComponent<Inventory>(colonist), assets::AssetRegistry::Get(), "Wood"), 0U)
			<< "precondition: the first fell filled his arms";

		EXPECT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; }))
			<< "the second tree is felled with full arms and the site leaves Clearing";
		EXPECT_FALSE(treeStands(trees[0]));
		EXPECT_FALSE(treeStands(trees[1]));
		EXPECT_EQ(piles, 2) << "each fell drops what didn't fit as one pile";
	}

	// The clear goal names no single blocker: when one blocker goes some other way (a craft Harvest
	// fells it), the goal is served by the next one.
	TEST_F(FootprintClearingTest, ClearGoalOutlivesABlockerRemovedElsewhere) {
		const std::vector<glm::vec2> trees{{2.0F, 2.0F}, {6.0F, 6.0F}};
		plantTrees(trees);
		auto colonist = createColonist({1.0F, 1.0F}, {});

		ASSERT_TRUE(runUntil([] { return !GoalTaskRegistry::Get().getGoalsOfType(TaskType::Harvest).empty(); }))
			<< "precondition: the blocked site emits its clear goal";

		// The first tree goes some other way; the colonist only ever learns about the second.
		executor.removeEntity(world::worldToChunk({trees[0].x, trees[0].y}), trees[0], kTree);
		remember(*world->getComponent<Memory>(colonist), trees[1]);

		EXPECT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; }))
			<< "the remaining blocker serves the standing clear goal";
		EXPECT_FALSE(treeStands(trees[1]));
	}

	// The first blocker in the index is one the colonist has never seen. A blocker he does know, in the
	// same footprint, must not hide behind it: he clears that one (discovery gating still holds for the first).
	TEST_F(FootprintClearingTest, KnownBlockerIsClearedWhileAnUnknownOneStands) {
		const std::vector<glm::vec2> trees{{2.0F, 2.0F}, {6.0F, 6.0F}};
		plantTrees(trees);
		createColonist({1.0F, 1.0F}, {trees[1]});

		EXPECT_TRUE(runUntil([&] { return !treeStands(trees[1]); })) << "the known blocker is cleared";
		EXPECT_TRUE(treeStands(trees[0])) << "the unknown blocker is left for when someone sees it";
		EXPECT_EQ(phase(), StructureBlueprint::BuildPhase::Clearing);
	}

	// A material Harvest (not a clear) feeds the site through construction's own per-material Haul:
	// completing it creates no second Haul, and the Wood still lands on the manifest.
	TEST_F(FootprintClearingTest, MaterialHarvestDeliversThroughTheSiteHaulAlone) {
		const glm::vec2 tree{12.0F, 12.0F}; // outside the footprint: the site is clear from the start
		plantTrees({tree});
		createColonist({11.0F, 11.0F}, {tree});

		size_t mostHauls = 0;
		const bool built = runUntil([&] {
			mostHauls = std::max(mostHauls, GoalTaskRegistry::Get().getGoalsOfType(TaskType::Haul).size());
			return world->getComponent<StructureBlueprint>(site)->materialsComplete();
		});
		EXPECT_TRUE(built) << "the felled Wood reaches the site";
		EXPECT_EQ(mostHauls, 1U) << "only construction's own Haul; the completed Harvest creates none";
	}

} // namespace ecs::test
