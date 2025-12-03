#include "SceneManager.h"
#include <algorithm>
#include <utils/Log.h>

namespace engine {

	SceneManager& SceneManager::Get() {
		static SceneManager instance;
		return instance;
	}

	void SceneManager::initialize(SceneRegistry registry, std::unordered_map<SceneKey, std::string> names) {
		// Exit current scene if one is active (handles re-initialization case)
		if (currentScene) {
			LOG_WARNING(Engine, "SceneManager::initialize() called with active scene - exiting current scene");
			currentScene->onExit();
			currentScene.reset();
			currentSceneKey = {};
		}

		sceneRegistry = std::move(registry);
		sceneNames = std::move(names);

		// Build reverse lookup for getKeyForName()
		nameToKey.clear();
		for (const auto& [key, name] : sceneNames) {
			nameToKey[name] = key;
		}

		LOG_INFO(Engine, "SceneManager initialized with %zu scenes", sceneRegistry.size());
	}

	bool SceneManager::switchTo(SceneKey key) {
		auto it = sceneRegistry.find(key);
		if (it == sceneRegistry.end()) {
			LOG_ERROR(Engine, "Scene key %zu not found in registry", key);
			return false;
		}

		// If no current scene (initialization), switch immediately
		if (!currentScene) {
			return doImmediateSwitch(key);
		}

		// Otherwise defer the switch to avoid use-after-free when called from callbacks
		LOG_DEBUG(Engine, "Deferring scene switch to: %s", getSceneName(key));
		pendingSceneKey = key;
		return true;
	}

	bool SceneManager::hasPendingSwitch() const {
		return pendingSceneKey.has_value();
	}

	bool SceneManager::applyPendingSceneChange() {
		if (!pendingSceneKey.has_value()) {
			return false;
		}

		SceneKey key = *pendingSceneKey;
		pendingSceneKey.reset();
		return doImmediateSwitch(key);
	}

	bool SceneManager::doImmediateSwitch(SceneKey key) {
		auto it = sceneRegistry.find(key);
		if (it == sceneRegistry.end()) {
			LOG_ERROR(Engine, "Scene key %zu not found in registry", key);
			return false;
		}

		// Exit current scene
		if (currentScene) {
			LOG_DEBUG(Engine, "Exiting scene: %s", getSceneName(currentSceneKey));
			currentScene->onExit();
			currentScene.reset();
		}

		// Create new scene
		currentSceneKey = key;
		currentScene = it->second();

		// Validate factory returned a valid scene
		if (!currentScene) {
			LOG_ERROR(Engine, "Scene factory for key %zu returned nullptr", key);
			currentSceneKey = {};
			return false;
		}

		// Inject SceneManager reference
		currentScene->setSceneManager(this);

		LOG_INFO(Engine, "Entering scene: %s", getSceneName(currentSceneKey));
		currentScene->onEnter();

		return true;
	}

	void SceneManager::handleInput(float dt) {
		if (currentScene) {
			currentScene->handleInput(dt);
		}
	}

	void SceneManager::update(float dt) {
		// Apply any pending scene change first (safe point after input handling)
		applyPendingSceneChange();

		if (currentScene) {
			currentScene->update(dt);
		}
	}

	void SceneManager::render() {
		if (currentScene) {
			currentScene->render();
		}
	}

	void SceneManager::requestExit() {
		LOG_INFO(Engine, "Exit requested");
		exitRequested = true;
	}

	bool SceneManager::isExitRequested() const {
		return exitRequested;
	}

	IScene* SceneManager::getCurrentScene() const {
		return currentScene.get();
	}

	SceneKey SceneManager::getCurrentSceneKey() const {
		return currentSceneKey;
	}

	bool SceneManager::hasScene(SceneKey key) const {
		return sceneRegistry.find(key) != sceneRegistry.end();
	}

	void SceneManager::shutdown() {
		if (currentScene) {
			LOG_INFO(Engine, "Shutting down scene system, exiting scene: %s", getSceneName(currentSceneKey));
			currentScene->onExit();
			currentScene.reset();
			currentSceneKey = {};
		}
		exitRequested = false;
	}

	SceneKey SceneManager::getKeyForName(const std::string& name) const {
		auto it = nameToKey.find(name);
		if (it == nameToKey.end()) {
			LOG_ERROR(Engine, "Unknown scene name: %s", name.c_str());
			return SIZE_MAX;
		}
		return it->second;
	}

	const char* SceneManager::getSceneName(SceneKey key) const {
		auto it = sceneNames.find(key);
		if (it != sceneNames.end()) {
			return it->second.c_str();
		}
		return "unknown";
	}

	std::vector<std::string> SceneManager::getAllSceneNames() const {
		std::vector<std::string> names;
		names.reserve(sceneNames.size());
		for (const auto& [key, name] : sceneNames) {
			names.push_back(name);
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	std::string SceneManager::getCurrentSceneName() const {
		if (currentScene == nullptr) {
			return "";
		}
		auto it = sceneNames.find(currentSceneKey);
		if (it != sceneNames.end()) {
			return it->second;
		}
		return "";
	}

} // namespace engine
