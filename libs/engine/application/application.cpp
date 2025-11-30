#include "application.h"
#include "clipboard/clipboard_manager.h"
#include "focus/focus_manager.h"
#include "input/input_manager.h"
#include "scene/scene_manager.h"
#include "utils/log.h"

#include <exception>
#include <iostream>

namespace engine {

	Application::Application(GLFWwindow* window)
		: window(window) {
		if (window == nullptr) {
			LOG_ERROR(Engine, "Application created with null window");
			return;
		}

		// Create and initialize InputManager
		inputManager = std::make_unique<InputManager>(window);
		InputManager::SetInstance(inputManager.get());
		LOG_INFO(Engine, "Application initialized with InputManager");

		// Create and initialize ClipboardManager
		clipboardManager = std::make_unique<ClipboardManager>(window);
		ClipboardManager::SetInstance(clipboardManager.get());
		LOG_INFO(Engine, "Application initialized with ClipboardManager");

		// Create and initialize FocusManager
		focusManager = std::make_unique<UI::FocusManager>();
		UI::FocusManager::SetInstance(focusManager.get());
		LOG_INFO(Engine, "Application initialized with FocusManager");

		// Wire InputManager callbacks to FocusManager
		inputManager->SetKeyInputCallback([this](Key key, int action, int mods) -> bool {
			// Only handle key press and repeat events (not release)
			if (action != GLFW_PRESS && action != GLFW_REPEAT) {
				return false; // Don't consume
			}

			// Handle Tab key for focus navigation
			if (key == Key::Tab) {
				if (mods & GLFW_MOD_SHIFT) {
					focusManager->FocusPrevious();
				} else {
					focusManager->FocusNext();
				}
				return true; // Consume Tab key
			}

			// Route other keys to focused component
			bool shift = (mods & GLFW_MOD_SHIFT) != 0;
			bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
#ifdef __APPLE__
			// On macOS, Cmd (Super) is used for standard shortcuts like Cmd+C/V/X
			ctrl = ctrl || (mods & GLFW_MOD_SUPER) != 0;
#endif
			bool alt = (mods & GLFW_MOD_ALT) != 0;

			focusManager->RouteKeyInput(key, shift, ctrl, alt);
			return focusManager->getFocused() != nullptr; // Consume if component has focus
		});

		inputManager->SetCharInputCallback([this](char32_t codepoint) -> bool {
			focusManager->RouteCharInput(codepoint);
			return focusManager->getFocused() != nullptr; // Consume if component has focus
		});
	}

	Application::~Application() {
		// Destructor must be defined in .cpp where InputManager/FocusManager are complete types
		// std::unique_ptr destructor requires complete type
		LOG_INFO(Engine, "Application destroyed");
	}

	void Application::Run() {
		if (window == nullptr) {
			LOG_ERROR(Engine, "Cannot run: window not initialized");
			return;
		}

		LOG_INFO(Engine, "Starting application main loop");

		isRunning = true;
		lastTime = glfwGetTime();

		while (glfwWindowShouldClose(window) == 0 && isRunning) {
			// Calculate delta time
			double currentTime = glfwGetTime();
			deltaTime = static_cast<float>(currentTime - lastTime);
			lastTime = currentTime;

			// Cap delta time to prevent large jumps (e.g., during debugging)
			// This prevents physics explosions and other time-step-sensitive bugs
			if (deltaTime > 0.25F) {
				LOG_DEBUG(Engine, "Large delta time detected (%.3fs), capping to 0.25s", deltaTime);
				deltaTime = 0.25F;
			}

			// Calculate FPS
			if (deltaTime > 0.0F) {
				fps = 1.0F / deltaTime;
			}

			// Poll GLFW events
			glfwPollEvents();

			// Update InputManager to capture input state for this frame
			if (inputManager) {
				try {
					inputManager->Update(deltaTime);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in InputManager::Update: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in InputManager::Update");
				}
			}

			// Pre-frame callback (debug server control, etc.)
			// Can return false to request exit
			if (preFrameCallback) {
				try {
					if (!preFrameCallback()) {
						LOG_INFO(Engine, "Pre-frame callback requested exit");
						isRunning = false;
						break;
					}
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in pre-frame callback: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in pre-frame callback");
				}
			}

			// Scene lifecycle (skip if paused)
			if (!isPaused) {
				// Handle input
				try {
					SceneManager::Get().HandleInput(deltaTime);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in HandleInput: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in HandleInput");
				}

				// Update
				try {
					SceneManager::Get().Update(deltaTime);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in Update: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in Update");
				}
			}

			// Clear screen before rendering
			glClearColor(0.1F, 0.1F, 0.15F, 1.0F);
			glClear(GL_COLOR_BUFFER_BIT);

			// Render (even when paused, so screen doesn't freeze)
			try {
				SceneManager::Get().Render();
			} catch (const std::exception& e) {
				LOG_ERROR(Engine, "Exception in Render: %s", e.what());
			} catch (...) {
				LOG_ERROR(Engine, "Unknown exception in Render");
			}

			// Application-level overlay (debug UI, navigation menu, etc.)
			if (overlayRenderer) {
				try {
					overlayRenderer();
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in overlay renderer: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in overlay renderer");
				}
			}

			// Post-frame callback (metrics, screenshot capture, etc.)
			if (postFrameCallback) {
				try {
					postFrameCallback();
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in post-frame callback: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in post-frame callback");
				}
			}

			// Swap buffers
			glfwSwapBuffers(window);
		}

		LOG_INFO(Engine, "Application main loop ended");
	}

	void Application::Stop() {
		LOG_INFO(Engine, "Application stop requested");
		isRunning = false;
	}

	void Application::Pause() {
		LOG_INFO(Engine, "Application paused");
		isPaused = true;
	}

	void Application::Resume() {
		LOG_INFO(Engine, "Application resumed");
		isPaused = false;
	}

	bool Application::IsPaused() const {
		return isPaused;
	}

	void Application::SetOverlayRenderer(OverlayRenderer renderer) {
		overlayRenderer = std::move(renderer);
	}

	void Application::SetPreFrameCallback(PreFrameCallback callback) {
		preFrameCallback = std::move(callback);
	}

	void Application::SetPostFrameCallback(PostFrameCallback callback) {
		postFrameCallback = std::move(callback);
	}

	float Application::GetFPS() const {
		return fps;
	}

	float Application::GetDeltaTime() const {
		return deltaTime;
	}

	UI::FocusManager& Application::GetFocusManager() {
		return *focusManager;
	}

} // namespace engine
