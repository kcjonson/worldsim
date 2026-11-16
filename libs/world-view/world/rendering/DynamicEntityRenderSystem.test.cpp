#include "DynamicEntityRenderSystem.h"

#include <ecs/World.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>
#include <ecs/systems/VisionSystem.h>

#include <gtest/gtest.h>

#include <algorithm>

namespace {

	constexpr const char* kMoverDefName = "Test_RenderedMover";

	class MoverRenderTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			colonist = addEntity({0.0F, 0.0F}, "Test_RenderedColonist");
			world.addComponent<ecs::Colonist>(colonist, ecs::Colonist{"Tester"});
			ecs::Memory memory;
			memory.sightRadius = 15.0F;
			world.addComponent<ecs::Memory>(colonist, std::move(memory));
			world.addComponent<ecs::AgentRadius>(colonist, ecs::AgentRadius{});
			for (int tick = 0; tick < 12; ++tick) {
				world.update(0.016F);
			}
		}

		ecs::EntityID addEntity(glm::vec2 at, const char* defName) {
			const ecs::EntityID entity = world.createEntity();
			world.addComponent<ecs::Position>(entity, ecs::Position{at});
			world.addComponent<ecs::Rotation>(entity, ecs::Rotation{0.0F});
			world.addComponent<ecs::Appearance>(entity, ecs::Appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			return entity;
		}

		// A thing that moves by itself and is not a colonist.
		ecs::EntityID addMover(glm::vec2 at) {
			const ecs::EntityID mover = addEntity(at, kMoverDefName);
			world.addComponent<ecs::AgentRadius>(mover, ecs::AgentRadius{});
			return mover;
		}

		[[nodiscard]] std::size_t drawn(const char* defName) {
			world.update(0.016F);
			const auto& data = render.getRenderData();
			return static_cast<std::size_t>(std::count_if(data.begin(), data.end(), [defName](const assets::PlacedEntity& placed) {
				return placed.defName == defName;
			}));
		}

		ecs::World								world;
		ecs::VisionSystem&						vision = world.registerSystem<ecs::VisionSystem>();
		ecs::PlayerVisibilitySystem&			visibility = world.registerSystem<ecs::PlayerVisibilitySystem>();
		worldview::DynamicEntityRenderSystem&	render = world.registerSystem<worldview::DynamicEntityRenderSystem>();
		ecs::EntityID							colonist = ecs::kInvalidEntity;
	};

} // namespace

TEST_F(MoverRenderTest, MoverOutOfEveryColonistsSightIsAbsentFromTheRenderData) {
	addMover({100.0F, 0.0F});
	ASSERT_FALSE(visibility.isVisible({100.0F, 0.0F}));
	EXPECT_EQ(drawn(kMoverDefName), 0U);
}

TEST_F(MoverRenderTest, MoverInsideAColonistsSightIsPresent) {
	addMover({5.0F, 0.0F});
	ASSERT_TRUE(visibility.isVisible({5.0F, 0.0F}));
	EXPECT_EQ(drawn(kMoverDefName), 1U);
}

TEST_F(MoverRenderTest, ColonistsAndThingsThatDoNotMoveAlwaysDraw) {
	addEntity({100.0F, 0.0F}, "Test_RenderedStill");
	ASSERT_FALSE(visibility.isVisible({100.0F, 0.0F}));
	EXPECT_EQ(drawn("Test_RenderedStill"), 1U);
	EXPECT_EQ(drawn("Test_RenderedColonist"), 1U);
}

TEST_F(MoverRenderTest, RevealDrawsEveryMover) {
	addMover({100.0F, 0.0F});
	visibility.setRevealed(true);
	EXPECT_EQ(drawn(kMoverDefName), 1U);
}
