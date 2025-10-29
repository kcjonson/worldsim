#pragma once

#include <functional>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

namespace engine {

/// @brief Core application class that owns the main game loop
///
/// Responsibilities:
/// - Main game loop (delta time, input polling, scene lifecycle)
/// - Pause/resume control
/// - Exception handling around scene methods
/// - Application-level overlay rendering (debug UI, HUD, etc.)
///
/// The Application class follows the proven pattern from ColonySim's ScreenManager,
/// adapted to WorldSim's Scene-based architecture. It manages the game loop while
/// delegating scene management to SceneManager.
///
/// Usage:
///   engine::Application app(window);
///   app.SetOverlayRenderer([]() { /* render debug UI */ });
///   app.Run();
///
/// The game loop executes in this order:
///   1. Calculate delta time (capped at 0.25s)
///   2. Poll GLFW events
///   3. Call pre-frame callbacks
///   4. SceneManager::HandleInput(dt)  [skipped if paused]
///   5. SceneManager::Update(dt)       [skipped if paused]
///   6. SceneManager::Render()
///   7. Call overlay renderer (application-level UI)
///   8. Call post-frame callbacks
///   9. Swap buffers
class Application {
public:
	/// @brief Overlay render callback type
	/// Called after scene renders, for application-level UI (debug menu, etc.)
	using OverlayRenderer = std::function<void()>;

	/// @brief Pre-frame callback type
	/// Called before scene lifecycle, can return false to exit application
	using PreFrameCallback = std::function<bool()>;

	/// @brief Post-frame callback type
	/// Called after rendering, before buffer swap
	using PostFrameCallback = std::function<void()>;

	/// @brief Construct application with GLFW window
	/// @param window GLFW window (must be valid, Application does not take ownership)
	explicit Application(GLFWwindow* window);

	~Application() = default;

	// Disable copy/move
	Application(const Application&) = delete;
	Application& operator=(const Application&) = delete;
	Application(Application&&) = delete;
	Application& operator=(Application&&) = delete;

	/// @brief Run the main game loop
	/// Blocks until window should close or Stop() is called
	void Run();

	/// @brief Stop the game loop
	/// Sets flag to exit on next iteration
	void Stop();

	/// @brief Pause scene updates
	/// When paused, HandleInput() and Update() are skipped, but Render() continues
	void Pause();

	/// @brief Resume scene updates
	void Resume();

	/// @brief Check if application is paused
	/// @return true if paused, false otherwise
	bool IsPaused() const;

	/// @brief Set overlay renderer callback
	/// Overlay is rendered after scene, for application-level UI
	/// @param renderer Callback function to render overlay
	void SetOverlayRenderer(OverlayRenderer renderer);

	/// @brief Set pre-frame callback
	/// Called before scene lifecycle each frame
	/// @param callback Callback function, return false to exit application
	void SetPreFrameCallback(PreFrameCallback callback);

	/// @brief Set post-frame callback
	/// Called after all rendering, before buffer swap
	/// @param callback Callback function
	void SetPostFrameCallback(PostFrameCallback callback);

	/// @brief Get current FPS
	/// @return Frames per second
	float GetFPS() const;

	/// @brief Get last delta time
	/// @return Delta time in seconds
	float GetDeltaTime() const;

private:
	GLFWwindow* m_window;           // GLFW window (not owned)
	bool m_isRunning;               // Main loop control flag
	bool m_isPaused;                // Pause state
	double m_lastTime;              // For delta time calculation
	float m_deltaTime;              // Last frame delta time
	float m_fps;                    // Current FPS

	OverlayRenderer m_overlayRenderer;   // Application-level UI
	PreFrameCallback m_preFrameCallback; // Pre-frame callback
	PostFrameCallback m_postFrameCallback; // Post-frame callback
};

} // namespace engine
