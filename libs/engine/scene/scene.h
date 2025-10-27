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
	virtual ~IScene() = default;

	/// @brief Called when scene becomes active
	/// Use for initialization, resource loading, state setup
	virtual void OnEnter() = 0;

	/// @brief Called every frame while scene is active
	/// @param dt Delta time in seconds
	virtual void Update(float dt) = 0;

	/// @brief Called every frame to render scene
	/// Use Renderer and Primitives APIs for drawing
	virtual void Render() = 0;

	/// @brief Called when scene becomes inactive
	/// Use for cleanup, save state, unload resources
	virtual void OnExit() = 0;

	/// @brief Export current scene state as JSON
	/// Used by debug server /api/scene/state endpoint
	/// Each scene implements its own state representation
	/// @return JSON string representing scene state
	virtual std::string ExportState() = 0;

	/// @brief Get human-readable scene name
	/// Used for command-line args, debug UI, logging
	/// Should be lowercase with no spaces (e.g., "shapes", "main_menu")
	/// @return Scene name
	virtual const char* GetName() const = 0;
};

} // namespace engine
