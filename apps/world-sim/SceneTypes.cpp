// SceneTypes.cpp - Scene registration for world-sim

#include "SceneTypes.h"

#include <scene/SceneStack.h>

namespace world_sim {

	void addScenes(app::SceneStack& stack) {
#define SCENE_ADD(scene) stack.add(scenes::scene.name, scenes::scene.factory);
		WORLD_SIM_SCENES(SCENE_ADD)
#undef SCENE_ADD
	}

} // namespace world_sim
