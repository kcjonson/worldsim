#pragma once

#include <string>
#include <vector>

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

	/// The ground tiles every land surface paints with, baked through stepGroundBake.
	worldview::GroundTileBank& groundTiles();

	/// Where the ground bake stands after a step.
	struct GroundBakeStep {
		bool					 done = false; // nothing left to run
		std::vector<std::string> errors;	   // once done: the asset library's validation errors, else the bake's
	};

	/// The one way the ground tiles are baked, once the asset library has loaded: it blocks on the library's
	/// validation errors, then runs the bank's next step, a variant a step, so the scene keeps drawing between
	/// them. The splash steps it a frame at a time; the loading scene finishes it when a --scene jump skipped
	/// the splash; a dev switch straight to the game scene runs it to the end.
	GroundBakeStep stepGroundBake();

} // namespace world_sim
