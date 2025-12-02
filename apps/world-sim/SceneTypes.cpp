// SceneTypes.cpp - Scene registry initialization for world-sim
// Each scene exports its factory function and name; this file collects them

#include "SceneTypes.h"
#include <scene/Scene.h>
#include <memory>

// Forward declarations of scene factory functions (defined in each scene .cpp)
namespace world_sim::scenes {
	std::unique_ptr<engine::IScene> createSplashScene();
	const char* getSplashSceneName();

	std::unique_ptr<engine::IScene> createMainMenuScene();
	const char* getMainMenuSceneName();

	std::unique_ptr<engine::IScene> createGameScene();
	const char* getGameSceneName();

	std::unique_ptr<engine::IScene> createSettingsScene();
	const char* getSettingsSceneName();

	std::unique_ptr<engine::IScene> createWorldCreatorScene();
	const char* getWorldCreatorSceneName();
} // namespace world_sim::scenes

namespace world_sim {

void initializeSceneManager() {
	using namespace scenes;

	// Build registry: enum -> factory
	engine::SceneRegistry registry;
	registry[toKey(SceneType::Splash)] = createSplashScene;
	registry[toKey(SceneType::MainMenu)] = createMainMenuScene;
	registry[toKey(SceneType::Game)] = createGameScene;
	registry[toKey(SceneType::Settings)] = createSettingsScene;
	registry[toKey(SceneType::WorldCreator)] = createWorldCreatorScene;

	// Build names: enum -> name (each scene declares its own name)
	std::unordered_map<engine::SceneKey, std::string> names;
	names[toKey(SceneType::Splash)] = getSplashSceneName();
	names[toKey(SceneType::MainMenu)] = getMainMenuSceneName();
	names[toKey(SceneType::Game)] = getGameSceneName();
	names[toKey(SceneType::Settings)] = getSettingsSceneName();
	names[toKey(SceneType::WorldCreator)] = getWorldCreatorSceneName();

	// Initialize SceneManager with our registry
	engine::SceneManager::Get().initialize(std::move(registry), std::move(names));
}

} // namespace world_sim
