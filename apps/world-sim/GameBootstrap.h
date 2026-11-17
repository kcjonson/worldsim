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
		std::vector<std::string> errors;	   // once done: the asset library's validation and recipe errors, else the bake's
	};

	/// The one way the ground tiles are baked, once the asset library has loaded: it blocks on the library's
	/// validation errors and the recipes' problems (prefixed [recipes]), then runs the bank's next step, a variant
	/// a step, so the scene keeps drawing between them. The splash steps it a frame at a time; the loading scene finishes it when a --scene
	/// jump skipped the splash; a dev switch straight to the game scene runs it to the end.
	GroundBakeStep stepGroundBake();

	/// The colonist rules (the condition vocabulary and the thought types under assets/config) load once, in
	/// the loading scene, which shows their errors itself. A scene that loads them without it (the game
	/// scene, jumped to directly) hands the errors here and switches to the splash, which blocks on them
	/// after the ground bake and shows them; it never loads the rules.
	void failColonistRules(std::vector<std::string> errors);

	/// The errors handed to failColonistRules, each naming its folder, which it takes: the handover happens once, and a
	/// second call answers empty until the next failColonistRules.
	std::vector<std::string> takeFailedColonistRules();

} // namespace world_sim
