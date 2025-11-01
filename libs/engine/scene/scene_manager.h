#pragma once

#include "scene.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace engine {

	/// @brief Factory function type for creating scenes
	using SceneFactory = std::function<std::unique_ptr<IScene>()>;

	/// @brief Manages scene registration, lifecycle, and switching
	///
	/// Singleton that maintains a registry of available scenes and handles
	/// switching between them. Used by both ui-sandbox and main game.
	///
	/// Usage:
	///   SceneManager::Get().RegisterScene("shapes", []() { return std::make_unique<ShapesScene>(); });
	///   SceneManager::Get().SwitchTo("shapes");
	///   SceneManager::Get().Update(dt);
	///   SceneManager::Get().Render();
	class SceneManager {
	  public:
		/// @brief Get singleton instance
		static SceneManager& Get();

		// Disable copy/move
		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;
		SceneManager(SceneManager&&) = delete;
		SceneManager& operator=(SceneManager&&) = delete;

		/// @brief Register a scene with the manager
		/// @param name Unique scene name (lowercase, no spaces)
		/// @param factory Function that creates a new instance of the scene
		void RegisterScene(const std::string& name, SceneFactory factory);

		/// @brief Switch to a different scene
		/// Calls OnExit() on current scene, then OnEnter() on new scene
		/// @param name Name of scene to switch to
		/// @return true if switch succeeded, false if scene not found
		bool SwitchTo(const std::string& name);

		/// @brief Handle input for current scene
		/// @param dt Delta time in seconds
		void HandleInput(float dt);

		/// @brief Update current scene
		/// @param dt Delta time in seconds
		void Update(float dt);

		/// @brief Render current scene
		void Render();

		/// @brief Get current active scene
		/// @return Pointer to current scene, or nullptr if no scene active
		IScene* GetCurrentScene() const;

		/// @brief Get current scene name
		/// @return Name of current scene, or empty string if no scene active
		std::string GetCurrentSceneName() const;

		/// @brief Get list of all registered scene names
		/// @return Vector of scene names (sorted alphabetically)
		std::vector<std::string> GetAllSceneNames() const;

		/// @brief Check if a scene is registered
		/// @param name Scene name to check
		/// @return true if scene exists in registry
		bool HasScene(const std::string& name) const;

		/// @brief Parse command-line args and switch to specified scene
		/// Looks for --scene=<name> argument
		/// @param argc Argument count
		/// @param argv Argument vector
		/// @return true if --scene arg found and scene loaded, false otherwise
		bool SetInitialSceneFromArgs(int argc, char** argv);

	  private:
		SceneManager() = default;
		~SceneManager() = default;

		std::map<std::string, SceneFactory> m_sceneRegistry{};
		std::unique_ptr<IScene>				m_currentScene{};
		std::string							m_currentSceneName{};
	};

} // namespace engine
