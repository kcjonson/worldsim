#pragma once

namespace app {
	class App;
}

namespace worldview {
	class GroundTileBank;
}

namespace world_sim {

	/// From main after App::create, before the first scene: starts the asset library load, synchronously when a
	/// --scene override jumps straight to a scene, and makes the ground tile bank.
	void initializeResources(app::App& app, bool hasSceneArg);

	/// From main after App::shutdown: frees the ground tile arrays while the GL context is still alive.
	void shutdownResources();

	/// The ground tiles every land surface paints with. The splash bakes them a variant a frame once the asset
	/// library has loaded; the game loading scene finishes the bake when a --scene jump skipped the splash.
	worldview::GroundTileBank& groundTiles();

} // namespace world_sim
