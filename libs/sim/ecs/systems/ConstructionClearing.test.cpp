// Foundation footprint clearing, end to end: ConstructionSystem emits the clear goal from the real
// footprint and placement index, AIDecisionSystem picks the work, ActionSystem harvests and removes
// the blocker from the index. Movement is skipped: a colonist arrives the moment it takes a task.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "ConstructionSystem.h"
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
#include "../components/SourceReachMemo.h"
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
#include <core/Vec2i64.h>
#include <predicates/Predicates.h>
#include <world/chunk/ChunkCoordinate.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

namespace ecs::test {

	namespace {
		constexpr const char* kTree = "Flora_ClearTestTree";
		constexpr const char* kBush = "Flora_ClearTestBush";
		constexpr uint32_t	  kTestRngSeed = 42;
		constexpr const char* kGrass = "Groundcover_ClearTestGrass";

		/// An 8 m square with its north-east quarter notched out: the floor is x 0-8 by y 0-4 and x 0-4 by y 4-8.
		geometry::Ring lShapedRing() {
			return {{0, 0}, {8000, 0}, {8000, 4000}, {4000, 4000}, {4000, 8000}, {0, 8000}};
		}

		bool onRing(const geometry::Ring& ring, glm::vec2 p) {
			return geometry::pointInPolygon(geometry::quantize({p.x, p.y}), ring) != geometry::PointInPolygon::Outside;
		}

		void registerGrass() {
			assets::AssetDefinition grass;
			grass.defName = kGrass;
			grass.label = "Clear Test Grass";
			grass.role = assets::AssetRole::Groundcover;
			assets::AssetRegistry::Get().registerTestDefinition(std::move(grass));
		}

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

				// A berry bush: one-hand yield, regrows rather than falling, and slower to strip than
				// the tree is to fell, so a tree clear finishes while a bush clear is still underway.
				// Edible like the real berry, so a colonist holding some counts as carrying food.
				assets::AssetDefinition berry;
				berry.defName = "Berry";
				berry.label = "Berry";
				berry.handsRequired = 1;
				berry.capabilities.carryable = assets::CarryableCapability{1};
				berry.itemProperties = assets::ItemProperties{};
				berry.itemProperties->stackSize = 200;
				berry.itemProperties->massKg = 0.15F;
				berry.itemProperties->edible = assets::EdibleCapability{0.3F, assets::CapabilityQuality::Normal, true};
				registry.registerTestDefinition(std::move(berry));

				assets::AssetDefinition bush;
				bush.defName = kBush;
				bush.label = "Clear Test Bush";
				assets::HarvestableCapability bushCap;
				bushCap.yieldDefName = "Berry";
				bushCap.amountMin = 3;
				bushCap.amountMax = 3;
				bushCap.durability = 30.0F;
				bushCap.destructive = false;
				bushCap.regrowthTime = 60.0F;
				bush.capabilities.harvestable = bushCap;
				registry.registerTestDefinition(std::move(bush));

				world = std::make_unique<World>();
				world->registerSystem<AIDecisionSystem>(registry, assets::RecipeRegistry::Get(), kTestRngSeed);
				auto& action = world->registerSystem<ActionSystem>(kTestRngSeed);
				action.setTakeSourceCallback(
					[this](const std::string& defName, float x, float y, ActionSystem::SourceTake take, float seconds) {
						return takeWorldSource(executor, *world, pendingRemoval, defName, {x, y}, take, seconds);
					}
				);
				action.setDropResourceCallback([this](const std::string& /*defName*/, float /*x*/, float /*y*/, uint32_t /*qty*/, glm::vec2) {
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

			/// Plant trees (and berry bushes) in the placement index, as world generation would.
			void plantTrees(const std::vector<glm::vec2>& at, const std::vector<glm::vec2>& bushes = {}) {
				const world::ChunkCoordinate coord = world::worldToChunk({at.front().x, at.front().y});
				assets::SpatialIndex		 index;
				for (const glm::vec2& pos : at) {
					assets::PlacedEntity tree;
					tree.defName = kTree;
					tree.position = pos;
					index.insert(tree);
				}
				for (const glm::vec2& pos : bushes) {
					assets::PlacedEntity bush;
					bush.defName = kBush;
					bush.position = pos;
					index.insert(bush);
				}
				assets::AsyncChunkPlacementResult result;
				result.coord = coord;
				result.spatialIndex = std::move(index);
				executor.storeChunkResult(std::move(result));
				processed.insert(coord);
			}

			/// Place the chunk under `at` with nothing growing in it, as world generation would for bare ground.
			void placeBareChunk(glm::vec2 at) {
				assets::AsyncChunkPlacementResult result;
				result.coord = world::worldToChunk({at.x, at.y});
				processed.insert(result.coord);
				executor.storeChunkResult(std::move(result));
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
				world->addComponent<SourceReachMemo>(entity, SourceReachMemo{});
				Memory memory;
				for (const glm::vec2& pos : knownTrees) {
					remember(memory, pos);
				}
				world->addComponent<Memory>(entity, std::move(memory));
				return entity;
			}

			static void remember(Memory& memory, glm::vec2 pos, const char* defName = kTree) {
				auto&		   registry = assets::AssetRegistry::Get();
				const uint32_t id = registry.getDefNameId(defName);
				memory.rememberWorldEntity(pos, id, registry.getCapabilityMask(id));
			}

			bool treeStands(glm::vec2 pos, const char* defName = kTree) const {
				const auto* index = executor.getChunkIndex(world::worldToChunk({pos.x, pos.y}));
				return index != nullptr && index->hasNearby(pos, 0.1F, defName);
			}

			/// Run frames, arriving each colonist at its task target at once (unless `held`, who stays
			/// on the way), until `done` holds. Taken spawned entities are destroyed after each update,
			/// as the game drains them.
			template <typename Done> bool runUntil(Done done, int maxFrames = 3000, EntityID held = kInvalidEntity) {
				for (int frame = 0; frame < maxFrames; ++frame) {
					world->update(0.016F);
					for (EntityID taken : pendingRemoval) {
						world->destroyEntity(taken);
					}
					pendingRemoval.clear();
					for (auto [entity, task, pos] : world->view<Task, Position>()) {
						if (task.state == TaskState::Moving && entity != held) {
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

			/// Give the site a footprint of another shape, CCW in integer mm, in place of the 8 m square.
			void reshapeSite(geometry::Ring ring) {
				auto& structure = *world->getComponent<Structure>(site);
				EXPECT_TRUE(topology.removeFoundation(structure.graphId));
				const construction::CommitResult foundation = topology.commitFoundation(std::move(ring), "wood");
				ASSERT_TRUE(foundation.ok());
				structure.graphId = foundation.id;
			}

			StructureBlueprint::BuildPhase phase() { return world->getComponent<StructureBlueprint>(site)->phase; }

			std::unique_ptr<World>				   world;
			ConstructionSystem*					   construction = nullptr;
			construction::ConstructionWorld		   topology;
			assets::PlacementExecutor			   executor{assets::AssetRegistry::Get()};
			std::unordered_set<world::ChunkCoordinate> processed;
			EntityID							   site = kInvalidEntity;
			std::vector<EntityID>				   pendingRemoval;
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

	// The whole site lifecycle, end to end: a tree in the footprint is cleared, two materials (Wood
	// from a tree outside, Berry from a bush) are cut and each cut carries itself to the site, and the
	// colonist then builds it.
	TEST_F(FootprintClearingTest, SiteIsClearedSuppliedWithTwoMaterialsAndBuilt) {
		world->registerSystem<TimeSystem>(); // the bush regrows on the game clock
		auto& blueprint = *world->getComponent<StructureBlueprint>(site);
		blueprint.required = {{"Wood", 10}, {"Berry", 3}};
		const glm::vec2 blocker{2.0F, 2.0F};
		const glm::vec2 tree{12.0F, 12.0F};
		const glm::vec2 bush{12.0F, 2.0F};
		plantTrees({blocker, tree}, {bush});
		const EntityID colonist = createColonist({1.0F, 1.0F}, {blocker, tree});
		remember(*world->getComponent<Memory>(colonist), bush, kBush);

		ASSERT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; })) << "the footprint is cleared";
		EXPECT_FALSE(treeStands(blocker));
		ASSERT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::Complete; }, 20000)) << "supplied and built";
		EXPECT_TRUE(blueprint.materialsComplete());
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

	// A runtime-spawned bush (an ECS entity, not in the placement index) is a blocker a colonist can
	// remember and clear: it holds the site in Clearing exactly as placed flora does until his clear
	// takes it, through the same take-source the game wires.
	TEST_F(FootprintClearingTest, SpawnedBlockerIsClearedLikePlacedFlora) {
		const glm::vec2 at{6.0F, 6.0F};
		placeBareChunk(at);
		const EntityID	bush = world->createEntity();
		world->addComponent<Position>(bush, Position{at});
		world->addComponent<Appearance>(bush, Appearance{kBush});
		auto colonist = createColonist({1.0F, 1.0F}, {});
		remember(*world->getComponent<Memory>(colonist), at, kBush);
		world->getComponent<Inventory>(colonist)->addItem("Berry", 5); // fed: he takes the bush only to clear it

		ASSERT_TRUE(runUntil([] { return !GoalTaskRegistry::Get().getGoalsOfType(TaskType::Harvest).empty(); }))
			<< "the spawned bush blocks the footprint and the site offers its clear goal";
		EXPECT_EQ(phase(), StructureBlueprint::BuildPhase::Clearing);

		EXPECT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; }))
			<< "his clear takes the spawned bush and the site moves on";
		EXPECT_FALSE(world->isAlive(bush));
	}

	// A material Harvest (not a clear) carries its own Wood to the site: no Haul goal exists at any
	// point, and the Wood lands on the manifest.
	TEST_F(FootprintClearingTest, MaterialHarvestCarriesItsYieldToTheSite) {
		const glm::vec2 tree{12.0F, 12.0F}; // outside the footprint: the site is clear from the start
		plantTrees({tree});
		createColonist({11.0F, 11.0F}, {tree});

		size_t mostHauls = 0;
		const bool built = runUntil([&] {
			mostHauls = std::max(mostHauls, GoalTaskRegistry::Get().getGoalsOfType(TaskType::Haul).size());
			return world->getComponent<StructureBlueprint>(site)->materialsComplete();
		});
		EXPECT_TRUE(built) << "the felled Wood reaches the site";
		EXPECT_EQ(mostHauls, 0U) << "the cut is its own deposit leg; no Haul goal is raised for it";
	}


	// Two colonists clear different blockers in one footprint. The tree falls while the other
	// colonist is still on his way to the bush; that must not end his clear (same task, same goal).
	// Reaching the bush only after the tree fell, the harvest he starts on that clear removes the bush
	// rather than leaving it to regrow, and the site leaves Clearing.
	TEST_F(FootprintClearingTest, FirstClearToFinishDoesNotResetAnother) {
		const glm::vec2 tree{2.0F, 2.0F};
		const glm::vec2 bush{6.0F, 6.0F};
		plantTrees({tree}, {bush});
		createColonist({1.0F, 1.0F}, {tree});
		auto stripper = createColonist({30.0F, 30.0F}, {});
		remember(*world->getComponent<Memory>(stripper), bush, kBush);
		world->getComponent<Inventory>(stripper)->addItem("Berry", 5); // fed: he strips the bush only to clear it

		ASSERT_TRUE(runUntil([&] { return !treeStands(tree); }, 3000, stripper)) << "precondition: the tree is felled first";
		auto* task = world->getComponent<Task>(stripper);
		ASSERT_EQ(task->type, TaskType::Harvest) << "precondition: he is on his way to the bush";
		const uint64_t goalId = task->harvestGoalId;

		bool kept = true;
		runUntil(
			[&] {
				kept = kept && task->type == TaskType::Harvest && task->harvestGoalId == goalId;
				return false;
			},
			60,
			stripper
		);
		EXPECT_TRUE(kept) << "the tree falling must not reset his clear";

		world->getComponent<Position>(stripper)->value = task->targetPosition;
		task->state = TaskState::Arrived;
		bool harvested = false;
		bool sameClear = true;
		ASSERT_TRUE(runUntil([&] {
			const bool harvesting = world->getComponent<Action>(stripper)->type == ActionType::Harvest;
			sameClear = sameClear && (harvested || harvesting || task->harvestGoalId == goalId);
			harvested = harvested || harvesting;
			return harvested && !harvesting;
		})) << "he harvests the bush";
		EXPECT_TRUE(sameClear) << "on the clear he set out on";
		EXPECT_FALSE(treeStands(bush, kBush)) << "that harvest removed the bush";

		EXPECT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; })) << "both are cleared";
		EXPECT_FALSE(treeStands(tree));
	}

	// A footprint over a chunk whose flora isn't placed yet is not known to be clear: the site waits in
	// Clearing, and moves on once the chunk is placed and holds nothing.
	TEST_F(FootprintClearingTest, SiteOverAnUnplacedChunkWaitsInClearing) {
		EXPECT_FALSE(runUntil([&] { return phase() != StructureBlueprint::BuildPhase::Clearing; }, 300))
			<< "nothing placed under the footprint yet";
		placeBareChunk({4.0F, 4.0F});
		EXPECT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; }, 300));
	}

	// Past Clearing the footprint is not rescanned each pass, but it is checked once more as the materials
	// complete: a tree placed under it after the site left Clearing sends it back to Clearing instead of
	// into UnderConstruction.
	TEST_F(FootprintClearingTest, BlockerFoundEnteringConstructionReturnsTheSiteToClearing) {
		placeBareChunk({4.0F, 4.0F});
		ASSERT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; }, 300));

		plantTrees({{3.0F, 3.0F}}); // the chunk's flora, placed late
		runUntil([] { return false; }, 120);
		EXPECT_EQ(phase(), StructureBlueprint::BuildPhase::AwaitingMaterials) << "no rescan while awaiting materials";

		world->getComponent<StructureBlueprint>(site)->delivered = {{"Wood", 10}};
		bool built = false;
		runUntil(
			[&] {
				built = built || phase() == StructureBlueprint::BuildPhase::UnderConstruction;
				return phase() == StructureBlueprint::BuildPhase::Clearing;
			},
			300
		);
		EXPECT_FALSE(built) << "never entered UnderConstruction over the tree";
		EXPECT_EQ(phase(), StructureBlueprint::BuildPhase::Clearing);
	}

	// What a clear can't carry drops just outside the footprint, not on the ground the site will stand on.
	TEST_F(FootprintClearingTest, AClearsSurplusDropsOutsideTheFootprint) {
		std::vector<glm::vec2> drops;
		world->getSystem<ActionSystem>().setDropResourceCallback([&](const std::string&, float x, float y, uint32_t, glm::vec2) { drops.emplace_back(x, y); });
		const std::vector<glm::vec2> trees{{2.0F, 2.0F}};
		plantTrees(trees);
		createColonist({1.0F, 1.0F}, trees);

		ASSERT_TRUE(runUntil([&] { return !treeStands(trees[0]); })) << "the tree is felled";
		ASSERT_EQ(drops.size(), 1U) << "six of the twenty wood do not fit the arms";
		const glm::vec2 at = drops.front();
		EXPECT_TRUE(at.x < 0.0F || at.x > 8.0F || at.y < 0.0F || at.y > 8.0F) << "dropped at (" << at.x << ", " << at.y << ")";
		EXPECT_LT(glm::distance(at, trees[0]), 3.0F) << "beside the footprint edge nearest the tree";
	}

	// A loose pile lying on the footprint neither holds the site back nor is built over: as the site
	// enters construction it is dropped again just outside.
	TEST_F(FootprintClearingTest, APileOnTheFootprintIsMovedOffWhenBuildingStarts) {
		struct Drop {
			glm::vec2 at;
			uint32_t  quantity;
			glm::vec2 fallback;
		};
		std::vector<Drop> drops;
		construction->setDropResourceCallback([&](const std::string&, float x, float y, uint32_t quantity, glm::vec2 fallback) {
			drops.push_back({{x, y}, quantity, fallback});
		});
		placeBareChunk({4.0F, 4.0F});
		const EntityID pile = world->createEntity();
		world->addComponent<Position>(pile, Position{{5.0F, 4.0F}});
		world->addComponent<Appearance>(pile, Appearance{"Berry", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		makeLoosePile(*world, pile, assets::AssetRegistry::Get().getDefNameId("Berry"), {5.0F, 4.0F}, 7);
		world->getComponent<StructureBlueprint>(site)->delivered = {{"Wood", 10}};

		ASSERT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::UnderConstruction; }, 300)) << "the pile does not block the site";
		EXPECT_FALSE(world->isAlive(pile));
		ASSERT_EQ(drops.size(), 1U);
		EXPECT_EQ(drops.front().quantity, 7U);
		EXPECT_GT(drops.front().at.x, 8.0F) << "out past the nearest edge";
		EXPECT_EQ(drops.front().fallback, glm::vec2(5.0F, 4.0F)) << "with no ground past the edge, the search restarts from the old spot";
	}

	// A foundation is built on bare ground. Grass is not harvestable, so it never holds the site in
	// Clearing; as building starts it is gone from the footprint, a placed thing to carry is dropped
	// again just outside as a pile is, and what grows outside the footprint is left alone.
	TEST_F(FootprintClearingTest, TheFootprintIsBaredWhenBuildingStarts) {
		registerGrass();

		std::vector<glm::vec2> drops;
		construction->setDropResourceCallback([&](const std::string& defName, float x, float y, uint32_t quantity, glm::vec2) {
			EXPECT_EQ(defName, "Berry");
			EXPECT_EQ(quantity, 1U);
			drops.emplace_back(x, y);
		});

		const glm::vec2				 tuft{3.0F, 3.0F};
		const glm::vec2				 berry{5.0F, 4.0F};
		const glm::vec2				 outside{9.0F, 3.0F};
		const world::ChunkCoordinate coord = world::worldToChunk({4.0F, 4.0F});
		assets::AsyncChunkPlacementResult chunk;
		chunk.coord = coord;
		chunk.spatialIndex.insert({kGrass, tuft});
		chunk.spatialIndex.insert({"Berry", berry});
		chunk.spatialIndex.insert({kGrass, outside});
		executor.storeChunkResult(std::move(chunk));
		processed.insert(coord);
		world->getComponent<StructureBlueprint>(site)->delivered = {{"Wood", 10}};

		ASSERT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::UnderConstruction; }, 300)) << "grass does not block the site";
		const assets::SpatialIndex* index = executor.getChunkIndex(coord);
		EXPECT_FALSE(index->contains(tuft, kGrass)) << "no grass is built over";
		EXPECT_FALSE(index->contains(berry, "Berry"));
		EXPECT_TRUE(index->contains(outside, kGrass)) << "grass beside the site still grows";
		ASSERT_EQ(drops.size(), 1U) << "the berry is dropped again, not lost";
		EXPECT_GT(drops.front().x, 8.0F) << "out past the nearest edge";
	}

	// A foundation the dev tools complete at once skips the clear and the start of building. Its ground
	// is bared all the same, the tree standing on it included.
	TEST_F(FootprintClearingTest, AFoundationCompletedAtOnceIsBaredToo) {
		const glm::vec2 tree{2.0F, 2.0F};
		plantTrees({tree});

		ASSERT_TRUE(construction->forceCompleteBlueprint(site));
		runUntil([] { return false; }, 60);
		EXPECT_FALSE(treeStands(tree));
	}

	// The footprint is the foundation's ring, not its bounding box. A tree in an L's notch neither holds
	// the site in Clearing nor is felled; the one on the floor is, and what that clear can't carry drops
	// just past the floor's nearest edge, which here opens onto the notch.
	TEST_F(FootprintClearingTest, AnLShapedSiteClearsItsFloorAndLeavesItsNotch) {
		reshapeSite(lShapedRing());
		std::vector<glm::vec2> drops;
		world->getSystem<ActionSystem>().setDropResourceCallback([&](const std::string&, float x, float y, uint32_t, glm::vec2) { drops.emplace_back(x, y); });
		const glm::vec2 floor{3.0F, 6.5F};
		const glm::vec2 notch{6.0F, 6.0F};
		plantTrees({floor, notch});
		createColonist({1.0F, 1.0F}, {floor, notch});

		ASSERT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; })) << "the notch's tree does not hold the site";
		EXPECT_FALSE(treeStands(floor));
		EXPECT_TRUE(treeStands(notch)) << "nothing is built in the notch, so its tree is left";
		ASSERT_EQ(drops.size(), 1U) << "six of the twenty wood do not fit the arms";
		EXPECT_FALSE(onRing(lShapedRing(), drops.front())) << "dropped at (" << drops.front().x << ", " << drops.front().y << ")";
		EXPECT_NEAR(drops.front().x, 4.5F, 0.01F) << "past the inner edge a metre away, not the top edge a metre and a half away";
		EXPECT_NEAR(drops.front().y, 6.5F, 0.01F);
	}

	// Baring follows the ring too: grass, a placed stone to carry, and a loose pile on the floor go, the
	// same three in the notch stay, and what is carried off lands outside the ring.
	TEST_F(FootprintClearingTest, AnLShapedSiteIsBaredOnlyWhereItsFloorGoes) {
		registerGrass();
		reshapeSite(lShapedRing());
		std::vector<glm::vec2> drops;
		construction->setDropResourceCallback([&](const std::string&, float x, float y, uint32_t, glm::vec2) { drops.emplace_back(x, y); });

		const glm::vec2				 floorTuft{2.0F, 6.0F};
		const glm::vec2				 notchTuft{6.0F, 6.0F};
		const glm::vec2				 floorStone{6.0F, 3.0F};
		const glm::vec2				 notchStone{7.0F, 5.0F};
		const world::ChunkCoordinate coord = world::worldToChunk({4.0F, 4.0F});
		assets::AsyncChunkPlacementResult chunk;
		chunk.coord = coord;
		chunk.spatialIndex.insert({kGrass, floorTuft});
		chunk.spatialIndex.insert({kGrass, notchTuft});
		chunk.spatialIndex.insert({"Berry", floorStone});
		chunk.spatialIndex.insert({"Berry", notchStone});
		executor.storeChunkResult(std::move(chunk));
		processed.insert(coord);

		const auto pileAt = [&](glm::vec2 at) {
			const EntityID pile = world->createEntity();
			world->addComponent<Position>(pile, Position{at});
			world->addComponent<Appearance>(pile, Appearance{"Berry", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			makeLoosePile(*world, pile, assets::AssetRegistry::Get().getDefNameId("Berry"), at, 4);
			return pile;
		};
		const EntityID floorPile = pileAt({2.0F, 2.0F});
		const EntityID notchPile = pileAt({6.0F, 7.0F});
		world->getComponent<StructureBlueprint>(site)->delivered = {{"Wood", 10}};

		ASSERT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::UnderConstruction; }, 300));
		const assets::SpatialIndex* index = executor.getChunkIndex(coord);
		EXPECT_FALSE(index->contains(floorTuft, kGrass));
		EXPECT_TRUE(index->contains(notchTuft, kGrass)) << "grass in the notch still grows";
		EXPECT_FALSE(index->contains(floorStone, "Berry"));
		EXPECT_TRUE(index->contains(notchStone, "Berry")) << "a stone in the notch is not carried off";
		EXPECT_FALSE(world->isAlive(floorPile));
		EXPECT_TRUE(world->isAlive(notchPile)) << "a pile in the notch is not moved";
		ASSERT_EQ(drops.size(), 2U) << "the floor's stone and pile";
		for (const glm::vec2& drop : drops) {
			EXPECT_FALSE(onRing(lShapedRing(), drop)) << "dropped at (" << drop.x << ", " << drop.y << ")";
		}
	}

	// A long diagonal strip's bounding box is many times its area: only what stands on the strip is cleared.
	TEST_F(FootprintClearingTest, ADiagonalStripClearsOnlyTheStrip) {
		reshapeSite({{0, 0}, {2000, 0}, {22000, 20000}, {20000, 20000}});
		const glm::vec2 onStrip{11.0F, 10.0F};
		const glm::vec2 offStrip{5.0F, 15.0F};
		plantTrees({onStrip, offStrip});
		createColonist({1.0F, 1.0F}, {onStrip, offStrip});

		ASSERT_TRUE(runUntil([&] { return phase() == StructureBlueprint::BuildPhase::AwaitingMaterials; }));
		EXPECT_FALSE(treeStands(onStrip));
		EXPECT_TRUE(treeStands(offStrip)) << "inside the strip's bounding box but off the strip";
	}

} // namespace ecs::test
