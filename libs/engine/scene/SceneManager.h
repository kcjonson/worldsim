#pragma once

#include "Scene.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

	/// @brief Key type for scene registry (apps cast their enum to this)
	using SceneKey = std::size_t;

	/// @brief Factory function type for creating scenes
	using SceneFactory = std::function<std::unique_ptr<IScene>()>;

	/// @brief Scene registry mapping keys to factories
	using SceneRegistry = std::unordered_map<SceneKey, SceneFactory>;

	/// @brief Manages scene lifecycle and switching
	///
	/// Initialized by each app with its own scene registry mapping enum values
	/// to factory functions. The engine has no knowledge of app-specific scene types.
	///
	/// Pattern:
	/// - App defines its own SceneType enum
	/// - App initializes SceneManager with {enum -> factory} map
	/// - Scenes receive SceneManager pointer via dependency injection
	/// - Exit requests via requestExit() instead of direct GLFW calls
	class SceneManager {
	  public:
		/// @brief Get singleton instance
		static SceneManager& Get();

		// Disable copy/move
		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;
		SceneManager(SceneManager&&) = delete;
		SceneManager& operator=(SceneManager&&) = delete;

		/// @brief Initialize with app-specific scene registry
		/// @param registry Map of scene keys to factory functions
		/// @param sceneNames Map of scene keys to names (for HTTP API, logging)
		void initialize(SceneRegistry registry, std::unordered_map<SceneKey, std::string> sceneNames);

		/// @brief Switch to a different scene (deferred for safety)
		/// If called from input handlers/callbacks, the actual switch is deferred
		/// until the next frame's update() to avoid use-after-free bugs.
		/// If no scene is currently active (initialization), switch happens immediately.
		/// @param key Scene key (app's enum cast to SceneKey)
		/// @return true if scene exists in registry, false if not found
		bool switchTo(SceneKey key);

		/// @brief Check if a scene switch is pending
		/// @return true if there's a queued scene transition
		bool hasPendingSwitch() const;

		/// @brief Update current scene
		/// @param dt Delta time in seconds
		void update(float dt);

		/// @brief Render current scene
		void render();

		/// @brief Request application exit
		/// Scenes call this instead of direct GLFW calls
		void requestExit();

		/// @brief Check if exit has been requested
		/// Application main loop checks this each frame
		/// @return true if exit was requested
		bool isExitRequested() const;

		/// @brief Get current active scene
		/// @return Pointer to current scene, or nullptr if no scene active
		IScene* getCurrentScene() const;

		/// @brief Get current scene key
		/// @return Current scene key, or 0 (default SceneKey) if no scene is active
		SceneKey getCurrentSceneKey() const;

		/// @brief Check if a scene is registered
		/// @param key Scene key to check
		/// @return true if scene exists in registry
		bool hasScene(SceneKey key) const;

		/// @brief Shutdown scene system - exits and destroys current scene
		/// Must be called before singletons (FocusManager, InputManager) are destroyed
		void shutdown();

		/// @brief Get scene key from name (for CLI args, HTTP API edge conversion)
		/// @param name Scene name
		/// @return Scene key, or SIZE_MAX if not found
		SceneKey getKeyForName(const std::string& name) const;

		/// @brief Get scene name from key (for logging, debugging)
		/// @param key Scene key
		/// @return Scene name or "unknown"
		const char* getSceneName(SceneKey key) const;

		/// @brief Get all registered scene names (for HTTP API, navigation menu)
		std::vector<std::string> getAllSceneNames() const;

		/// @brief Get current scene name (for HTTP API, logging)
		/// @return Current scene name or empty string if no scene active
		std::string getCurrentSceneName() const;

	  private:
		SceneManager() = default;
		~SceneManager() = default;

		/// @brief Apply pending scene change (called at start of update)
		/// @return true if a switch was applied, false if no pending switch
		bool applyPendingSceneChange();

		/// @brief Immediately switch to a scene (internal use)
		/// @param key Scene key to switch to
		/// @return true if switch succeeded
		bool doImmediateSwitch(SceneKey key);

		SceneRegistry sceneRegistry;
		std::unordered_map<SceneKey, std::string> sceneNames;
		std::unordered_map<std::string, SceneKey> nameToKey;
		std::unique_ptr<IScene> currentScene;
		SceneKey currentSceneKey{};
		std::optional<SceneKey> pendingSceneKey;
		bool exitRequested{false};
	};

} // namespace engine
