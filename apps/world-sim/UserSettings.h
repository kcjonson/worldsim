#pragma once

// UserSettings - runtime, session-scoped user preferences.
//
// Minimal home for player-facing toggles set from the main-menu settings screen.
// Values default from the base config XML (read once on first access) and are
// mutated in place by the settings UI for the rest of the session. The drawing
// settings here aren't saved across launches; the interface size and the window's
// size are, in settings.json, through the App (see theme/InterfaceScale.h).

#include <assets/ConstructionRegistry.h>
#include <construction/DrawingAids.h>

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
