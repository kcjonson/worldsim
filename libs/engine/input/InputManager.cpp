#include "input/InputManager.h"
#include "utils/Log.h"
#include <optional>
#include <stdexcept>

namespace engine {

	// Internal conversion functions: Our enums ↔ GLFW constants
	namespace {
		// Convert our Key enum to GLFW key code
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

		// Convert GLFW key code to our Key enum
		std::optional<Key> FromGLFW(int glfwKey) {
			switch (glfwKey) {
				case GLFW_KEY_SPACE: return Key::Space;
				case GLFW_KEY_APOSTROPHE: return Key::Apostrophe;
				case GLFW_KEY_COMMA: return Key::Comma;
				case GLFW_KEY_MINUS: return Key::Minus;
				case GLFW_KEY_PERIOD: return Key::Period;
				case GLFW_KEY_SLASH: return Key::Slash;
				case GLFW_KEY_0: return Key::Num0;
				case GLFW_KEY_1: return Key::Num1;
				case GLFW_KEY_2: return Key::Num2;
				case GLFW_KEY_3: return Key::Num3;
				case GLFW_KEY_4: return Key::Num4;
				case GLFW_KEY_5: return Key::Num5;
				case GLFW_KEY_6: return Key::Num6;
				case GLFW_KEY_7: return Key::Num7;
				case GLFW_KEY_8: return Key::Num8;
				case GLFW_KEY_9: return Key::Num9;
				case GLFW_KEY_A: return Key::A;
				case GLFW_KEY_B: return Key::B;
				case GLFW_KEY_C: return Key::C;
				case GLFW_KEY_D: return Key::D;
				case GLFW_KEY_E: return Key::E;
				case GLFW_KEY_F: return Key::F;
				case GLFW_KEY_G: return Key::G;
				case GLFW_KEY_H: return Key::H;
				case GLFW_KEY_I: return Key::I;
				case GLFW_KEY_J: return Key::J;
				case GLFW_KEY_K: return Key::K;
				case GLFW_KEY_L: return Key::L;
				case GLFW_KEY_M: return Key::M;
				case GLFW_KEY_N: return Key::N;
				case GLFW_KEY_O: return Key::O;
				case GLFW_KEY_P: return Key::P;
				case GLFW_KEY_Q: return Key::Q;
				case GLFW_KEY_R: return Key::R;
				case GLFW_KEY_S: return Key::S;
				case GLFW_KEY_T: return Key::T;
				case GLFW_KEY_U: return Key::U;
				case GLFW_KEY_V: return Key::V;
				case GLFW_KEY_W: return Key::W;
				case GLFW_KEY_X: return Key::X;
				case GLFW_KEY_Y: return Key::Y;
				case GLFW_KEY_Z: return Key::Z;
				case GLFW_KEY_ESCAPE: return Key::Escape;
				case GLFW_KEY_ENTER: return Key::Enter;
				case GLFW_KEY_TAB: return Key::Tab;
				case GLFW_KEY_BACKSPACE: return Key::Backspace;
				case GLFW_KEY_INSERT: return Key::Insert;
				case GLFW_KEY_DELETE: return Key::Delete;
				case GLFW_KEY_HOME: return Key::Home;
				case GLFW_KEY_END: return Key::End;
				case GLFW_KEY_PAGE_UP: return Key::PageUp;
				case GLFW_KEY_PAGE_DOWN: return Key::PageDown;
				case GLFW_KEY_RIGHT: return Key::Right;
				case GLFW_KEY_LEFT: return Key::Left;
				case GLFW_KEY_DOWN: return Key::Down;
				case GLFW_KEY_UP: return Key::Up;
				case GLFW_KEY_F1: return Key::F1;
				case GLFW_KEY_F2: return Key::F2;
				case GLFW_KEY_F3: return Key::F3;
				case GLFW_KEY_F4: return Key::F4;
				case GLFW_KEY_F5: return Key::F5;
				case GLFW_KEY_F6: return Key::F6;
				case GLFW_KEY_F7: return Key::F7;
				case GLFW_KEY_F8: return Key::F8;
				case GLFW_KEY_F9: return Key::F9;
				case GLFW_KEY_F10: return Key::F10;
				case GLFW_KEY_F11: return Key::F11;
				case GLFW_KEY_F12: return Key::F12;
				case GLFW_KEY_LEFT_SHIFT: return Key::LeftShift;
				case GLFW_KEY_LEFT_CONTROL: return Key::LeftControl;
				case GLFW_KEY_LEFT_ALT: return Key::LeftAlt;
				case GLFW_KEY_LEFT_SUPER: return Key::LeftSuper;
				case GLFW_KEY_RIGHT_SHIFT: return Key::RightShift;
				case GLFW_KEY_RIGHT_CONTROL: return Key::RightControl;
				case GLFW_KEY_RIGHT_ALT: return Key::RightAlt;
				case GLFW_KEY_RIGHT_SUPER: return Key::RightSuper;
				case GLFW_KEY_KP_0: return Key::Kp0;
				case GLFW_KEY_KP_1: return Key::Kp1;
				case GLFW_KEY_KP_2: return Key::Kp2;
				case GLFW_KEY_KP_3: return Key::Kp3;
				case GLFW_KEY_KP_4: return Key::Kp4;
				case GLFW_KEY_KP_5: return Key::Kp5;
				case GLFW_KEY_KP_6: return Key::Kp6;
				case GLFW_KEY_KP_7: return Key::Kp7;
				case GLFW_KEY_KP_8: return Key::Kp8;
				case GLFW_KEY_KP_9: return Key::Kp9;
				case GLFW_KEY_KP_DECIMAL: return Key::KpDecimal;
				case GLFW_KEY_KP_DIVIDE: return Key::KpDivide;
				case GLFW_KEY_KP_MULTIPLY: return Key::KpMultiply;
				case GLFW_KEY_KP_SUBTRACT: return Key::KpSubtract;
				case GLFW_KEY_KP_ADD: return Key::KpAdd;
				case GLFW_KEY_KP_ENTER: return Key::KpEnter;
				case GLFW_KEY_KP_EQUAL: return Key::KpEqual;
				default: return std::nullopt;
			}
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

	void InputManager::setInstance(InputManager* instance) {
		s_instance = instance;
		LOG_INFO(Engine, "InputManager singleton instance set");
	}

	InputManager::InputManager(GLFWwindow* window)
		: window(window) {

		LOG_INFO(Engine, "Initializing InputManager");

		if (!window) {
			LOG_WARNING(Engine, "InputManager created with null window");
			return;
		}

		// Get initial window size
		int width, height;
		glfwGetWindowSize(window, &width, &height);
		windowSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
		LOG_DEBUG(Engine, "Window size: %.0fx%.0f", windowSize.x, windowSize.y);

		// Get initial mouse position
		double x, y;
		glfwGetCursorPos(window, &x, &y);
		mousePosition = glm::vec2(static_cast<float>(x), static_cast<float>(y));
		lastMousePosition = mousePosition;

		// Save existing callbacks before overwriting them (for callback chaining)
		previousKeyCallback = glfwSetKeyCallback(window, KeyCallback);
		previousCharCallback = glfwSetCharCallback(window, CharCallback);
		previousMouseButtonCallback = glfwSetMouseButtonCallback(window, MouseButtonCallback);
		previousCursorPosCallback = glfwSetCursorPosCallback(window, CursorPosCallback);
		previousScrollCallback = glfwSetScrollCallback(window, ScrollCallback);
		previousCursorEnterCallback = glfwSetCursorEnterCallback(window, CursorEnterCallback);

		LOG_INFO(Engine, "InputManager initialized successfully");
	}

	InputManager::~InputManager() {
		LOG_INFO(Engine, "InputManager destroyed");
		if (s_instance == this) {
			s_instance = nullptr;
		}
	}

	void InputManager::update(float deltaTime) {
		// Calculate mouse delta
		mouseDelta = mousePosition - lastMousePosition;
		lastMousePosition = mousePosition;

		// Update window size (in case window was resized)
		if (window) {
			int width, height;
			glfwGetWindowSize(window, &width, &height);
			windowSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
		}

		// Update button state transitions (Pressed → Down, Released → Up)
		updateButtonStates();

		// Note: scrollDelta is NOT reset here - it's consumed by consumeScrollDelta()
		// This allows handleInput() to read the value after update() is called
	}

	void InputManager::updateButtonStates() {
		// Update mouse button states
		for (auto& [button, state] : mouseButtonStates) {
			mouseButtonPreviousStates[button] = state;
			if (state == ButtonState::Pressed) {
				state = ButtonState::Down;
			} else if (state == ButtonState::Released) {
				state = ButtonState::Up;
			}
		}

		// Update key states
		for (auto& [key, state] : keyStates) {
			keyPreviousStates[key] = state;
			if (state == ButtonState::Pressed) {
				state = ButtonState::Down;
			} else if (state == ButtonState::Released) {
				state = ButtonState::Up;
			}
		}
	}

	// Query API implementations
	bool InputManager::isMouseButtonDown(MouseButton button) const {
		int glfwButton = ToGLFW(button);
		auto it = mouseButtonStates.find(glfwButton);
		if (it == mouseButtonStates.end())
			return false;
		return it->second == ButtonState::Down || it->second == ButtonState::Pressed;
	}

	bool InputManager::isMouseButtonPressed(MouseButton button) const {
		int glfwButton = ToGLFW(button);
		// Check previous state because updateButtonStates() transitions Pressed→Down
		// before handleInput() is called. The previous state captures the one-frame event.
		auto it = mouseButtonPreviousStates.find(glfwButton);
		if (it == mouseButtonPreviousStates.end())
			return false;
		return it->second == ButtonState::Pressed;
	}

	bool InputManager::isMouseButtonReleased(MouseButton button) const {
		int glfwButton = ToGLFW(button);
		// Check previous state because updateButtonStates() transitions Released→Up
		// before handleInput() is called. The previous state captures the one-frame event.
		auto it = mouseButtonPreviousStates.find(glfwButton);
		if (it == mouseButtonPreviousStates.end())
			return false;
		return it->second == ButtonState::Released;
	}

	bool InputManager::isKeyDown(Key key) const {
		int glfwKey = ToGLFW(key);
		auto it = keyStates.find(glfwKey);
		if (it == keyStates.end())
			return false;
		return it->second == ButtonState::Down || it->second == ButtonState::Pressed;
	}

	bool InputManager::isKeyPressed(Key key) const {
		int glfwKey = ToGLFW(key);
		// Check previous state because updateButtonStates() transitions Pressed→Down
		// before handleInput() is called. The previous state captures the one-frame event.
		auto it = keyPreviousStates.find(glfwKey);
		if (it == keyPreviousStates.end())
			return false;
		return it->second == ButtonState::Pressed;
	}

	bool InputManager::isKeyReleased(Key key) const {
		int glfwKey = ToGLFW(key);
		// Check previous state because updateButtonStates() transitions Released→Up
		// before handleInput() is called. The previous state captures the one-frame event.
		auto it = keyPreviousStates.find(glfwKey);
		if (it == keyPreviousStates.end())
			return false;
		return it->second == ButtonState::Released;
	}

	// GLFW static callbacks
	void InputManager::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->handleKeyInput(key, action);

			// Call external callback (e.g., FocusManager) - can consume event
			if (s_instance->keyInputCallback) {
				auto ourKey = FromGLFW(key);
				if (ourKey.has_value()) {
					bool consumed = s_instance->keyInputCallback(*ourKey, action, mods);
					if (consumed) {
						return;  // Event consumed, don't chain
					}
				}
			}

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->previousKeyCallback) {
				s_instance->previousKeyCallback(window, key, scancode, action, mods);
			}
		}
	}

	void InputManager::CharCallback(GLFWwindow* window, unsigned int codepoint) {
		// Call external callback (e.g., FocusManager for text input)
		if (s_instance && s_instance->charInputCallback) {
			bool consumed = s_instance->charInputCallback(static_cast<char32_t>(codepoint));
			if (consumed) {
				return;  // Event consumed, don't chain
			}
		}

		// Chain to previous callback
		if (s_instance && s_instance->previousCharCallback) {
			s_instance->previousCharCallback(window, codepoint);
		}
	}

	void InputManager::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->handleMouseButton(button, action);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->previousMouseButtonCallback) {
				s_instance->previousMouseButtonCallback(window, button, action, mods);
			}
		}
	}

	void InputManager::CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->handleMouseMove(xpos, ypos);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->previousCursorPosCallback) {
				s_instance->previousCursorPosCallback(window, xpos, ypos);
			}
		}
	}

	void InputManager::ScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->handleScroll(xoffset, yoffset);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->previousScrollCallback) {
				s_instance->previousScrollCallback(window, xoffset, yoffset);
			}
		}
	}

	void InputManager::CursorEnterCallback(GLFWwindow* window, int entered) {
		// Process input for InputManager
		if (s_instance) {
			s_instance->handleCursorEnter(entered);

			// Chain to previous callback (e.g., UI/menu handlers)
			if (s_instance->previousCursorEnterCallback) {
				s_instance->previousCursorEnterCallback(window, entered);
			}
		}
	}

	// Instance event handlers
	void InputManager::handleKeyInput(int key, int action) {
		if (action == GLFW_PRESS) {
			keyStates[key] = ButtonState::Pressed;
			LOG_DEBUG(Engine, "Key pressed: %d", key);
		} else if (action == GLFW_RELEASE) {
			keyStates[key] = ButtonState::Released;
			LOG_DEBUG(Engine, "Key released: %d", key);
		}
		// GLFW_REPEAT is handled implicitly (key stays in Down state)
	}

	void InputManager::handleCharInput(unsigned int codepoint) {
		LOG_DEBUG(Engine, "Character input: U+%04X (%lc)", codepoint, static_cast<wchar_t>(codepoint));
		// Character input is routed via callback in CharCallback static method
		// This method is just for logging/debugging
	}

	void InputManager::handleMouseButton(int button, int action) {
		if (action == GLFW_PRESS) {
			mouseButtonStates[button] = ButtonState::Pressed;
			LOG_DEBUG(Engine, "Mouse button pressed: %d at (%.0f, %.0f)", button, mousePosition.x, mousePosition.y);

			// Track dragging for left mouse button
			if (button == GLFW_MOUSE_BUTTON_LEFT) {
				dragging = true;
				dragStartPos = mousePosition;
			}
		} else if (action == GLFW_RELEASE) {
			mouseButtonStates[button] = ButtonState::Released;
			LOG_DEBUG(Engine, "Mouse button released: %d at (%.0f, %.0f)", button, mousePosition.x, mousePosition.y);

			// Stop dragging
			if (button == GLFW_MOUSE_BUTTON_LEFT) {
				dragging = false;
			}
		}
	}

	void InputManager::handleMouseMove(double x, double y) {
		mousePosition = glm::vec2(static_cast<float>(x), static_cast<float>(y));
	}

	void InputManager::handleScroll(double xoffset, double yoffset) {
		scrollDelta = static_cast<float>(yoffset);
		if (scrollDelta != 0.0f) {
			LOG_DEBUG(Engine, "Scroll event: %.1f", scrollDelta);
		}
	}

	void InputManager::handleCursorEnter(int entered) {
		cursorInWindow = (entered != 0);
		LOG_DEBUG(Engine, "Cursor %s window", cursorInWindow ? "entered" : "left");
	}

} // namespace engine
