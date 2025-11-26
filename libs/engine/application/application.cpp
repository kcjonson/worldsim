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
		: m_window(window) {
		if (m_window == nullptr) {
			LOG_ERROR(Engine, "Application created with null window");
			return;
		}

		// Create and initialize InputManager
		m_inputManager = std::make_unique<InputManager>(m_window);
		InputManager::SetInstance(m_inputManager.get());
		LOG_INFO(Engine, "Application initialized with InputManager");

		// Create and initialize ClipboardManager
		m_clipboardManager = std::make_unique<ClipboardManager>(m_window);
		ClipboardManager::SetInstance(m_clipboardManager.get());
		LOG_INFO(Engine, "Application initialized with ClipboardManager");

		// Create and initialize FocusManager
		m_focusManager = std::make_unique<UI::FocusManager>();
		UI::FocusManager::SetInstance(m_focusManager.get());
		LOG_INFO(Engine, "Application initialized with FocusManager");

		// Wire InputManager callbacks to FocusManager
		m_inputManager->SetKeyInputCallback([this](Key key, int action, int mods) -> bool {
			// Only handle key press and repeat events (not release)
			if (action != GLFW_PRESS && action != GLFW_REPEAT) {
				return false; // Don't consume
			}

			// Handle Tab key for focus navigation
			if (key == Key::Tab) {
				if (mods & GLFW_MOD_SHIFT) {
					m_focusManager->FocusPrevious();
				} else {
					m_focusManager->FocusNext();
				}
				return true; // Consume Tab key
			}

			// Route other keys to focused component
			bool shift = (mods & GLFW_MOD_SHIFT) != 0;
			bool ctrl = (mods & GLFW_MOD_CONTROL) != 0;
			bool alt = (mods & GLFW_MOD_ALT) != 0;

			m_focusManager->RouteKeyInput(key, shift, ctrl, alt);
			return m_focusManager->GetFocused() != nullptr; // Consume if component has focus
		});

		m_inputManager->SetCharInputCallback([this](char32_t codepoint) -> bool {
			m_focusManager->RouteCharInput(codepoint);
			return m_focusManager->GetFocused() != nullptr; // Consume if component has focus
		});
	}

	Application::~Application() {
		// Destructor must be defined in .cpp where InputManager/FocusManager are complete types
		// std::unique_ptr destructor requires complete type
		LOG_INFO(Engine, "Application destroyed");
	}

	void Application::Run() {
		if (m_window == nullptr) {
			LOG_ERROR(Engine, "Cannot run: window not initialized");
			return;
		}

		LOG_INFO(Engine, "Starting application main loop");

		m_isRunning = true;
		m_lastTime = glfwGetTime();

		while (glfwWindowShouldClose(m_window) == 0 && m_isRunning) {
			// Calculate delta time
			double currentTime = glfwGetTime();
			m_deltaTime = static_cast<float>(currentTime - m_lastTime);
			m_lastTime = currentTime;

			// Cap delta time to prevent large jumps (e.g., during debugging)
			// This prevents physics explosions and other time-step-sensitive bugs
			if (m_deltaTime > 0.25F) {
				LOG_DEBUG(Engine, "Large delta time detected (%.3fs), capping to 0.25s", m_deltaTime);
				m_deltaTime = 0.25F;
			}

			// Calculate FPS
			if (m_deltaTime > 0.0F) {
				m_fps = 1.0F / m_deltaTime;
			}

			// Poll GLFW events
			glfwPollEvents();

			// Update InputManager to capture input state for this frame
			if (m_inputManager) {
				try {
					m_inputManager->Update(m_deltaTime);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in InputManager::Update: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in InputManager::Update");
				}
			}

			// Pre-frame callback (debug server control, etc.)
			// Can return false to request exit
			if (m_preFrameCallback) {
				try {
					if (!m_preFrameCallback()) {
						LOG_INFO(Engine, "Pre-frame callback requested exit");
						m_isRunning = false;
						break;
					}
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in pre-frame callback: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in pre-frame callback");
				}
			}

			// Scene lifecycle (skip if paused)
			if (!m_isPaused) {
				// Handle input
				try {
					SceneManager::Get().HandleInput(m_deltaTime);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in HandleInput: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in HandleInput");
				}

				// Update
				try {
					SceneManager::Get().Update(m_deltaTime);
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
			if (m_overlayRenderer) {
				try {
					m_overlayRenderer();
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in overlay renderer: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in overlay renderer");
				}
			}

			// Post-frame callback (metrics, screenshot capture, etc.)
			if (m_postFrameCallback) {
				try {
					m_postFrameCallback();
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Exception in post-frame callback: %s", e.what());
				} catch (...) {
					LOG_ERROR(Engine, "Unknown exception in post-frame callback");
				}
			}

			// Swap buffers
			glfwSwapBuffers(m_window);
		}

		LOG_INFO(Engine, "Application main loop ended");
	}

	void Application::Stop() {
		LOG_INFO(Engine, "Application stop requested");
		m_isRunning = false;
	}

	void Application::Pause() {
		LOG_INFO(Engine, "Application paused");
		m_isPaused = true;
	}

	void Application::Resume() {
		LOG_INFO(Engine, "Application resumed");
		m_isPaused = false;
	}

	bool Application::IsPaused() const {
		return m_isPaused;
	}

	void Application::SetOverlayRenderer(OverlayRenderer renderer) {
		m_overlayRenderer = std::move(renderer);
	}

	void Application::SetPreFrameCallback(PreFrameCallback callback) {
		m_preFrameCallback = std::move(callback);
	}

	void Application::SetPostFrameCallback(PostFrameCallback callback) {
		m_postFrameCallback = std::move(callback);
	}

	float Application::GetFPS() const {
		return m_fps;
	}

	float Application::GetDeltaTime() const {
		return m_deltaTime;
	}

	UI::FocusManager& Application::GetFocusManager() {
		return *m_focusManager;
	}

} // namespace engine
