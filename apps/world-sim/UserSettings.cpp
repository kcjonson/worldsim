#include "UserSettings.h"

#include <settings/Settings.h>

#include <array>
#include <string>
#include <utility>

namespace world_sim {

	namespace {

		constexpr const char* kGridSpacingKey = "drawing.rectangleGridSpacingMeters";

		const std::array<std::pair<const char*, bool DrawingSettings::*>, 10> kSwitches{{
			{"drawing.angleSteps", &DrawingSettings::angleSteps},
			{"drawing.guidesFromShape", &DrawingSettings::guidesFromShape},
			{"drawing.alignToExistingBuildings", &DrawingSettings::alignToExistingBuildings},
			{"drawing.guidesFromWorldAxes", &DrawingSettings::guidesFromWorldAxes},
			{"drawing.guideCrossings", &DrawingSettings::guideCrossings},
			{"drawing.commonLengths", &DrawingSettings::commonLengths},
			{"drawing.guideDistances", &DrawingSettings::guideDistances},
			{"drawing.angleReadout", &DrawingSettings::angleReadout},
			{"drawing.roundLengths", &DrawingSettings::roundLengths},
			{"drawing.showKeyGuide", &DrawingSettings::showKeyGuide},
		}};

		const std::array<std::pair<const char*, bool LandingSettings::*>, 1> kLandingSwitches{{
			{"landing.showSuggestedSites", &LandingSettings::showSuggestedSites},
		}};

		// The spacings the Settings scene offers; anything else in the file is ignored.
		bool offeredGridSpacing(float meters) {
			return meters == 1.0F || meters == 0.5F;
		}

	} // namespace

	void UserSettings::load(const app::Settings& file) {
		drawing = registryDefaults();
		for (const auto& [key, field] : kSwitches) {
			drawing.*field = file.get(key, drawing.*field);
		}
		const float spacing = file.get(kGridSpacingKey, drawing.rectangleGridSpacingMeters);
		if (offeredGridSpacing(spacing)) {
			drawing.rectangleGridSpacingMeters = spacing;
		}
		landing = LandingSettings{};
		for (const auto& [key, field] : kLandingSwitches) {
			landing.*field = file.get(key, landing.*field);
		}
	}

	void UserSettings::store(app::Settings& file) const {
		for (const auto& [key, field] : kSwitches) {
			file.set(key, drawing.*field);
		}
		file.set(kGridSpacingKey, drawing.rectangleGridSpacingMeters);
		for (const auto& [key, field] : kLandingSwitches) {
			file.set(key, landing.*field);
		}
	}

} // namespace world_sim
