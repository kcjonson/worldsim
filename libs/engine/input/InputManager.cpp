#include "input/InputManager.h"
#include "utils/Log.h"
#include <cctype>
#include <optional>
#include <stdexcept>

namespace engine {

	// Internal conversion functions: Our enums ↔ GLFW constants
	namespace {
		// Convert our Key enum to GLFW key code
		int ToGLFW(foundation::Key key) {
			switch (key) {
				case foundation::Key::Space: return GLFW_KEY_SPACE;
				case foundation::Key::Apostrophe: return GLFW_KEY_APOSTROPHE;
				case foundation::Key::Comma: return GLFW_KEY_COMMA;
				case foundation::Key::Minus: return GLFW_KEY_MINUS;
				case foundation::Key::Period: return GLFW_KEY_PERIOD;
				case foundation::Key::Slash: return GLFW_KEY_SLASH;
				case foundation::Key::Num0: return GLFW_KEY_0;
				case foundation::Key::Num1: return GLFW_KEY_1;
				case foundation::Key::Num2: return GLFW_KEY_2;
				case foundation::Key::Num3: return GLFW_KEY_3;
				case foundation::Key::Num4: return GLFW_KEY_4;
				case foundation::Key::Num5: return GLFW_KEY_5;
				case foundation::Key::Num6: return GLFW_KEY_6;
				case foundation::Key::Num7: return GLFW_KEY_7;
				case foundation::Key::Num8: return GLFW_KEY_8;
				case foundation::Key::Num9: return GLFW_KEY_9;
				case foundation::Key::A: return GLFW_KEY_A;
				case foundation::Key::B: return GLFW_KEY_B;
				case foundation::Key::C: return GLFW_KEY_C;
				case foundation::Key::D: return GLFW_KEY_D;
				case foundation::Key::E: return GLFW_KEY_E;
				case foundation::Key::F: return GLFW_KEY_F;
				case foundation::Key::G: return GLFW_KEY_G;
				case foundation::Key::H: return GLFW_KEY_H;
				case foundation::Key::I: return GLFW_KEY_I;
				case foundation::Key::J: return GLFW_KEY_J;
				case foundation::Key::K: return GLFW_KEY_K;
				case foundation::Key::L: return GLFW_KEY_L;
				case foundation::Key::M: return GLFW_KEY_M;
				case foundation::Key::N: return GLFW_KEY_N;
				case foundation::Key::O: return GLFW_KEY_O;
				case foundation::Key::P: return GLFW_KEY_P;
				case foundation::Key::Q: return GLFW_KEY_Q;
				case foundation::Key::R: return GLFW_KEY_R;
				case foundation::Key::S: return GLFW_KEY_S;
				case foundation::Key::T: return GLFW_KEY_T;
				case foundation::Key::U: return GLFW_KEY_U;
				case foundation::Key::V: return GLFW_KEY_V;
				case foundation::Key::W: return GLFW_KEY_W;
				case foundation::Key::X: return GLFW_KEY_X;
				case foundation::Key::Y: return GLFW_KEY_Y;
				case foundation::Key::Z: return GLFW_KEY_Z;
				case foundation::Key::Escape: return GLFW_KEY_ESCAPE;
				case foundation::Key::Enter: return GLFW_KEY_ENTER;
				case foundation::Key::Tab: return GLFW_KEY_TAB;
				case foundation::Key::Backspace: return GLFW_KEY_BACKSPACE;
				case foundation::Key::Insert: return GLFW_KEY_INSERT;
				case foundation::Key::Delete: return GLFW_KEY_DELETE;
				case foundation::Key::Home: return GLFW_KEY_HOME;
				case foundation::Key::End: return GLFW_KEY_END;
				case foundation::Key::PageUp: return GLFW_KEY_PAGE_UP;
				case foundation::Key::PageDown: return GLFW_KEY_PAGE_DOWN;
				case foundation::Key::Right: return GLFW_KEY_RIGHT;
				case foundation::Key::Left: return GLFW_KEY_LEFT;
				case foundation::Key::Down: return GLFW_KEY_DOWN;
				case foundation::Key::Up: return GLFW_KEY_UP;
				case foundation::Key::F1: return GLFW_KEY_F1;
				case foundation::Key::F2: return GLFW_KEY_F2;
				case foundation::Key::F3: return GLFW_KEY_F3;
				case foundation::Key::F4: return GLFW_KEY_F4;
				case foundation::Key::F5: return GLFW_KEY_F5;
				case foundation::Key::F6: return GLFW_KEY_F6;
				case foundation::Key::F7: return GLFW_KEY_F7;
				case foundation::Key::F8: return GLFW_KEY_F8;
				case foundation::Key::F9: return GLFW_KEY_F9;
				case foundation::Key::F10: return GLFW_KEY_F10;
				case foundation::Key::F11: return GLFW_KEY_F11;
				case foundation::Key::F12: return GLFW_KEY_F12;
				case foundation::Key::LeftShift: return GLFW_KEY_LEFT_SHIFT;
				case foundation::Key::LeftControl: return GLFW_KEY_LEFT_CONTROL;
				case foundation::Key::LeftAlt: return GLFW_KEY_LEFT_ALT;
				case foundation::Key::LeftSuper: return GLFW_KEY_LEFT_SUPER;
				case foundation::Key::RightShift: return GLFW_KEY_RIGHT_SHIFT;
				case foundation::Key::RightControl: return GLFW_KEY_RIGHT_CONTROL;
				case foundation::Key::RightAlt: return GLFW_KEY_RIGHT_ALT;
				case foundation::Key::RightSuper: return GLFW_KEY_RIGHT_SUPER;
				case foundation::Key::Kp0: return GLFW_KEY_KP_0;
				case foundation::Key::Kp1: return GLFW_KEY_KP_1;
				case foundation::Key::Kp2: return GLFW_KEY_KP_2;
				case foundation::Key::Kp3: return GLFW_KEY_KP_3;
				case foundation::Key::Kp4: return GLFW_KEY_KP_4;
				case foundation::Key::Kp5: return GLFW_KEY_KP_5;
				case foundation::Key::Kp6: return GLFW_KEY_KP_6;
				case foundation::Key::Kp7: return GLFW_KEY_KP_7;
				case foundation::Key::Kp8: return GLFW_KEY_KP_8;
				case foundation::Key::Kp9: return GLFW_KEY_KP_9;
				case foundation::Key::KpDecimal: return GLFW_KEY_KP_DECIMAL;
				case foundation::Key::KpDivide: return GLFW_KEY_KP_DIVIDE;
				case foundation::Key::KpMultiply: return GLFW_KEY_KP_MULTIPLY;
				case foundation::Key::KpSubtract: return GLFW_KEY_KP_SUBTRACT;
				case foundation::Key::KpAdd: return GLFW_KEY_KP_ADD;
				case foundation::Key::KpEnter: return GLFW_KEY_KP_ENTER;
				case foundation::Key::KpEqual: return GLFW_KEY_KP_EQUAL;
				default: return GLFW_KEY_UNKNOWN;
			}
		}

		int ToGLFW(foundation::MouseButton button) {
			return static_cast<int>(button); // MouseButton enum values match GLFW
		}

		// Convert GLFW key code to our Key enum
		std::optional<foundation::Key> FromGLFW(int glfwKey) {
			switch (glfwKey) {
				case GLFW_KEY_SPACE: return foundation::Key::Space;
				case GLFW_KEY_APOSTROPHE: return foundation::Key::Apostrophe;
				case GLFW_KEY_COMMA: return foundation::Key::Comma;
				case GLFW_KEY_MINUS: return foundation::Key::Minus;
				case GLFW_KEY_PERIOD: return foundation::Key::Period;
				case GLFW_KEY_SLASH: return foundation::Key::Slash;
				case GLFW_KEY_0: return foundation::Key::Num0;
				case GLFW_KEY_1: return foundation::Key::Num1;
				case GLFW_KEY_2: return foundation::Key::Num2;
				case GLFW_KEY_3: return foundation::Key::Num3;
				case GLFW_KEY_4: return foundation::Key::Num4;
				case GLFW_KEY_5: return foundation::Key::Num5;
				case GLFW_KEY_6: return foundation::Key::Num6;
				case GLFW_KEY_7: return foundation::Key::Num7;
				case GLFW_KEY_8: return foundation::Key::Num8;
				case GLFW_KEY_9: return foundation::Key::Num9;
				case GLFW_KEY_A: return foundation::Key::A;
				case GLFW_KEY_B: return foundation::Key::B;
				case GLFW_KEY_C: return foundation::Key::C;
				case GLFW_KEY_D: return foundation::Key::D;
				case GLFW_KEY_E: return foundation::Key::E;
				case GLFW_KEY_F: return foundation::Key::F;
				case GLFW_KEY_G: return foundation::Key::G;
				case GLFW_KEY_H: return foundation::Key::H;
				case GLFW_KEY_I: return foundation::Key::I;
				case GLFW_KEY_J: return foundation::Key::J;
				case GLFW_KEY_K: return foundation::Key::K;
				case GLFW_KEY_L: return foundation::Key::L;
				case GLFW_KEY_M: return foundation::Key::M;
				case GLFW_KEY_N: return foundation::Key::N;
				case GLFW_KEY_O: return foundation::Key::O;
				case GLFW_KEY_P: return foundation::Key::P;
				case GLFW_KEY_Q: return foundation::Key::Q;
				case GLFW_KEY_R: return foundation::Key::R;
				case GLFW_KEY_S: return foundation::Key::S;
				case GLFW_KEY_T: return foundation::Key::T;
				case GLFW_KEY_U: return foundation::Key::U;
				case GLFW_KEY_V: return foundation::Key::V;
				case GLFW_KEY_W: return foundation::Key::W;
				case GLFW_KEY_X: return foundation::Key::X;
				case GLFW_KEY_Y: return foundation::Key::Y;
				case GLFW_KEY_Z: return foundation::Key::Z;
				case GLFW_KEY_ESCAPE: return foundation::Key::Escape;
				case GLFW_KEY_ENTER: return foundation::Key::Enter;
				case GLFW_KEY_TAB: return foundation::Key::Tab;
				case GLFW_KEY_BACKSPACE: return foundation::Key::Backspace;
				case GLFW_KEY_INSERT: return foundation::Key::Insert;
				case GLFW_KEY_DELETE: return foundation::Key::Delete;
				case GLFW_KEY_HOME: return foundation::Key::Home;
				case GLFW_KEY_END: return foundation::Key::End;
				case GLFW_KEY_PAGE_UP: return foundation::Key::PageUp;
				case GLFW_KEY_PAGE_DOWN: return foundation::Key::PageDown;
				case GLFW_KEY_RIGHT: return foundation::Key::Right;
				case GLFW_KEY_LEFT: return foundation::Key::Left;
				case GLFW_KEY_DOWN: return foundation::Key::Down;
				case GLFW_KEY_UP: return foundation::Key::Up;
				case GLFW_KEY_F1: return foundation::Key::F1;
				case GLFW_KEY_F2: return foundation::Key::F2;
				case GLFW_KEY_F3: return foundation::Key::F3;
				case GLFW_KEY_F4: return foundation::Key::F4;
				case GLFW_KEY_F5: return foundation::Key::F5;
				case GLFW_KEY_F6: return foundation::Key::F6;
				case GLFW_KEY_F7: return foundation::Key::F7;
				case GLFW_KEY_F8: return foundation::Key::F8;
				case GLFW_KEY_F9: return foundation::Key::F9;
				case GLFW_KEY_F10: return foundation::Key::F10;
				case GLFW_KEY_F11: return foundation::Key::F11;
				case GLFW_KEY_F12: return foundation::Key::F12;
				case GLFW_KEY_LEFT_SHIFT: return foundation::Key::LeftShift;
				case GLFW_KEY_LEFT_CONTROL: return foundation::Key::LeftControl;
				case GLFW_KEY_LEFT_ALT: return foundation::Key::LeftAlt;
				case GLFW_KEY_LEFT_SUPER: return foundation::Key::LeftSuper;
				case GLFW_KEY_RIGHT_SHIFT: return foundation::Key::RightShift;
				case GLFW_KEY_RIGHT_CONTROL: return foundation::Key::RightControl;
				case GLFW_KEY_RIGHT_ALT: return foundation::Key::RightAlt;
				case GLFW_KEY_RIGHT_SUPER: return foundation::Key::RightSuper;
				case GLFW_KEY_KP_0: return foundation::Key::Kp0;
				case GLFW_KEY_KP_1: return foundation::Key::Kp1;
				case GLFW_KEY_KP_2: return foundation::Key::Kp2;
				case GLFW_KEY_KP_3: return foundation::Key::Kp3;
				case GLFW_KEY_KP_4: return foundation::Key::Kp4;
				case GLFW_KEY_KP_5: return foundation::Key::Kp5;
				case GLFW_KEY_KP_6: return foundation::Key::Kp6;
				case GLFW_KEY_KP_7: return foundation::Key::Kp7;
				case GLFW_KEY_KP_8: return foundation::Key::Kp8;
				case GLFW_KEY_KP_9: return foundation::Key::Kp9;
				case GLFW_KEY_KP_DECIMAL: return foundation::Key::KpDecimal;
				case GLFW_KEY_KP_DIVIDE: return foundation::Key::KpDivide;
				case GLFW_KEY_KP_MULTIPLY: return foundation::Key::KpMultiply;
				case GLFW_KEY_KP_SUBTRACT: return foundation::Key::KpSubtract;
				case GLFW_KEY_KP_ADD: return foundation::Key::KpAdd;
				case GLFW_KEY_KP_ENTER: return foundation::Key::KpEnter;
				case GLFW_KEY_KP_EQUAL: return foundation::Key::KpEqual;
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
		hostMousePosition = glm::vec2(static_cast<float>(x), static_cast<float>(y));
		mousePosition = hostMousePosition;
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
	bool InputManager::isMouseButtonDown(foundation::MouseButton button) const {
		int glfwButton = ToGLFW(button);
		auto it = mouseButtonStates.find(glfwButton);
		if (it == mouseButtonStates.end())
			return false;
		return it->second == ButtonState::Down || it->second == ButtonState::Pressed;
	}

	bool InputManager::isMouseButtonPressed(foundation::MouseButton button) const {
		int glfwButton = ToGLFW(button);
		// Check previous state because updateButtonStates() transitions Pressed→Down
		// before handleInput() is called. The previous state captures the one-frame event.
		auto it = mouseButtonPreviousStates.find(glfwButton);
		if (it == mouseButtonPreviousStates.end())
			return false;
		return it->second == ButtonState::Pressed;
	}

	bool InputManager::isMouseButtonReleased(foundation::MouseButton button) const {
		int glfwButton = ToGLFW(button);
		// Check previous state because updateButtonStates() transitions Released→Up
		// before handleInput() is called. The previous state captures the one-frame event.
		auto it = mouseButtonPreviousStates.find(glfwButton);
		if (it == mouseButtonPreviousStates.end())
			return false;
		return it->second == ButtonState::Released;
	}

	bool InputManager::isKeyDown(foundation::Key key) const {
		int glfwKey = ToGLFW(key);
		auto it = keyStates.find(glfwKey);
		if (it == keyStates.end())
			return false;
		return it->second == ButtonState::Down || it->second == ButtonState::Pressed;
	}

	bool InputManager::isKeyPressed(foundation::Key key) const {
		int glfwKey = ToGLFW(key);
		// Check previous state because updateButtonStates() transitions Pressed→Down
		// before handleInput() is called. The previous state captures the one-frame event.
		auto it = keyPreviousStates.find(glfwKey);
		if (it == keyPreviousStates.end())
			return false;
		return it->second == ButtonState::Pressed;
	}

	bool InputManager::isKeyReleased(foundation::Key key) const {
		int glfwKey = ToGLFW(key);
		// Check previous state because updateButtonStates() transitions Released→Up
		// before handleInput() is called. The previous state captures the one-frame event.
		auto it = keyPreviousStates.find(glfwKey);
		if (it == keyPreviousStates.end())
			return false;
		return it->second == ButtonState::Released;
	}

	void InputManager::injectKey(foundation::Key key, bool down) {
		// Funnel through the exact path GLFW key events take, so the press/release edge
		// state machine (and isKeyPressed/isKeyDown/isKeyReleased) behaves identically.
		handleKeyInput(ToGLFW(key), down ? GLFW_PRESS : GLFW_RELEASE);
	}

	void InputManager::injectMousePosition(glm::vec2 position) {
		// hostMousePosition is left alone so the next host button/scroll can snap back to it.
		mousePosition = position;
	}

	std::optional<foundation::Key> InputManager::keyFromName(const std::string& name) {
		// keyFromName relies on the Key enum's letters, digits, and function keys being
		// contiguous (see InputTypes.h); these guard against a future reordering.
		static_assert(static_cast<int>(foundation::Key::Z) - static_cast<int>(foundation::Key::A) == 25, "Key A-Z must be contiguous");
		static_assert(
			static_cast<int>(foundation::Key::Num9) - static_cast<int>(foundation::Key::Num0) == 9, "Key Num0-9 must be contiguous"
		);
		static_assert(
			static_cast<int>(foundation::Key::F12) - static_cast<int>(foundation::Key::F1) == 11, "Key F1-F12 must be contiguous"
		);

		if (name.empty()) {
			return std::nullopt;
		}
		if (name.size() == 1) {
			const char c = name[0];
			if (c >= 'a' && c <= 'z') {
				return static_cast<foundation::Key>(static_cast<int>(foundation::Key::A) + (c - 'a'));
			}
			if (c >= 'A' && c <= 'Z') {
				return static_cast<foundation::Key>(static_cast<int>(foundation::Key::A) + (c - 'A'));
			}
			if (c >= '0' && c <= '9') {
				return static_cast<foundation::Key>(static_cast<int>(foundation::Key::Num0) + (c - '0'));
			}
		}

		std::string lower;
		lower.reserve(name.size());
		for (char c : name) {
			lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}

		if (lower == "space")
			return foundation::Key::Space;
		if (lower == "escape" || lower == "esc")
			return foundation::Key::Escape;
		if (lower == "enter" || lower == "return")
			return foundation::Key::Enter;
		if (lower == "tab")
			return foundation::Key::Tab;
		if (lower == "backspace")
			return foundation::Key::Backspace;
		if (lower == "delete" || lower == "del")
			return foundation::Key::Delete;
		if (lower == "insert")
			return foundation::Key::Insert;
		if (lower == "home")
			return foundation::Key::Home;
		if (lower == "end")
			return foundation::Key::End;
		if (lower == "pageup")
			return foundation::Key::PageUp;
		if (lower == "pagedown")
			return foundation::Key::PageDown;
		if (lower == "up")
			return foundation::Key::Up;
		if (lower == "down")
			return foundation::Key::Down;
		if (lower == "left")
			return foundation::Key::Left;
		if (lower == "right")
			return foundation::Key::Right;

		// Function keys F1..F12.
		if (lower.size() >= 2 && lower[0] == 'f') {
			try {
				const int n = std::stoi(lower.substr(1));
				if (n >= 1 && n <= 12) {
					return static_cast<foundation::Key>(static_cast<int>(foundation::Key::F1) + (n - 1));
				}
			} catch (...) {
				return std::nullopt;
			}
		}
		return std::nullopt;
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

			// Call external callback (e.g., UI event system) - can consume event
			if (s_instance->mouseButtonInputCallback) {
				auto btn = static_cast<foundation::MouseButton>(button);
				bool consumed = s_instance->mouseButtonInputCallback(btn, action, s_instance->mousePosition, mods);
				if (consumed) {
					return;	 // Event consumed, don't chain
				}
			}

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

			// Call external callback (e.g., UI event system for hover) - can consume event
			if (s_instance->mouseMoveInputCallback) {
				bool consumed = s_instance->mouseMoveInputCallback(s_instance->mousePosition);
				if (consumed) {
					return;	 // Event consumed, don't chain
				}
			}

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

			// Call external callback (e.g., UI event system for scroll) - can consume event
			if (s_instance->scrollInputCallback) {
				bool consumed = s_instance->scrollInputCallback(s_instance->scrollDelta, s_instance->mousePosition);
				if (consumed) {
					return;	 // Event consumed, don't chain
				}
			}

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
		// A host button event lands under the host cursor, reclaiming the pointer from
		// any injected position (a no-op when nothing was injected).
		mousePosition = hostMousePosition;
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
		hostMousePosition = glm::vec2(static_cast<float>(x), static_cast<float>(y));
		mousePosition = hostMousePosition;
	}

	void InputManager::handleScroll(double xoffset, double yoffset) {
		mousePosition = hostMousePosition; // reclaims the pointer, as in handleMouseButton
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
