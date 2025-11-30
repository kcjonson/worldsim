#include "scene_manager.h"
#include <algorithm>
#include <cstring>
#include <utils/log.h>

namespace engine {

	SceneManager& SceneManager::Get() {
		static SceneManager instance;
		return instance;
	}

	void
	SceneManager::registerScene(const std::string& name, SceneFactory factory) { // NOLINT(readability-convert-member-functions-to-static)
		if (sceneRegistry.find(name) != sceneRegistry.end()) {
			LOG_WARNING(Engine, "Scene '%s' already registered, overwriting", name.c_str());
		}

		sceneRegistry[name] = std::move(factory);
		LOG_DEBUG(Engine, "Registered scene: %s", name.c_str());
	}

	bool SceneManager::switchTo(const std::string& name) { // NOLINT(readability-convert-member-functions-to-static)
		// Check if scene exists
		auto it = sceneRegistry.find(name);
		if (it == sceneRegistry.end()) {
			LOG_ERROR(Engine, "Scene '%s' not found in registry", name.c_str());
			return false;
		}

		// Exit current scene
		if (currentScene) {
			LOG_DEBUG(Engine, "Exiting scene: %s", currentSceneName.c_str());
			currentScene->OnExit();
			currentScene.reset();
		}

		// Create and enter new scene
		currentSceneName = name;
		currentScene = it->second();
		LOG_INFO(Engine, "Entering scene: %s", currentSceneName.c_str());
		currentScene->OnEnter();

		return true;
	}

	void SceneManager::handleInput(float dt) { // NOLINT(readability-convert-member-functions-to-static)
		if (currentScene) {
			currentScene->HandleInput(dt);
		}
	}

	void SceneManager::update(float dt) { // NOLINT(readability-convert-member-functions-to-static)
		if (currentScene) {
			currentScene->Update(dt);
		}
	}

	void SceneManager::render() { // NOLINT(readability-convert-member-functions-to-static)
		if (currentScene) {
			currentScene->Render();
		}
	}

	IScene* SceneManager::getCurrentScene() const {
		return currentScene.get();
	}

	std::string SceneManager::getCurrentSceneName() const {
		return currentSceneName;
	}

	std::vector<std::string> SceneManager::getAllSceneNames() const {
		std::vector<std::string> names;
		names.reserve(sceneRegistry.size());

		for (const auto& [name, factory] : sceneRegistry) {
			names.push_back(name);
		}

		// Sort alphabetically for consistent ordering
		std::sort(names.begin(), names.end());

		return names;
	}

	bool SceneManager::hasScene(const std::string& name) const {
		return sceneRegistry.find(name) != sceneRegistry.end();
	}

	void SceneManager::shutdown() {
		if (currentScene) {
			LOG_INFO(Engine, "Shutting down scene system, exiting scene: %s", currentSceneName.c_str());
			currentScene->OnExit();
			currentScene.reset();
			currentSceneName.clear();
		}
	}

	bool SceneManager::setInitialSceneFromArgs( // NOLINT(readability-convert-member-functions-to-static)
		int	   argc,
		char** argv
	) { // NOLINT(readability-convert-member-functions-to-static,cppcoreguidelines-pro-bounds-pointer-arithmetic)
		// Look for --scene=<name> argument
		for (int i = 1; i < argc; ++i) {
			const char* arg = argv[i]; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

			// Check if starts with --scene=
			if (std::strncmp(arg, "--scene=", 8) == 0) {
				const char* sceneName = arg + 8; // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic) Skip "--scene="

				if (std::strlen(sceneName) == 0) {
					LOG_WARNING(Engine, "--scene argument provided but no scene name specified");
					return false;
				}

				// Try to switch to the specified scene
				if (switchTo(sceneName)) {
					LOG_INFO(Engine, "Loaded scene from command-line: %s", sceneName);
					return true;
				}
				LOG_ERROR(Engine, "Failed to load scene from command-line: %s", sceneName);
				return false;
			}
		}

		// No --scene argument found
		return false;
	}

} // namespace engine
