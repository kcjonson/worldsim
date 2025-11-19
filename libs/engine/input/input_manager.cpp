#include "input/input_manager.h"
#include "utils/log.h"
#include <stdexcept>

namespace engine {

	// Internal conversion functions: Our enums ↔ GLFW constants
	namespace {
		int ToGLFW(Key key) {
			switch (key) {
				case Key::Space: return GLFW_KEY_SPACE;
				case Key::Apostrophe: return GLFW_KEY_APOSTROPHE;
				case Key::Comma: return GLFW_KEY_COMMA;
				case Key::Minus: return GLFW_KEY_MINUS;
				case Key::Period: return GLFW_KEY_PERIOD;
				case Key::Slash: return GLFW_KEY_SLASH;
				case Key::Num0: return GLFW_KEY_0;
				case Key::Num1: return GLFW_KEY_1;
				case Key::Num2: return GLFW_KEY_2;
				case Key::Num3: return GLFW_KEY_3;
				case Key::Num4: return GLFW_KEY_4;
				case Key::Num5: return GLFW_KEY_5;
				case Key::Num6: return GLFW_KEY_6;
				case Key::Num7: return GLFW_KEY_7;
				case Key::Num8: return GLFW_KEY_8;
				case Key::Num9: return GLFW_KEY_9;
				case Key::A: return GLFW_KEY_A;
				case Key::B: return GLFW_KEY_B;
				case Key::C: return GLFW_KEY_C;
				case Key::D: return GLFW_KEY_D;
				case Key::E: return GLFW_KEY_E;
				case Key::F: return GLFW_KEY_F;
				case Key::G: return GLFW_KEY_G;
				case Key::H: return GLFW_KEY_H;
				case Key::I: return GLFW_KEY_I;
				case Key::J: return GLFW_KEY_J;
				case Key::K: return GLFW_KEY_K;
				case Key::L: return GLFW_KEY_L;
				case Key::M: return GLFW_KEY_M;
				case Key::N: return GLFW_KEY_N;
				case Key::O: return GLFW_KEY_O;
				case Key::P: return GLFW_KEY_P;
				case Key::Q: return GLFW_KEY_Q;
				case Key::R: return GLFW_KEY_R;
				case Key::S: return GLFW_KEY_S;
				case Key::T: return GLFW_KEY_T;
				case Key::U: return GLFW_KEY_U;
				case Key::V: return GLFW_KEY_V;
				case Key::W: return GLFW_KEY_W;
				case Key::X: return GLFW_KEY_X;
				case Key::Y: return GLFW_KEY_Y;
				case Key::Z: return GLFW_KEY_Z;
				case Key::Escape: return GLFW_KEY_ESCAPE;
				case Key::Enter: return GLFW_KEY_ENTER;
				case Key::Tab: return GLFW_KEY_TAB;
				case Key::Backspace: return GLFW_KEY_BACKSPACE;
				case Key::Insert: return GLFW_KEY_INSERT;
				case Key::Delete: return GLFW_KEY_DELETE;
				case Key::Home: return GLFW_KEY_HOME;
				case Key::End: return GLFW_KEY_END;
				case Key::PageUp: return GLFW_KEY_PAGE_UP;
				case Key::PageDown: return GLFW_KEY_PAGE_DOWN;
				case Key::Right: return GLFW_KEY_RIGHT;
				case Key::Left: return GLFW_KEY_LEFT;
				case Key::Down: return GLFW_KEY_DOWN;
				case Key::Up: return GLFW_KEY_UP;
				case Key::F1: return GLFW_KEY_F1;
				case Key::F2: return GLFW_KEY_F2;
				case Key::F3: return GLFW_KEY_F3;
				case Key::F4: return GLFW_KEY_F4;
				case Key::F5: return GLFW_KEY_F5;
				case Key::F6: return GLFW_KEY_F6;
				case Key::F7: return GLFW_KEY_F7;
				case Key::F8: return GLFW_KEY_F8;
				case Key::F9: return GLFW_KEY_F9;
				case Key::F10: return GLFW_KEY_F10;
				case Key::F11: return GLFW_KEY_F11;
				case Key::F12: return GLFW_KEY_F12;
				case Key::LeftShift: return GLFW_KEY_LEFT_SHIFT;
				case Key::LeftControl: return GLFW_KEY_LEFT_CONTROL;
				case Key::LeftAlt: return GLFW_KEY_LEFT_ALT;
				case Key::LeftSuper: return GLFW_KEY_LEFT_SUPER;
				case Key::RightShift: return GLFW_KEY_RIGHT_SHIFT;
				case Key::RightControl: return GLFW_KEY_RIGHT_CONTROL;
				case Key::RightAlt: return GLFW_KEY_RIGHT_ALT;
				case Key::RightSuper: return GLFW_KEY_RIGHT_SUPER;
				case Key::Kp0: return GLFW_KEY_KP_0;
				case Key::Kp1: return GLFW_KEY_KP_1;
				case Key::Kp2: return GLFW_KEY_KP_2;
				case Key::Kp3: return GLFW_KEY_KP_3;
				case Key::Kp4: return GLFW_KEY_KP_4;
				case Key::Kp5: return GLFW_KEY_KP_5;
				case Key::Kp6: return GLFW_KEY_KP_6;
				case Key::Kp7: return GLFW_KEY_KP_7;
				case Key::Kp8: return GLFW_KEY_KP_8;
				case Key::Kp9: return GLFW_KEY_KP_9;
				case Key::KpDecimal: return GLFW_KEY_KP_DECIMAL;
				case Key::KpDivide: return GLFW_KEY_KP_DIVIDE;
				case Key::KpMultiply: return GLFW_KEY_KP_MULTIPLY;
				case Key::KpSubtract: return GLFW_KEY_KP_SUBTRACT;
				case Key::KpAdd: return GLFW_KEY_KP_ADD;
				case Key::KpEnter: return GLFW_KEY_KP_ENTER;
				case Key::KpEqual: return GLFW_KEY_KP_EQUAL;
				default: return GLFW_KEY_UNKNOWN;
			}
		}

		int ToGLFW(MouseButton button) {
			return static_cast<int>(button); // MouseButton enum values match GLFW
		}
	} // anonymous namespace

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

		// Save existing callbacks before overwriting them (for callback chaining)
		m_previousKeyCallback = glfwSetKeyCallback(window, KeyCallback);
		m_previousMouseButtonCallback = glfwSetMouseButtonCallback(window, MouseButtonCallback);
		m_previousCursorPosCallback = glfwSetCursorPosCallback(window, CursorPosCallback);
		m_previousScrollCallback = glfwSetScrollCallback(window, ScrollCallback);
		m_previousCursorEnterCallback = glfwSetCursorEnterCallback(window, CursorEnterCallback);

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
	bool InputManager::IsMouseButtonDown(MouseButton button) const {
		int glfwButton = ToGLFW(button);
		auto it = m_mouseButtonStates.find(glfwButton);
		if (it == m_mouseButtonStates.end())
			return false;
		return it->second == ButtonState::Down || it->second == ButtonState::Pressed;
	}

	bool InputManager::IsMouseButtonPressed(MouseButton button) const {
		int glfwButton = ToGLFW(button);
		auto it = m_mouseButtonStates.find(glfwButton);
		if (it == m_mouseButtonStates.end())
			return false;
		return it->second == ButtonState::Pressed;
	}

	bool InputManager::IsMouseButtonReleased(MouseButton button) const {
		int glfwButton = ToGLFW(button);
		auto it = m_mouseButtonStates.find(glfwButton);
		if (it == m_mouseButtonStates.end())
			return false;
		return it->second == ButtonState::Released;
	}

	bool InputManager::IsKeyDown(Key key) const {
		int glfwKey = ToGLFW(key);
		auto it = m_keyStates.find(glfwKey);
		if (it == m_keyStates.end())
			return false;
		return it->second == ButtonState::Down || it->second == ButtonState::Pressed;
	}

	bool InputManager::IsKeyPressed(Key key) const {
		int glfwKey = ToGLFW(key);
		auto it = m_keyStates.find(glfwKey);
		if (it == m_keyStates.end())
			return false;
		return it->second == ButtonState::Pressed;
	}

	bool InputManager::IsKeyReleased(Key key) const {
		int glfwKey = ToGLFW(key);
		auto it = m_keyStates.find(glfwKey);
		if (it == m_keyStates.end())
			return false;
		return it->second == ButtonState::Released;
	}

	// GLFW static callbacks
	void InputManager::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->HandleKeyInput(key, action);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->m_previousKeyCallback) {
				s_instance->m_previousKeyCallback(window, key, scancode, action, mods);
			}
		}
	}

	void InputManager::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->HandleMouseButton(button, action);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->m_previousMouseButtonCallback) {
				s_instance->m_previousMouseButtonCallback(window, button, action, mods);
			}
		}
	}

	void InputManager::CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->HandleMouseMove(xpos, ypos);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->m_previousCursorPosCallback) {
				s_instance->m_previousCursorPosCallback(window, xpos, ypos);
			}
		}
	}

	void InputManager::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->HandleScroll(xoffset, yoffset);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->m_previousScrollCallback) {
				s_instance->m_previousScrollCallback(window, xoffset, yoffset);
			}
		}
	}

	void InputManager::CursorEnterCallback(GLFWwindow* window, int entered) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->HandleCursorEnter(entered);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->m_previousCursorEnterCallback) {
				s_instance->m_previousCursorEnterCallback(window, entered);
			}
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
