#pragma once

#include "input/InputTypes.h"
#include <GLFW/glfw3.h>
#include <functional>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace engine {

	/**
	 * InputManager
	 *
	 * Centralized input handling system that tracks mouse, keyboard, and scroll state.
	 * Uses singleton pattern for global access (like SceneManager).
	 *
	 * Responsibilities:
	 * - Register GLFW callbacks and route to instance methods
	 * - Track current input state (mouse position, button states, key states)
	 * - Provide query API for scenes to read input state
	 *
	 * Non-responsibilities:
	 * - Camera control (scenes implement their own)
	 * - UI component input forwarding (handled by UI components)
	 * - Game logic (handled by scenes)
	 */
	class InputManager {
	  public:
		// Singleton access
		static InputManager& Get();
		static void			 setInstance(InputManager* instance);

		explicit InputManager(GLFWwindow* window);
		~InputManager();

		// Disable copy/move
		InputManager(const InputManager&) = delete;
		InputManager& operator=(const InputManager&) = delete;
		InputManager(InputManager&&) = delete;
		InputManager& operator=(InputManager&&) = delete;

		// Frame update - call once per frame before scene HandleInput
		void update(float deltaTime);

		// Query API for scenes to read current input state
		glm::vec2 getMousePosition() const { return mousePosition; }
		glm::vec2 getMouseDelta() const { return mouseDelta; }
		bool	  isMouseButtonDown(MouseButton button) const;
		bool	  isMouseButtonPressed(MouseButton button) const;  // True only on frame button was pressed
		bool	  isMouseButtonReleased(MouseButton button) const; // True only on frame button was released
		bool	  isDragging() const { return dragging; }
		glm::vec2 getDragStartPosition() const { return dragStartPos; }
		glm::vec2 getDragDelta() const { return mousePosition - dragStartPos; }

		bool isKeyDown(Key key) const;
		bool isKeyPressed(Key key) const;  // True only on frame key was pressed
		bool isKeyReleased(Key key) const; // True only on frame key was released

		float getScrollDelta() const { return scrollDelta; }
		float consumeScrollDelta() {
			float delta = scrollDelta;
			scrollDelta = 0.0F;
			return delta;
		}
		bool	  isCursorInWindow() const { return cursorInWindow; }
		glm::vec2 getWindowSize() const { return windowSize; }

		// Callbacks for external systems (e.g., FocusManager for keyboard, UI event system for mouse)
		using KeyInputCallback = std::function<bool(Key key, int action, int mods)>;
		using CharInputCallback = std::function<bool(char32_t codepoint)>;

		// Mouse event callbacks - return true to consume the event
		using MouseButtonInputCallback = std::function<bool(MouseButton button, int action, glm::vec2 position, int mods)>;
		using MouseMoveInputCallback = std::function<bool(glm::vec2 position)>;
		using ScrollInputCallback = std::function<bool(float delta, glm::vec2 position)>;

		void setKeyInputCallback(KeyInputCallback callback) { keyInputCallback = callback; }
		void setCharInputCallback(CharInputCallback callback) { charInputCallback = callback; }
		void setMouseButtonInputCallback(MouseButtonInputCallback callback) { mouseButtonInputCallback = callback; }
		void setMouseMoveInputCallback(MouseMoveInputCallback callback) { mouseMoveInputCallback = callback; }
		void setScrollInputCallback(ScrollInputCallback callback) { scrollInputCallback = callback; }

		// Configuration setters
		void setPanSpeed(float speed) { panSpeed = speed; }
		void setZoomSpeed(float speed) { zoomSpeed = speed; }
		void setEdgePanThreshold(float threshold) { edgePanThreshold = threshold; }
		void setEdgePanSpeed(float speed) { edgePanSpeed = speed; }

		// GLFW static callbacks
		static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void CharCallback(GLFWwindow* window, unsigned int codepoint);
		static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
		static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
		static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
		static void CursorEnterCallback(GLFWwindow* window, int entered);

	  private:
		// Singleton instance pointer
		static InputManager* s_instance;

		// Store original callbacks (for chaining)
		GLFWkeyfun		   previousKeyCallback = nullptr;
		GLFWcharfun		   previousCharCallback = nullptr;
		GLFWmousebuttonfun previousMouseButtonCallback = nullptr;
		GLFWcursorposfun   previousCursorPosCallback = nullptr;
		GLFWscrollfun	   previousScrollCallback = nullptr;
		GLFWcursorenterfun previousCursorEnterCallback = nullptr;

		// GLFW window reference
		GLFWwindow* window;

		// Mouse state
		glm::vec2 mousePosition{0.0f};
		glm::vec2 lastMousePosition{0.0f};
		glm::vec2 mouseDelta{0.0f};
		glm::vec2 windowSize{800.0f, 600.0f};
		bool	  dragging = false;
		glm::vec2 dragStartPos{0.0f};

		// Mouse button state tracking
		enum class ButtonState { Up, Pressed, Down, Released };
		std::unordered_map<int, ButtonState> mouseButtonStates;
		std::unordered_map<int, ButtonState> mouseButtonPreviousStates;

		// Keyboard state tracking
		std::unordered_map<int, ButtonState> keyStates;
		std::unordered_map<int, ButtonState> keyPreviousStates;

		// Scroll state
		float scrollDelta = 0.0f;

		// Window state
		bool cursorInWindow = true;

		// Configuration (for future use by scenes)
		float panSpeed = 100.0f;
		float zoomSpeed = 1.0f;
		float edgePanThreshold = 0.05f; // 5% of screen width/height
		float edgePanSpeed = 50.0f;

		// Instance methods called by static callbacks
		void handleKeyInput(int key, int action);
		void handleCharInput(unsigned int codepoint);
		void handleMouseButton(int button, int action);
		void handleMouseMove(double x, double y);
		void handleScroll(double xoffset, double yoffset);
		void handleCursorEnter(int entered);

		// Helper to update button state transitions
		void updateButtonStates();

		// External callbacks (for FocusManager integration)
		KeyInputCallback  keyInputCallback{};
		CharInputCallback charInputCallback{};

		// External callbacks (for UI event system)
		MouseButtonInputCallback mouseButtonInputCallback{};
		MouseMoveInputCallback	 mouseMoveInputCallback{};
		ScrollInputCallback		 scrollInputCallback{};
	};

} // namespace engine
