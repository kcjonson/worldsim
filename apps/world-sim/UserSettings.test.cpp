#include "UserSettings.h"

#include <settings/Settings.h>

#include <gtest/gtest.h>

namespace {

	class UserSettingsFile : public ::testing::Test {
	  protected:
		void SetUp() override { playerSettings = world_sim::UserSettings::Get().drawing; }
		void TearDown() override { world_sim::UserSettings::Get().drawing = playerSettings; }

		world_sim::DrawingSettings playerSettings;
	};

} // namespace

TEST_F(UserSettingsFile, DrawingSettingsComeBackAsTheyWereStored) {
	world_sim::UserSettings& settings = world_sim::UserSettings::Get();
	const world_sim::DrawingSettings stored{
		.angleSteps = true,
		.guideCrossings = true,
		.angleReadout = true,
		.showKeyGuide = true,
		.rectangleGridSpacingMeters = 0.5F,
	};
	settings.drawing = stored;
	app::Settings file;
	settings.store(file);

	settings.drawing = world_sim::DrawingSettings{};
	settings.load(file);
	EXPECT_EQ(settings.drawing, stored);
}

TEST_F(UserSettingsFile, MissingOrUnofferedValuesKeepTheDefaults) {
	world_sim::UserSettings& settings = world_sim::UserSettings::Get();
	const world_sim::DrawingSettings defaults{.roundLengths = true, .rectangleGridSpacingMeters = 1.0F};
	settings.drawing = defaults;
	app::Settings file;
	file.set("drawing.angleSteps", "yes");
	file.set("drawing.rectangleGridSpacingMeters", 0.25F);

	settings.load(file);
	EXPECT_EQ(settings.drawing, defaults);
}
