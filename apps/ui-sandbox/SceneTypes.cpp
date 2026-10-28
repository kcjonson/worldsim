// SceneTypes.cpp - Scene registration for ui-sandbox

#include "SceneTypes.h"

#include <scene/SceneStack.h>

namespace ui_sandbox {

	void addScenes(app::SceneStack& stack) {
#define SCENE_ADD(scene) stack.add(scenes::scene.name, scenes::scene.factory);
		UI_SANDBOX_SCENES(SCENE_ADD)
#undef SCENE_ADD
	}

} // namespace ui_sandbox
