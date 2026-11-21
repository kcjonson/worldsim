#pragma once

// UserSettings - the player's game preferences, set from the Settings scene.
//
// Values are their defaults with the settings file read over them, by load(): once at launch, and again when
// a game loads the construction config, since only then are the drawing defaults read. The Settings scene
// changes them in place and writes them back as they change, so the second load keeps them. The file is the
// App's (settings.json, under "drawing" and "landing"), so --no-settings neither reads nor saves them.

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

	/// The world creator's options (landing-sites.md section 6).
	struct LandingSettings {
		bool showSuggestedSites = false;

		bool operator==(const LandingSettings&) const = default;
	};

	class UserSettings {
	  public:
		static UserSettings& Get() {
			static UserSettings instance;
			return instance;
		}

		/// Starts from snapping.xml's DrawingSettingsDefaults as the registry holds them when this
		/// singleton is first touched.
		DrawingSettings drawing;
		/// Off until the player turns a switch on; no config file sets these defaults.
		LandingSettings landing;

		/// The defaults, then what the file holds over them; a missing or malformed value keeps
		/// its default.
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
		UserSettings()
			: drawing(registryDefaults()) {}

		static DrawingSettings registryDefaults() {
			const assets::DrawingSettingsDefaults& defaults = assets::ConstructionRegistry::Get().snapping().drawingSettingsDefaults;
			return DrawingSettings{
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
