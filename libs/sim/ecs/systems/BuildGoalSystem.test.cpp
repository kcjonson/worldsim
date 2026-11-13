#include "BuildGoalSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Packaged.h"
#include "../components/Transform.h"

#include <gtest/gtest.h>

#include <memory>

namespace ecs {

	class BuildGoalSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			GoalTaskRegistry::Get().clear();
			world = std::make_unique<World>();
			world->registerSystem<BuildGoalSystem>();
		}

		void TearDown() override {
			GoalTaskRegistry::Get().clear();
			world.reset();
		}

		void pumpPastThrottle() {
			for (int i = 0; i < 35; ++i) {
				world->update(0.016F);
			}
		}

		// A box whose storage goal (the umbrella StorageGoalSystem keeps for a box with rules) is up.
		EntityID boxWithStorageGoal() {
			const EntityID box = world->createEntity();
			world->addComponent<Position>(box, Position{{2.0F, 2.0F}});
			GoalTask goal;
			goal.type = TaskType::Haul;
			goal.owner = GoalOwner::StorageGoalSystem;
			goal.destinationEntity = box;
			storageGoalId = GoalTaskRegistry::Get().createGoal(std::move(goal));
			return box;
		}

		std::unique_ptr<World> world;
		uint64_t			   storageGoalId = 0;
	};

	TEST_F(BuildGoalSystemTest, APackagedBoxWithAStorageGoalStillGetsAPlacementGoal) {
		const EntityID box = boxWithStorageGoal();
		world->addComponent<Packaged>(box, Packaged{glm::vec2{3.0F, 2.0F}, false});

		pumpPastThrottle();

		const GoalTask* goal = GoalTaskRegistry::Get().getGoalByDestination(box);
		ASSERT_NE(goal, nullptr);
		EXPECT_EQ(goal->type, TaskType::PlacePackaged);
		EXPECT_EQ(goal->owner, GoalOwner::BuildGoalSystem);
		EXPECT_EQ(GoalTaskRegistry::Get().getGoalsOfType(TaskType::Haul).size(), 0U) << "the storage goal is retired, not left beside it";
	}

	TEST_F(BuildGoalSystemTest, ABoxAwaitingItsSpotKeepsItsStorageGoal) {
		const EntityID box = boxWithStorageGoal();
		world->addComponent<Packaged>(box, Packaged{std::nullopt, false});

		pumpPastThrottle();

		const GoalTask* goal = GoalTaskRegistry::Get().getGoalByDestination(box);
		ASSERT_NE(goal, nullptr);
		EXPECT_EQ(goal->id, storageGoalId);
		EXPECT_EQ(goal->type, TaskType::Haul);
	}

} // namespace ecs
