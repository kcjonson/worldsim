#pragma once

// UserSettings - runtime, session-scoped user preferences.
//
// Minimal home for player-facing toggles set from the main-menu settings screen.
// Values default from the base config XML (read once on first access) and are
// mutated in place by the settings UI for the rest of the session. There is no
// cross-launch persistence yet; when a user-settings file lands, this is where
// load/save hangs.

#include <assets/ConstructionRegistry.h>

namespace world_sim {

	class UserSettings {
	  public:
		static UserSettings& Get() {
			static UserSettings instance;
			return instance;
		}

		// Whether construction alignment guides also snap to the corners of existing
		// (committed) foundations. The in-progress shape's own nodes are always guide
		// targets regardless. Default comes from snapping.xml (alignToExistingDefault),
		// read once when this singleton is first touched.
		bool alignSnapToExistingFoundations;

	  private:
		UserSettings()
			: alignSnapToExistingFoundations(
				  engine::assets::ConstructionRegistry::Get().snapping().alignToExistingDefault) {}
	};

} // namespace world_sim
