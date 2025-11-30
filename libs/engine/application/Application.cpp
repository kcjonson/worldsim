#include "Application.h"
#include "clipboard/ClipboardManager.h"
#include "focus/FocusManager.h"
#include "input/InputManager.h"
#include "scene/SceneManager.h"
#include "utils/Log.h"

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
		InputManager::setInstance(inputManager.get());
		LOG_INFO(Engine, "Application initialized with InputManager");

		// Create and initialize ClipboardManager
		clipboardManager = std::make_unique<ClipboardManager>(window);
		ClipboardManager::setInstance(clipboardManager.get());
		LOG_INFO(Engine, "Application initialized with ClipboardManager");

		// Create and initialize FocusManager
		focusManager = std::make_unique<UI::FocusManager>();
		UI::FocusManager::setInstance(focusManager.get());
		LOG_INFO(Engine, "Application initialized with FocusManager");

		// Wire InputManager callbacks to FocusManager
		inputManager->setKeyInputCallback([this](Key key, int action, int mods) -> bool {
			// Only handle key press and repeat events (not release)
			if (action != GLFW_PRESS && action != GLFW_REPEAT) {
				return false; // Don't consume
			}

			// Handle Tab key for focus navigation
			if (key == Key::Tab) {
				if (mods & GLFW_MOD_SHIFT) {
					focusManager->focusPrevious();
				} else {
					focusManager->focusNext();
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

			focusManager->routeKeyInput(key, shift, ctrl, alt);
			return focusManager->getFocused() != nullptr; // Consume if component has focus
		});

		inputManager->setCharInputCallback([this](char32_t codepoint) -> bool {
			focusManager->routeCharInput(codepoint);
			return focusManager->getFocused() != nullptr; // Consume if component has focus
		});
	}

	Application::~Application() {
		// Destructor must be defined in .cpp where InputManager/FocusManager are complete types
		// std::unique_ptr destructor requires complete type
		LOG_INFO(Engine, "Application destroyed");
	}

	void Application::run() {
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
					inputManager->update(deltaTime);
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
			if (!paused) {
				// Handle input
				try {
					SceneManager::Get().handleInput(deltaTime);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in HandleInput: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in HandleInput");
				}

				// Update
				try {
					SceneManager::Get().update(deltaTime);
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
				SceneManager::Get().render();
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

	void Application::stop() {
		LOG_INFO(Engine, "Application stop requested");
		isRunning = false;
	}

	void Application::pause() {
		LOG_INFO(Engine, "Application paused");
		paused = true;
	}

	void Application::resume() {
		LOG_INFO(Engine, "Application resumed");
		paused = false;
	}

	bool Application::isPaused() const {
		return paused;
	}

	void Application::setOverlayRenderer(OverlayRenderer renderer) {
		overlayRenderer = std::move(renderer);
	}

	void Application::setPreFrameCallback(PreFrameCallback callback) {
		preFrameCallback = std::move(callback);
	}

	void Application::setPostFrameCallback(PostFrameCallback callback) {
		postFrameCallback = std::move(callback);
	}

	float Application::getFPS() const {
		return fps;
	}

	float Application::getDeltaTime() const {
		return deltaTime;
	}

	UI::FocusManager& Application::getFocusManager() {
		return *focusManager;
	}

} // namespace engine
