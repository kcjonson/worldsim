#include "scene_manager.h"
#include <algorithm>
#include <cstring>
#include <utils/log.h>

namespace engine {

	SceneManager& SceneManager::Get() {
		static SceneManager instance;
		return instance;
	}

	void SceneManager::RegisterScene(const std::string& name, SceneFactory factory) {
		if (m_sceneRegistry.find(name) != m_sceneRegistry.end()) {
			LOG_WARNING(Engine, "Scene '%s' already registered, overwriting", name.c_str());
		}

		m_sceneRegistry[name] = factory;
		LOG_DEBUG(Engine, "Registered scene: %s", name.c_str());
	}

	bool SceneManager::SwitchTo(const std::string& name) {
		// Check if scene exists
		auto it = m_sceneRegistry.find(name);
		if (it == m_sceneRegistry.end()) {
			LOG_ERROR(Engine, "Scene '%s' not found in registry", name.c_str());
			return false;
		}

		// Exit current scene
		if (m_currentScene) {
			LOG_DEBUG(Engine, "Exiting scene: %s", m_currentSceneName.c_str());
			m_currentScene->OnExit();
			m_currentScene.reset();
		}

		// Create and enter new scene
		m_currentSceneName = name;
		m_currentScene = it->second();
		LOG_INFO(Engine, "Entering scene: %s", m_currentSceneName.c_str());
		m_currentScene->OnEnter();

		return true;
	}

	void SceneManager::HandleInput(float dt) {
		if (m_currentScene) {
			m_currentScene->HandleInput(dt);
		}
	}

	void SceneManager::Update(float dt) {
		if (m_currentScene) {
			m_currentScene->Update(dt);
		}
	}

	void SceneManager::Render() {
		if (m_currentScene) {
			m_currentScene->Render();
		}
	}

	IScene* SceneManager::GetCurrentScene() const {
		return m_currentScene.get();
	}

	std::string SceneManager::GetCurrentSceneName() const {
		return m_currentSceneName;
	}

	std::vector<std::string> SceneManager::GetAllSceneNames() const {
		std::vector<std::string> names;
		names.reserve(m_sceneRegistry.size());

		for (const auto& [name, factory] : m_sceneRegistry) {
			names.push_back(name);
		}

		// Sort alphabetically for consistent ordering
		std::sort(names.begin(), names.end());

		return names;
	}

	bool SceneManager::HasScene(const std::string& name) const {
		return m_sceneRegistry.find(name) != m_sceneRegistry.end();
	}

	bool SceneManager::SetInitialSceneFromArgs(int argc, char** argv) {
		// Look for --scene=<name> argument
		for (int i = 1; i < argc; ++i) {
			const char* arg = argv[i];

			// Check if starts with --scene=
			if (std::strncmp(arg, "--scene=", 8) == 0) {
				const char* sceneName = arg + 8; // Skip "--scene="

				if (std::strlen(sceneName) == 0) {
					LOG_WARNING(Engine, "--scene argument provided but no scene name specified");
					return false;
				}

				// Try to switch to the specified scene
				if (SwitchTo(sceneName)) {
					LOG_INFO(Engine, "Loaded scene from command-line: %s", sceneName);
					return true;
				} else {
					LOG_ERROR(Engine, "Failed to load scene from command-line: %s", sceneName);
					return false;
				}
			}
		}

		// No --scene argument found
		return false;
	}

} // namespace engine
