#include "UserSettings.h"

#include <settings/Settings.h>

#include <gtest/gtest.h>

namespace {

	class UserSettingsFile : public ::testing::Test {
	  protected:
		void SetUp() override { playerSettings = world_sim::UserSettings::Get().drawing; }
		void TearDown() override { world_sim::UserSettings::Get().drawing = playerSettings; }

		static world_sim::DrawingSettings defaults() {
			world_sim::UserSettings& settings = world_sim::UserSettings::Get();
			settings.load(app::Settings{});
			return settings.drawing;
		}

		world_sim::DrawingSettings playerSettings;
	};

} // namespace

// Every switch flipped from its default, so a key stored to the wrong field reads back wrong.
TEST_F(UserSettingsFile, DrawingSettingsComeBackAsTheyWereStored) {
	world_sim::UserSettings&		 settings = world_sim::UserSettings::Get();
	const world_sim::DrawingSettings start = defaults();
	const world_sim::DrawingSettings stored{
		.angleSteps = !start.angleSteps,
		.guidesFromShape = !start.guidesFromShape,
		.alignToExistingBuildings = !start.alignToExistingBuildings,
		.guidesFromWorldAxes = !start.guidesFromWorldAxes,
		.guideCrossings = !start.guideCrossings,
		.commonLengths = !start.commonLengths,
		.guideDistances = !start.guideDistances,
		.angleReadout = !start.angleReadout,
		.roundLengths = !start.roundLengths,
		.showKeyGuide = !start.showKeyGuide,
		.rectangleGridSpacingMeters = start.rectangleGridSpacingMeters == 1.0F ? 0.5F : 1.0F,
	};
	settings.drawing = stored;
	app::Settings file;
	settings.store(file);

	settings.load(file);
	EXPECT_EQ(settings.drawing, stored);
}

TEST_F(UserSettingsFile, MalformedOrUnofferedValuesKeepTheDefaults) {
	world_sim::UserSettings&		 settings = world_sim::UserSettings::Get();
	const world_sim::DrawingSettings expected = defaults();
	app::Settings					 file;
	file.set("drawing.angleSteps", "yes");
	file.set("drawing.rectangleGridSpacingMeters", 0.25F);

	settings.load(file);
	EXPECT_EQ(settings.drawing, expected);
}
