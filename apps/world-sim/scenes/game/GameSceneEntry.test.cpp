// The game scene is built only over a loaded world: a switch to it with none waiting (the dev server's scene
// action, reload, --scene=game) builds the loading scene, which loads the config the game's state views read.

#include "GameWorldState.h"
#include "SceneTypes.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

namespace world_sim::testing {

	namespace {

		std::string_view builtSceneName() {
			const std::unique_ptr<app::Scene> scene = scenes::Game.factory();
			return scene != nullptr ? scene->name() : std::string_view{};
		}

	} // namespace

	TEST(GameSceneEntry, SwitchWithNoLoadedWorldBuildsTheLoadingScene) {
		ASSERT_FALSE(GameWorldState::HasPending());
		EXPECT_EQ(builtSceneName(), std::string_view(scenes::GameLoading.name));
	}

	TEST(GameSceneEntry, SwitchWithALoadedWorldWaitingBuildsTheGameScene) {
		GameWorldState::SetPending(std::make_unique<GameWorldState>());
		EXPECT_EQ(builtSceneName(), std::string_view(scenes::Game.name));
		EXPECT_TRUE(GameWorldState::HasPending()) << "the game scene takes the world on enter, not when it's built";
		GameWorldState::Take();
	}

} // namespace world_sim::testing
