#include "UserSettings.h"

#include <settings/Settings.h>

#include <gtest/gtest.h>

namespace {

	class UserSettingsFile : public ::testing::Test {
	  protected:
		void SetUp() override {
			playerDrawing = world_sim::UserSettings::Get().drawing;
			playerLanding = world_sim::UserSettings::Get().landing;
		}
		void TearDown() override {
			world_sim::UserSettings::Get().drawing = playerDrawing;
			world_sim::UserSettings::Get().landing = playerLanding;
		}

		static world_sim::DrawingSettings defaults() {
			world_sim::UserSettings& settings = world_sim::UserSettings::Get();
			settings.load(app::Settings{});
			return settings.drawing;
		}

		world_sim::DrawingSettings playerDrawing;
		world_sim::LandingSettings playerLanding;
	};

} // namespace

// Every switch flipped from its default, so a key stored to the wrong field reads back wrong.
TEST_F(UserSettingsFile, SettingsComeBackAsTheyWereStored) {
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
	const world_sim::LandingSettings storedLanding{.showSuggestedSites = true};
	settings.drawing = stored;
	settings.landing = storedLanding;
	app::Settings file;
	settings.store(file);

	settings.load(file);
	EXPECT_EQ(settings.drawing, stored);
	EXPECT_EQ(settings.landing, storedLanding);
}

TEST_F(UserSettingsFile, MalformedOrUnofferedValuesKeepTheDefaults) {
	world_sim::UserSettings&		 settings = world_sim::UserSettings::Get();
	const world_sim::DrawingSettings expected = defaults();
	app::Settings					 file;
	file.set("drawing.angleSteps", "yes");
	file.set("drawing.rectangleGridSpacingMeters", 0.25F);
	file.set("landing.showSuggestedSites", "yes");

	settings.load(file);
	EXPECT_EQ(settings.drawing, expected);
	EXPECT_FALSE(settings.landing.showSuggestedSites);
}

// A --no-settings launch reads an empty file: the suggested-site markers start off, whatever was on before.
TEST_F(UserSettingsFile, SuggestedSitesAreOffWithoutASettingsFile) {
	EXPECT_FALSE(world_sim::LandingSettings{}.showSuggestedSites);
	world_sim::UserSettings& settings = world_sim::UserSettings::Get();
	settings.landing.showSuggestedSites = true;

	settings.load(app::Settings{});
	EXPECT_FALSE(settings.landing.showSuggestedSites);
}
