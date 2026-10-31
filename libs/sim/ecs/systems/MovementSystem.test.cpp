// Movement integrates on game time: PhysicsSystem scales the integration step by
// TimeSystem::effectiveTimeScale (so fast-forward moves colonists faster and pause freezes them),
// and MovementSystem caps each step to the current target so the larger fast-forward step can't
// overshoot the arrival threshold. These tests pin all three behaviors, with the still-arrives-at-10x
// case guarding the overshoot/ping-pong regression that the step cap exists to prevent.

#include "MovementSystem.h"
#include "PhysicsSystem.h"
#include "TimeSystem.h"

#include "../World.h"
#include "../components/Movement.h"
#include "../components/NavPath.h"
#include "../components/Transform.h"

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include <memory>

namespace ecs {
namespace {

class MovementTimeScaleTest : public ::testing::Test {
  protected:
	void SetUp() override {
		world = std::make_unique<World>();
		timeSystem = &world->registerSystem<TimeSystem>();
		world->registerSystem<MovementSystem>();
		world->registerSystem<PhysicsSystem>();
	}

	EntityID createMover(glm::vec2 start, glm::vec2 target, float speed = 2.0F) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{start});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{target, speed, true});
		return entity;
	}

	// Tick `dt` up to maxUpdates times; return the tick on which the mover arrived (MovementTarget
	// cleared its active flag), or -1 if it never did.
	int runToArrival(EntityID entity, float dt, int maxUpdates) {
		for (int i = 1; i <= maxUpdates; ++i) {
			world->update(dt);
			if (!world->getComponent<MovementTarget>(entity)->active) {
				return i;
			}
		}
		return -1;
	}

	std::unique_ptr<World> world;
	TimeSystem*			   timeSystem = nullptr;
};

// At 1x the step is 0.1 m/tick and the target is a multiple of it, so the mover lands cleanly.
constexpr float kDt = 0.05F;
constexpr glm::vec2 kTarget{9.5F, 0.0F};

// Pause freezes movement: an active mover holds position and never arrives.
TEST_F(MovementTimeScaleTest, PauseFreezesMovement) {
	timeSystem->setSpeed(GameSpeed::Paused);
	auto entity = createMover({0.0F, 0.0F}, kTarget);

	for (int i = 0; i < 20; ++i) {
		world->update(kDt);
	}

	const auto* pos = world->getComponent<Position>(entity);
	EXPECT_FLOAT_EQ(pos->value.x, 0.0F) << "Paused colonist must not move";
	EXPECT_FLOAT_EQ(pos->value.y, 0.0F);
	EXPECT_TRUE(world->getComponent<MovementTarget>(entity)->active) << "Still en route, just frozen";
}

// Fast-forward speeds movement: 10x reaches the same target in strictly fewer ticks than 1x.
TEST_F(MovementTimeScaleTest, FastForwardArrivesInFewerTicks) {
	timeSystem->setSpeed(GameSpeed::Normal);
	const int normalTicks = runToArrival(createMover({0.0F, 0.0F}, kTarget), kDt, 1000);
	ASSERT_GT(normalTicks, 0) << "Colonist arrives at 1x";

	timeSystem->setSpeed(GameSpeed::VeryFast);
	const int fastTicks = runToArrival(createMover({0.0F, 0.0F}, kTarget), kDt, 1000);
	ASSERT_GT(fastTicks, 0) << "Colonist still arrives at 10x";

	EXPECT_LT(fastTicks, normalTicks) << "10x covers the distance in fewer real ticks";
}

// Regression guard: at 10x the per-tick step (~1.0 m) dwarfs the 0.1 m arrival threshold, so without
// the step cap the mover would overshoot and circle its target forever, never registering Arrived
// (which would freeze chopping/crafting at fast speeds). Assert it both arrives and stops on target.
TEST_F(MovementTimeScaleTest, FastForwardStillArrivesWithoutOvershoot) {
	timeSystem->setSpeed(GameSpeed::VeryFast);
	auto entity = createMover({0.0F, 0.0F}, kTarget);

	const int ticks = runToArrival(entity, kDt, 200);
	ASSERT_GT(ticks, 0) << "10x movement must still arrive, not ping-pong around the goal";

	const auto* pos = world->getComponent<Position>(entity);
	EXPECT_LT(glm::distance(pos->value, kTarget), 0.1F) << "Stops on the target, not short or past it";
}

// The in-game path: MovementSystem follows a NavPath's waypoints (the direct-movement branch above is
// only the headless no-NavigationSystem fallback). At 10x the step cap must still land on each corner,
// so the mover advances through the intermediates and registers Arrived rather than cutting a corner or
// circling the goal.
TEST_F(MovementTimeScaleTest, FastForwardFollowsNavPathAndArrives) {
	timeSystem->setSpeed(GameSpeed::VeryFast);
	const glm::vec2 goal{6.0F, 3.0F};
	auto			entity = createMover({0.0F, 0.0F}, goal);

	NavPath path;
	path.waypoints = {{3.0F, 0.0F}, {3.0F, 3.0F}, goal}; // right, up, right -- two corners to round
	path.valid	   = true;
	world->addComponent<NavPath>(entity, path);

	const int ticks = runToArrival(entity, kDt, 400);
	ASSERT_GT(ticks, 0) << "10x NavPath movement must advance through the waypoints and arrive";

	const auto* pos = world->getComponent<Position>(entity);
	EXPECT_LT(glm::distance(pos->value, goal), 0.1F) << "stops on the final waypoint";
	EXPECT_TRUE(world->getComponent<NavPath>(entity)->done()) << "the path is fully consumed";
}

}  // namespace
}  // namespace ecs
