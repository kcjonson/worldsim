#include "Application.h"
#include "clipboard/ClipboardManager.h"
#include "focus/FocusManager.h"
#include "input/InputManager.h"
#include "scene/SceneManager.h"
#include "utils/Log.h"

#include <input/InputEvent.h>
#include <math/Types.h>

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

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

		while (glfwWindowShouldClose(window) == 0 && isRunning && !SceneManager::Get().isExitRequested()) {
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
			// Note: FPS is calculated at end of frame after frame pacing sleep

			// Poll GLFW events
			auto pollStart = std::chrono::high_resolution_clock::now();
			glfwPollEvents();
			auto pollEnd = std::chrono::high_resolution_clock::now();
			m_frameTimings.pollEventsMs = std::chrono::duration<float, std::milli>(pollEnd - pollStart).count();

			// Update InputManager to capture input state for this frame
			auto inputStart = std::chrono::high_resolution_clock::now();
			if (inputManager) {
				try {
					inputManager->update(deltaTime);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in InputManager::Update: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in InputManager::Update");
				}
			}

			// Dispatch UI input events through SceneManager
			// SceneManager dispatches to overlays first, then to scene
			if (!paused && inputManager) {
				auto mousePos = inputManager->getMousePosition();
				auto pos = Foundation::Vec2{mousePos.x, mousePos.y};

				// Build modifier flags from current key state (GLFW modifier bit values)
				int mods = 0;
				if (inputManager->isKeyDown(engine::Key::LeftShift) || inputManager->isKeyDown(engine::Key::RightShift)) {
					mods |= 0x0001; // GLFW_MOD_SHIFT
				}
				if (inputManager->isKeyDown(engine::Key::LeftControl) || inputManager->isKeyDown(engine::Key::RightControl)) {
					mods |= 0x0002; // GLFW_MOD_CONTROL
				}
				if (inputManager->isKeyDown(engine::Key::LeftAlt) || inputManager->isKeyDown(engine::Key::RightAlt)) {
					mods |= 0x0004; // GLFW_MOD_ALT
				}

				// MouseMove for hover states
				UI::InputEvent moveEvent = UI::InputEvent::mouseMove(pos);
				SceneManager::Get().handleInput(moveEvent);

				// MouseDown on press (left button)
				if (inputManager->isMouseButtonPressed(engine::MouseButton::Left)) {
					UI::InputEvent downEvent = UI::InputEvent::mouseDown(pos, engine::MouseButton::Left, mods);
					SceneManager::Get().handleInput(downEvent);
				}

				// MouseDown on press (right button)
				if (inputManager->isMouseButtonPressed(engine::MouseButton::Right)) {
					UI::InputEvent downEvent = UI::InputEvent::mouseDown(pos, engine::MouseButton::Right, mods);
					SceneManager::Get().handleInput(downEvent);
				}

				// MouseUp on release (left button)
				if (inputManager->isMouseButtonReleased(engine::MouseButton::Left)) {
					UI::InputEvent upEvent = UI::InputEvent::mouseUp(pos, engine::MouseButton::Left, mods);
					SceneManager::Get().handleInput(upEvent);
				}

				// MouseUp on release (right button)
				if (inputManager->isMouseButtonReleased(engine::MouseButton::Right)) {
					UI::InputEvent upEvent = UI::InputEvent::mouseUp(pos, engine::MouseButton::Right, mods);
					SceneManager::Get().handleInput(upEvent);
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
			auto updateStart = std::chrono::high_resolution_clock::now();
			if (!paused) {
				// Update
				try {
					SceneManager::Get().update(deltaTime);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in Update: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in Update");
				}
			}
			auto updateEnd = std::chrono::high_resolution_clock::now();
			// Input handling includes InputManager update through pre-frame callback
			auto inputEnd = std::chrono::high_resolution_clock::now();
			m_frameTimings.inputHandleMs = std::chrono::duration<float, std::milli>(inputEnd - inputStart).count();
			m_frameTimings.sceneUpdateMs = std::chrono::duration<float, std::milli>(updateEnd - updateStart).count();

			// Clear screen before rendering
			auto renderStart = std::chrono::high_resolution_clock::now();
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
			auto renderEnd = std::chrono::high_resolution_clock::now();
			m_frameTimings.sceneRenderMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();

			// Swap buffers
			auto swapStart = std::chrono::high_resolution_clock::now();
			glfwSwapBuffers(window);
			auto swapEnd = std::chrono::high_resolution_clock::now();
			m_frameTimings.swapBuffersMs = std::chrono::duration<float, std::milli>(swapEnd - swapStart).count();

			// Frame pacing: yield CPU to prevent starving other processes
			constexpr float kTargetFrameMs = 1000.0F / 120.0F; // 8.33ms for 120 FPS cap
			double			frameNow = glfwGetTime();
			float			elapsedMs = static_cast<float>((frameNow - lastTime) * 1000.0);
			float			sleepMs = kTargetFrameMs - elapsedMs;
			if (sleepMs > 1.0F) {
				std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int>(sleepMs * 1000.0F)));
			}

			// Recalculate deltaTime/FPS after sleep so they reflect the capped frame duration
			double frameEnd = glfwGetTime();
			deltaTime = static_cast<float>(frameEnd - lastTime);
			if (deltaTime > 0.0F) {
				fps = 1.0F / deltaTime;
			}

			// Post-frame callback (metrics, screenshot capture, etc.)
			// Called after frame pacing so metrics include the full frame duration
			if (postFrameCallback) {
				try {
					postFrameCallback();
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in post-frame callback: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in post-frame callback");
				}
			}
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
