#pragma once

#include <string>

// Forward declaration for UI event dispatch
namespace UI {
	struct InputEvent;
}

namespace engine {

	// Forward declaration
	class SceneManager;

	/// @brief Base interface for all scenes (game states, UI test scenes, etc.)
	///
	/// Scenes represent distinct states of the application:
	/// - ui-sandbox: ShapesScene, ArenaScene, HandleScene (test scenes)
	/// - world-sim: SplashScene, MainMenuScene, GameScene (game scenes)
	///
	/// The SceneManager handles registration, switching, and lifecycle.
	///
	/// Resource Injection (colonysim pattern):
	/// Scenes receive a pointer to SceneManager before onEnter() is called.
	/// This provides access to:
	/// - Scene transitions: sceneManager->switchTo(SceneType::MainMenu)
	/// - Exit requests: sceneManager->requestExit()
	/// This avoids direct GLFW calls in scenes and provides clean dependency injection.
	class IScene {
	  public:
		IScene() = default;
		virtual ~IScene() = default;

		// Interfaces should not be copied or moved
		IScene(const IScene&) = delete;
		IScene& operator=(const IScene&) = delete;
		IScene(IScene&&) = delete;
		IScene& operator=(IScene&&) = delete;

		/// @brief Set the SceneManager reference (called by SceneManager before onEnter)
		/// This provides scenes with access to scene switching and exit requests
		/// without needing to use global singletons directly.
		/// @param manager Pointer to the SceneManager
		void setSceneManager(SceneManager* manager) { sceneManager = manager; }

		/// @brief Called when scene becomes active
		/// Use for initialization, resource loading, state setup
		virtual void onEnter() = 0;

		/// @brief Called every frame while scene is active
		/// @param dt Delta time in seconds
		virtual void update(float dt) = 0;

		/// @brief Called every frame to render scene
		/// Use Renderer and Primitives APIs for drawing
		virtual void render() = 0;

		/// @brief Called when scene becomes inactive
		/// Use for cleanup, save state, unload resources
		virtual void onExit() = 0;

		/// @brief Export current scene state as JSON
		/// Used by debug server /api/scene/state endpoint
		/// Each scene implements its own state representation
		/// @return JSON string representing scene state
		virtual std::string exportState() = 0;

		/// @brief Get human-readable scene name
		/// Used for command-line args, debug UI, logging
		/// Should be lowercase with no spaces (e.g., "shapes", "main_menu")
		/// @return Scene name
		virtual const char* getName() const = 0;

		/// @brief Handle UI input event
		/// Application dispatches mouse events here. Override to forward to UI components.
		/// @param event The input event to handle
		/// @return true if the event was consumed
		virtual bool handleInput(UI::InputEvent& /*event*/) { return false; }

	  protected:
		/// @brief SceneManager reference for scene transitions and exit requests
		/// Set by SceneManager before onEnter() is called.
		/// Scenes should use this instead of SceneManager::Get() for cleaner dependency injection.
		SceneManager* sceneManager = nullptr;
	};

} // namespace engine
