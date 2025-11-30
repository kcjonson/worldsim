#pragma once

#include <string>

namespace engine {

	/// @brief Base interface for all scenes (game states, UI test scenes, etc.)
	///
	/// Scenes represent distinct states of the application:
	/// - ui-sandbox: ShapesScene, ArenaScene, HandleScene (test scenes)
	/// - world-sim: SplashScene, MainMenuScene, GameplayScene (game scenes)
	///
	/// The SceneManager handles registration, switching, and lifecycle.
	class IScene {
	  public:
		IScene() = default;
		virtual ~IScene() = default;

		// Interfaces should not be copied or moved
		IScene(const IScene&) = delete;
		IScene& operator=(const IScene&) = delete;
		IScene(IScene&&) = delete;
		IScene& operator=(IScene&&) = delete;

		/// @brief Called when scene becomes active
		/// Use for initialization, resource loading, state setup
		virtual void onEnter() = 0;

		/// @brief Called every frame to handle input
		/// Separates input handling from game logic for better control
		/// (e.g., can disable input during cutscenes while Update continues)
		/// @param dt Delta time in seconds
		virtual void handleInput(float dt) = 0;

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
	};

} // namespace engine
