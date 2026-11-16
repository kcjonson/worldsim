#include "BuildGoalSystem.h"
#include "StorageGoalSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/Packaged.h"
#include "../components/StorageConfiguration.h"
#include "../components/Transform.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <vector>

namespace ecs {

	// Both systems that put a goal on a storage box run together on real configured boxes, so each
	// one's handling of the other's goal is what the tests see.
	class BoxGoalOwnershipTest : public ::testing::Test {
	  protected:
		static constexpr int kFramesUntilBothPolled = 200;
		static constexpr int kStableFrames = 600;

		void SetUp() override {
			GoalTaskRegistry::Get().clear();
			world = std::make_unique<World>();
			world->registerSystem<StorageGoalSystem>();
			world->registerSystem<BuildGoalSystem>();
		}

		void TearDown() override {
			GoalTaskRegistry::Get().clear();
			world.reset();
		}

		EntityID installedBox(StorageConfiguration config) {
			const EntityID box = world->createEntity();
			world->addComponent<Position>(box, Position{{2.0F, 2.0F}});
			world->addComponent<Inventory>(box, Inventory::createForStorage());
			world->addComponent<StorageConfiguration>(box, std::move(config));
			return box;
		}

		// What PlacementSystem::spawnEntity and the crafted-furniture drain leave: an accept-everything box, packaged.
		EntityID craftedBox(std::optional<glm::vec2> target) {
			const EntityID box = installedBox(StorageConfiguration::createAcceptEverything());
			world->addComponent<Packaged>(box, Packaged{target, false});
			return box;
		}

		// What handleMoveFurniture does to an installed box that already has its storage umbrella.
		EntityID movedBox(std::optional<glm::vec2> target) {
			const EntityID box = installedBox(StorageConfiguration::createAcceptEverything());
			runFrames(kFramesUntilBothPolled);
			const GoalTask* umbrella = GoalTaskRegistry::Get().getGoalByDestination(box);
			EXPECT_NE(umbrella, nullptr) << "an installed box with rules has its storage umbrella before the move";
			world->addComponent<Packaged>(box, Packaged{target, false});
			return box;
		}

		void runFrames(int count) {
			for (int frame = 0; frame < count; ++frame) {
				world->update(0.016F);
			}
		}

		static std::vector<const GoalTask*> goalsOn(EntityID box) {
			std::vector<const GoalTask*> found;
			for (GoalOwner owner : {GoalOwner::StorageGoalSystem,
									GoalOwner::CraftingGoalSystem,
									GoalOwner::BuildGoalSystem,
									GoalOwner::ConstructionGoalSystem}) {
				for (const GoalTask* goal : GoalTaskRegistry::Get().getGoalsByOwner(owner)) {
					if (goal->destinationEntity == box) {
						found.push_back(goal);
					}
				}
			}
			return found;
		}

		// Runs until the box's only goal is BuildGoalSystem's placement goal, and returns its id (0 if it never was).
		uint64_t placementGoalOnceUp(EntityID box) {
			for (int frame = 0; frame < kFramesUntilBothPolled; ++frame) {
				world->update(0.016F);
				const auto goals = goalsOn(box);
				if (goals.size() == 1 && goals.front()->type == TaskType::PlacePackaged) {
					return goals.front()->id;
				}
			}
			return 0;
		}

		// Over kStableFrames the box keeps exactly the one placement goal, under the same id, with nothing written into it.
		void expectPlacementGoalHolds(EntityID box, uint64_t goalId) {
			for (int frame = 0; frame < kStableFrames; ++frame) {
				world->update(0.016F);
				const auto goals = goalsOn(box);
				ASSERT_EQ(goals.size(), 1U) << "frame " << frame;
				const GoalTask& goal = *goals.front();
				ASSERT_EQ(goal.id, goalId) << "frame " << frame;
				ASSERT_EQ(goal.type, TaskType::PlacePackaged);
				ASSERT_EQ(goal.owner, GoalOwner::BuildGoalSystem);
				ASSERT_TRUE(goal.acceptedDefNameIds.empty()) << "frame " << frame;
				ASSERT_EQ(goal.acceptedCategory, assets::ItemCategory::None) << "frame " << frame;
			}
		}

		std::unique_ptr<World> world;
	};

	TEST_F(BoxGoalOwnershipTest, APackagedBoxAwaitingItsSpotHasNoGoal) {
		const EntityID crafted = craftedBox(std::nullopt);
		const EntityID moved = movedBox(std::nullopt);

		runFrames(kFramesUntilBothPolled);

		EXPECT_TRUE(goalsOn(crafted).empty());
		EXPECT_TRUE(goalsOn(moved).empty()) << "the storage umbrella is retired while the box is packaged";
	}

	TEST_F(BoxGoalOwnershipTest, APackagedBoxWithASpotHoldsOnlyItsPlacementGoal) {
		const EntityID crafted = craftedBox(glm::vec2{6.0F, 2.0F});
		const EntityID moved = movedBox(glm::vec2{6.0F, 4.0F});

		const uint64_t craftedGoal = placementGoalOnceUp(crafted);
		const uint64_t movedGoal = placementGoalOnceUp(moved);
		ASSERT_NE(craftedGoal, 0U);
		ASSERT_NE(movedGoal, 0U) << "the placement goal follows the storage umbrella's retirement";

		expectPlacementGoalHolds(crafted, craftedGoal);
		expectPlacementGoalHolds(moved, movedGoal);
	}

	TEST_F(BoxGoalOwnershipTest, ABoxWithNoRulesKeepsItsPlacementGoal) {
		const EntityID box = installedBox(StorageConfiguration::createEmpty());
		world->addComponent<Packaged>(box, Packaged{glm::vec2{6.0F, 2.0F}, false});

		const uint64_t goal = placementGoalOnceUp(box);
		ASSERT_NE(goal, 0U);

		expectPlacementGoalHolds(box, goal);
	}

	TEST_F(BoxGoalOwnershipTest, APlacedBoxTradesItsPlacementGoalForAStorageUmbrella) {
		const EntityID box = craftedBox(glm::vec2{6.0F, 2.0F});
		ASSERT_NE(placementGoalOnceUp(box), 0U);

		world->removeComponent<Packaged>(box);
		runFrames(kFramesUntilBothPolled);

		const auto goals = goalsOn(box);
		ASSERT_EQ(goals.size(), 1U);
		EXPECT_EQ(goals.front()->type, TaskType::Haul);
		EXPECT_EQ(goals.front()->owner, GoalOwner::StorageGoalSystem);
		EXPECT_FALSE(goals.front()->parentGoalId.has_value());
	}

} // namespace ecs
