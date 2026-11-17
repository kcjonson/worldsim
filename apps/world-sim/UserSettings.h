#pragma once

// UserSettings - the player's game preferences, set from the Settings scene.
//
// Values default from the base config XML (read once on first access), are read over from the settings
// file at launch, and are changed in place by the Settings scene, which writes them back. The file is the
// App's (settings.json, under "drawing"), so --no-settings neither reads nor saves them.

#include <assets/ConstructionRegistry.h>
#include <construction/DrawingAids.h>

namespace app {
	class Settings;
} // namespace app

namespace world_sim {

	/// The player's Drawing Settings (building-construction D14). The helper switches
	/// and the grid spacing reach the snap engine through drawingAids(); guide
	/// distances, the angle readout, and the key guide are display only.
	struct DrawingSettings {
		bool  angleSteps = false;
		bool  guidesFromShape = false;
		bool  alignToExistingBuildings = false;
		bool  guidesFromWorldAxes = false;
		bool  guideCrossings = false;
		bool  commonLengths = false;
		bool  guideDistances = false;
		bool  angleReadout = false;
		bool  roundLengths = false;
		bool  showKeyGuide = false;
		float rectangleGridSpacingMeters = 1.0F;

		bool operator==(const DrawingSettings&) const = default;
	};

	class UserSettings {
	  public:
		static UserSettings& Get() {
			static UserSettings instance;
			return instance;
		}

		/// Starts from snapping.xml's DrawingSettingsDefaults, read once when this
		/// singleton is first touched.
		DrawingSettings drawing;

		/// Takes what the file holds over the defaults; a missing or malformed value keeps its default.
		void load(const app::Settings& file);
		/// Writes every value into the file's in-memory settings; the caller saves the file.
		void store(app::Settings& file) const;

		/// The engine-facing part of `drawing`.
		[[nodiscard]] construction::DrawingAids drawingAids() const {
			return construction::DrawingAids{
				.angleSteps = drawing.angleSteps,
				.guidesFromShape = drawing.guidesFromShape,
				.alignToExistingBuildings = drawing.alignToExistingBuildings,
				.guidesFromWorldAxes = drawing.guidesFromWorldAxes,
				.guideCrossings = drawing.guideCrossings,
				.commonLengths = drawing.commonLengths,
				.roundLengths = drawing.roundLengths,
				.rectangleGridSpacingMeters = drawing.rectangleGridSpacingMeters,
			};
		}

	  private:
		UserSettings() {
			const assets::DrawingSettingsDefaults& defaults = assets::ConstructionRegistry::Get().snapping().drawingSettingsDefaults;
			drawing = DrawingSettings{
				.angleSteps = defaults.angleSteps,
				.guidesFromShape = defaults.guidesFromShape,
				.alignToExistingBuildings = defaults.alignToExistingBuildings,
				.guidesFromWorldAxes = defaults.guidesFromWorldAxes,
				.guideCrossings = defaults.guideCrossings,
				.commonLengths = defaults.commonLengths,
				.guideDistances = defaults.guideDistances,
				.angleReadout = defaults.angleReadout,
				.roundLengths = defaults.roundLengths,
				.showKeyGuide = defaults.showKeyGuide,
				.rectangleGridSpacingMeters = defaults.rectangleGridSpacingMeters,
			};
		}
	};

} // namespace world_sim
