#include "SceneTypes.h"

#include <scene/SceneStack.h>

namespace asset_manager {

	void addScenes(app::SceneStack& stack) {
#define SCENE_ADD(scene) stack.add(scenes::scene.name, scenes::scene.factory);
		ASSET_MANAGER_SCENES(SCENE_ADD)
#undef SCENE_ADD
	}

} // namespace asset_manager
