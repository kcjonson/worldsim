#pragma once

// The names salal's per-user directories are made from (app::userDataDir, AppConfig::organization and
// applicationName). world-sim is the game; the asset-manager names its organization from here too, and
// keeps the ground bake cache under the game's data directory, so a bake either app stores warms the other.

namespace worldview {

	inline constexpr char kOrganization[] = "kcjonson";
	inline constexpr char kGameName[] = "world-sim";

} // namespace worldview
