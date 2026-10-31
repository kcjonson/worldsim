#pragma once

#include <scene/Scene.h>

#include <functional>
#include <memory>

namespace app {
	class SceneStack;
} // namespace app

namespace world_sim {

	/// @brief Scene registration info - each scene exports one of these
	struct SceneInfo {
		const char*									 name;
		std::function<std::unique_ptr<app::Scene>()> factory;
	};

// ============================================================================
// SCENE LIST - Add new scenes here (the ONLY place you need to edit)
// ============================================================================
// clang-format off
#define WORLD_SIM_SCENES(X) \
	X(Splash)         \
	X(MainMenu)       \
	X(GameLoading)    \
	X(Game)           \
	X(Settings)       \
	X(WorldCreator)   \
	X(ScenarioSelect) \
	X(PartySelect)
	// clang-format on

	namespace scenes {
#define SCENE_EXTERN(scene) extern const SceneInfo scene;
		WORLD_SIM_SCENES(SCENE_EXTERN)
#undef SCENE_EXTERN
	} // namespace scenes

	/// @brief Add every scene above to the stack under its name
	void addScenes(app::SceneStack& stack);

} // namespace world_sim
