// SceneTypes.cpp - Scene registry initialization for world-sim

#include "SceneTypes.h"

// Forward declare all scene infos (auto-generated from SCENE_LIST)
namespace world_sim::scenes {
	using world_sim::SceneInfo;
#define SCENE_EXTERN(name) extern const SceneInfo name;
	WORLD_SIM_SCENES(SCENE_EXTERN)
#undef SCENE_EXTERN
} // namespace world_sim::scenes

namespace world_sim {

	void initializeSceneManager() {
		// Build scene list (auto-generated from SCENE_LIST)
		const std::pair<SceneType, const SceneInfo*> allScenes[] = {
#define SCENE_PAIR(name) {SceneType::name, &scenes::name},
			WORLD_SIM_SCENES(SCENE_PAIR)
#undef SCENE_PAIR
		};

		// Register all scenes
		engine::SceneRegistry registry;
		std::unordered_map<engine::SceneKey, std::string> names;

		for (const auto& [type, info] : allScenes) {
			registry[toKey(type)] = info->factory;
			names[toKey(type)] = info->name;
		}

		engine::SceneManager::Get().initialize(std::move(registry), std::move(names));
	}

} // namespace world_sim
