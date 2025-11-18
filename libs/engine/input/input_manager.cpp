#include "input/input_manager.h"
#include "utils/log.h"

namespace engine {

	// Static member initialization
	InputManager* InputManager::s_instance = nullptr;

	InputManager& InputManager::Get() {
		if (!s_instance) {
			LOG_ERROR(Engine, "InputManager::Get() called before InputManager was created");
			throw std::runtime_error("InputManager not initialized");
		}
		return *s_instance;
	}

	void InputManager::SetInstance(InputManager* instance) {
		s_instance = instance;
		LOG_INFO(Engine, "InputManager singleton instance set");
	}

	InputManager::InputManager(GLFWwindow* window)
		: m_window(window) {

		LOG_INFO(Engine, "Initializing InputManager");

		if (!window) {
			LOG_WARNING(Engine, "InputManager created with null window");
			return;
		}

		// Get initial window size
		int width, height;
		glfwGetWindowSize(window, &width, &height);
		m_windowSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
		LOG_DEBUG(Engine, "Window size: %.0fx%.0f", m_windowSize.x, m_windowSize.y);

		// Get initial mouse position
		double x, y;
		glfwGetCursorPos(window, &x, &y);
		m_mousePosition = glm::vec2(static_cast<float>(x), static_cast<float>(y));
		m_lastMousePosition = m_mousePosition;

		// Register GLFW callbacks
		glfwSetKeyCallback(window, KeyCallback);
		glfwSetMouseButtonCallback(window, MouseButtonCallback);
		glfwSetCursorPosCallback(window, CursorPosCallback);
		glfwSetScrollCallback(window, ScrollCallback);
		glfwSetCursorEnterCallback(window, CursorEnterCallback);

		LOG_INFO(Engine, "InputManager initialized successfully");
	}

	InputManager::~InputManager() {
		LOG_INFO(Engine, "InputManager destroyed");
		if (s_instance == this) {
			s_instance = nullptr;
		}
	}

	void InputManager::Update(float deltaTime) {
		// Calculate mouse delta
		m_mouseDelta = m_mousePosition - m_lastMousePosition;
		m_lastMousePosition = m_mousePosition;

		// Update window size (in case window was resized)
		if (m_window) {
			int width, height;
			glfwGetWindowSize(m_window, &width, &height);
			m_windowSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
		}

		// Update button state transitions (Pressed → Down, Released → Up)
		UpdateButtonStates();

		// Reset per-frame state
		m_scrollDelta = 0.0f;
	}

	void InputManager::UpdateButtonStates() {
		// Update mouse button states
		for (auto& [button, state] : m_mouseButtonStates) {
			m_mouseButtonPreviousStates[button] = state;
			if (state == ButtonState::Pressed) {
				state = ButtonState::Down;
			} else if (state == ButtonState::Released) {
				state = ButtonState::Up;
			}
		}

		// Update key states
		for (auto& [key, state] : m_keyStates) {
			m_keyPreviousStates[key] = state;
			if (state == ButtonState::Pressed) {
				state = ButtonState::Down;
			} else if (state == ButtonState::Released) {
				state = ButtonState::Up;
			}
		}
	}

	// Query API implementations
	bool InputManager::IsMouseButtonDown(int button) const {
		auto it = m_mouseButtonStates.find(button);
		if (it == m_mouseButtonStates.end())
			return false;
		return it->second == ButtonState::Down || it->second == ButtonState::Pressed;
	}

	bool InputManager::IsMouseButtonPressed(int button) const {
		auto it = m_mouseButtonStates.find(button);
		if (it == m_mouseButtonStates.end())
			return false;
		return it->second == ButtonState::Pressed;
	}

	bool InputManager::IsMouseButtonReleased(int button) const {
		auto it = m_mouseButtonStates.find(button);
		if (it == m_mouseButtonStates.end())
			return false;
		return it->second == ButtonState::Released;
	}

	bool InputManager::IsKeyDown(int key) const {
		auto it = m_keyStates.find(key);
		if (it == m_keyStates.end())
			return false;
		return it->second == ButtonState::Down || it->second == ButtonState::Pressed;
	}

	bool InputManager::IsKeyPressed(int key) const {
		auto it = m_keyStates.find(key);
		if (it == m_keyStates.end())
			return false;
		return it->second == ButtonState::Pressed;
	}

	bool InputManager::IsKeyReleased(int key) const {
		auto it = m_keyStates.find(key);
		if (it == m_keyStates.end())
			return false;
		return it->second == ButtonState::Released;
	}

	// GLFW static callbacks
	void InputManager::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
		if (s_instance) {
			s_instance->HandleKeyInput(key, action);
		}
	}

	void InputManager::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
		if (s_instance) {
			s_instance->HandleMouseButton(button, action);
		}
	}

	void InputManager::CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
		if (s_instance) {
			s_instance->HandleMouseMove(xpos, ypos);
		}
	}

	void InputManager::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
		if (s_instance) {
			s_instance->HandleScroll(xoffset, yoffset);
		}
	}

	void InputManager::CursorEnterCallback(GLFWwindow* window, int entered) {
		if (s_instance) {
			s_instance->HandleCursorEnter(entered);
		}
	}

	// Instance event handlers
	void InputManager::HandleKeyInput(int key, int action) {
		if (action == GLFW_PRESS) {
			m_keyStates[key] = ButtonState::Pressed;
			LOG_DEBUG(Engine, "Key pressed: %d", key);
		} else if (action == GLFW_RELEASE) {
			m_keyStates[key] = ButtonState::Released;
			LOG_DEBUG(Engine, "Key released: %d", key);
		}
		// GLFW_REPEAT is handled implicitly (key stays in Down state)
	}

	void InputManager::HandleMouseButton(int button, int action) {
		if (action == GLFW_PRESS) {
			m_mouseButtonStates[button] = ButtonState::Pressed;
			LOG_DEBUG(Engine, "Mouse button pressed: %d at (%.0f, %.0f)", button, m_mousePosition.x, m_mousePosition.y);

			// Track dragging for left mouse button
			if (button == GLFW_MOUSE_BUTTON_LEFT) {
				m_isDragging = true;
				m_dragStartPos = m_mousePosition;
			}
		} else if (action == GLFW_RELEASE) {
			m_mouseButtonStates[button] = ButtonState::Released;
			LOG_DEBUG(Engine, "Mouse button released: %d at (%.0f, %.0f)", button, m_mousePosition.x, m_mousePosition.y);

			// Stop dragging
			if (button == GLFW_MOUSE_BUTTON_LEFT) {
				m_isDragging = false;
			}
		}
	}

	void InputManager::HandleMouseMove(double x, double y) {
		m_mousePosition = glm::vec2(static_cast<float>(x), static_cast<float>(y));
	}

	void InputManager::HandleScroll(double xoffset, double yoffset) {
		m_scrollDelta = static_cast<float>(yoffset);
		if (m_scrollDelta != 0.0f) {
			LOG_DEBUG(Engine, "Scroll event: %.1f", m_scrollDelta);
		}
	}

	void InputManager::HandleCursorEnter(int entered) {
		m_cursorInWindow = (entered != 0);
		LOG_DEBUG(Engine, "Cursor %s window", m_cursorInWindow ? "entered" : "left");
	}

} // namespace engine
