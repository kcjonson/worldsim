#include "application.h"
#include "scene/scene_manager.h"
#include "utils/log.h"

#include <exception>
#include <iostream>

namespace engine {

	Application::Application(GLFWwindow* window)
		: m_window(window),
		  m_isRunning(false),
		  m_isPaused(false),
		  m_lastTime(0.0),
		  m_deltaTime(0.0f),
		  m_fps(0.0f),
		  m_overlayRenderer(nullptr),
		  m_preFrameCallback(nullptr),
		  m_postFrameCallback(nullptr) {
		if (!m_window) {
			LOG_ERROR(Engine, "Application created with null window");
		}
	}

	void Application::Run() {
		if (!m_window) {
			LOG_ERROR(Engine, "Cannot run: window not initialized");
			return;
		}

		LOG_INFO(Engine, "Starting application main loop");

		m_isRunning = true;
		m_lastTime = glfwGetTime();

		while (!glfwWindowShouldClose(m_window) && m_isRunning) {
			// Calculate delta time
			double currentTime = glfwGetTime();
			m_deltaTime = static_cast<float>(currentTime - m_lastTime);
			m_lastTime = currentTime;

			// Cap delta time to prevent large jumps (e.g., during debugging)
			// This prevents physics explosions and other time-step-sensitive bugs
			if (m_deltaTime > 0.25f) {
				LOG_DEBUG(Engine, "Large delta time detected (%.3fs), capping to 0.25s", m_deltaTime);
				m_deltaTime = 0.25f;
			}

			// Calculate FPS
			if (m_deltaTime > 0.0f) {
				m_fps = 1.0f / m_deltaTime;
			}

			// Poll GLFW events
			glfwPollEvents();

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
		m_overlayRenderer = renderer;
	}

	void Application::SetPreFrameCallback(PreFrameCallback callback) {
		m_preFrameCallback = callback;
	}

	void Application::SetPostFrameCallback(PostFrameCallback callback) {
		m_postFrameCallback = callback;
	}

	float Application::GetFPS() const {
		return m_fps;
	}

	float Application::GetDeltaTime() const {
		return m_deltaTime;
	}

} // namespace engine
