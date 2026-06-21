#include "SceneTypes.h"

namespace asset_manager::scenes {
	using asset_manager::SceneInfo;
#define SCENE_EXTERN(name) extern const SceneInfo name;
	ASSET_MANAGER_SCENES(SCENE_EXTERN)
#undef SCENE_EXTERN
} // namespace asset_manager::scenes

namespace asset_manager {

	void initializeSceneManager() {
		const std::pair<SceneType, const SceneInfo*> allScenes[] = {
#define SCENE_PAIR(name) {SceneType::name, &scenes::name},
			ASSET_MANAGER_SCENES(SCENE_PAIR)
#undef SCENE_PAIR
		};

		engine::SceneRegistry							  registry;
		std::unordered_map<engine::SceneKey, std::string> names;
		for (const auto& [type, info] : allScenes) {
			registry[toKey(type)] = info->factory;
			names[toKey(type)] = info->name;
		}
		engine::SceneManager::Get().initialize(std::move(registry), std::move(names));
	}

} // namespace asset_manager
