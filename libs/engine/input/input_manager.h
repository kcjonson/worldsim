#pragma once

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <unordered_map>
#include <string>

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
    static void SetInstance(InputManager* instance);

    explicit InputManager(GLFWwindow* window);
    ~InputManager();

    // Disable copy/move
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;
    InputManager(InputManager&&) = delete;
    InputManager& operator=(InputManager&&) = delete;

    // Frame update - call once per frame before scene HandleInput
    void Update(float deltaTime);

    // Query API for scenes to read current input state
    glm::vec2 GetMousePosition() const { return m_mousePosition; }
    glm::vec2 GetMouseDelta() const { return m_mouseDelta; }
    bool IsMouseButtonDown(int button) const;
    bool IsMouseButtonPressed(int button) const;   // True only on frame button was pressed
    bool IsMouseButtonReleased(int button) const;  // True only on frame button was released
    bool IsDragging() const { return m_isDragging; }
    glm::vec2 GetDragStartPosition() const { return m_dragStartPos; }
    glm::vec2 GetDragDelta() const { return m_mousePosition - m_dragStartPos; }

    bool IsKeyDown(int key) const;
    bool IsKeyPressed(int key) const;    // True only on frame key was pressed
    bool IsKeyReleased(int key) const;   // True only on frame key was released

    float GetScrollDelta() const { return m_scrollDelta; }
    bool IsCursorInWindow() const { return m_cursorInWindow; }
    glm::vec2 GetWindowSize() const { return m_windowSize; }

    // Configuration setters
    void SetPanSpeed(float speed) { m_panSpeed = speed; }
    void SetZoomSpeed(float speed) { m_zoomSpeed = speed; }
    void SetEdgePanThreshold(float threshold) { m_edgePanThreshold = threshold; }
    void SetEdgePanSpeed(float speed) { m_edgePanSpeed = speed; }

    // GLFW static callbacks
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
    static void CursorEnterCallback(GLFWwindow* window, int entered);

private:
    // Singleton instance pointer
    static InputManager* s_instance;

    // GLFW window reference
    GLFWwindow* m_window;

    // Mouse state
    glm::vec2 m_mousePosition{0.0f};
    glm::vec2 m_lastMousePosition{0.0f};
    glm::vec2 m_mouseDelta{0.0f};
    glm::vec2 m_windowSize{800.0f, 600.0f};
    bool m_isDragging = false;
    glm::vec2 m_dragStartPos{0.0f};

    // Mouse button state tracking
    enum class ButtonState { Up, Pressed, Down, Released };
    std::unordered_map<int, ButtonState> m_mouseButtonStates;
    std::unordered_map<int, ButtonState> m_mouseButtonPreviousStates;

    // Keyboard state tracking
    std::unordered_map<int, ButtonState> m_keyStates;
    std::unordered_map<int, ButtonState> m_keyPreviousStates;

    // Scroll state
    float m_scrollDelta = 0.0f;

    // Window state
    bool m_cursorInWindow = true;

    // Configuration (for future use by scenes)
    float m_panSpeed = 100.0f;
    float m_zoomSpeed = 1.0f;
    float m_edgePanThreshold = 0.05f;  // 5% of screen width/height
    float m_edgePanSpeed = 50.0f;

    // Instance methods called by static callbacks
    void HandleKeyInput(int key, int action);
    void HandleMouseButton(int button, int action);
    void HandleMouseMove(double x, double y);
    void HandleScroll(double xoffset, double yoffset);
    void HandleCursorEnter(int entered);

    // Helper to update button state transitions
    void UpdateButtonStates();
};

} // namespace engine
