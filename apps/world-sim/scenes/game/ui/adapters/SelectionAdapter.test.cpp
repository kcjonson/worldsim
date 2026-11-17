#include "SelectionAdapter.h"

#include <assets/MoodConfig.h>
#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Mood.h>

#include <gtest/gtest.h>

#include <filesystem>

namespace world_sim {

	namespace {
		std::filesystem::path shippedMoodConfig() {
			// <root>/apps/world-sim/scenes/game/ui/adapters/SelectionAdapter.test.cpp -> <root>/assets/config/colonists/mood.xml
			std::filesystem::path root = std::filesystem::path(__FILE__);
			for (int level = 0; level < 7; ++level) {
				root = root.parent_path();
			}
			return root / "assets" / "config" / "colonists" / "mood.xml";
		}
	} // namespace

	class SelectionAdapterTest : public ::testing::Test {
	  protected:
		void SetUp() override { ASSERT_TRUE(assets::MoodConfig::Get().loadFromFile(shippedMoodConfig().parent_path(), shippedMoodConfig().filename())); }
		void TearDown() override { assets::MoodConfig::Get().clear(); }

		ecs::World world;
	};

	TEST_F(SelectionAdapterTest, ColonistMoodLabelIsTheConfiguredBand) {
		const ecs::EntityID colonist = world.createEntity();
		world.addComponent<ecs::Colonist>(colonist, ecs::Colonist{"Ada"});
		world.addComponent<ecs::Mood>(colonist, ecs::Mood{.value = 40.0F});

		PanelContent content = adaptColonistStatus(world, colonist);
		ASSERT_TRUE(content.colonist.has_value());
		EXPECT_FLOAT_EQ(content.colonist->moodValue, 40.0F);
		EXPECT_EQ(content.colonist->moodLabel, "Low");

		world.getComponent<ecs::Mood>(colonist)->value = 11.9F;
		content = adaptColonistStatus(world, colonist);
		EXPECT_EQ(content.colonist->moodLabel, "Collapsing");
	}

} // namespace world_sim
