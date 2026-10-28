#pragma once

// Scene registry for the Asset Manager app. The X-macro is the single source of
// truth for the scene list (mirrors the ui-sandbox pattern).

#include <app/UiScene.h>

namespace app {
	class SceneStack;
} // namespace app

namespace asset_manager {

	struct SceneInfo {
		const char*			   name;
		engine::UiSceneFactory factory;
	};

#define ASSET_MANAGER_SCENES(X) X(Browser)

	namespace scenes {
#define SCENE_EXTERN(scene) extern const SceneInfo scene;
		ASSET_MANAGER_SCENES(SCENE_EXTERN)
#undef SCENE_EXTERN
	} // namespace scenes

	/// @brief Add every asset-manager scene to the stack under its name
	void addScenes(app::SceneStack& stack);

} // namespace asset_manager
