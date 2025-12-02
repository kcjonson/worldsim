// SceneTypes.cpp - Scene registry initialization for world-sim
// Each scene exports a SceneInfo struct; this file collects them into one list

#include "SceneTypes.h"
#include <scene/Scene.h>

// Forward declarations of scene info (defined in each scene .cpp)
namespace world_sim::scenes {
	using world_sim::SceneInfo;
	extern const SceneInfo Splash;
	extern const SceneInfo MainMenu;
	extern const SceneInfo Game;
	extern const SceneInfo Settings;
	extern const SceneInfo WorldCreator;
} // namespace world_sim::scenes

namespace world_sim {

void initializeSceneManager() {
	// Single list: each scene mentioned exactly once
	const std::pair<SceneType, const SceneInfo*> allScenes[] = {
		{SceneType::Splash, &scenes::Splash},
		{SceneType::MainMenu, &scenes::MainMenu},
		{SceneType::Game, &scenes::Game},
		{SceneType::Settings, &scenes::Settings},
		{SceneType::WorldCreator, &scenes::WorldCreator},
	};

	// One loop populates both registry and names
	engine::SceneRegistry registry;
	std::unordered_map<engine::SceneKey, std::string> names;
	for (const auto& [type, info] : allScenes) {
		registry[toKey(type)] = info->factory;
		names[toKey(type)] = info->name;
	}

	engine::SceneManager::Get().initialize(std::move(registry), std::move(names));
}

} // namespace world_sim
