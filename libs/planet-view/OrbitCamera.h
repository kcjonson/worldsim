#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace planetview {

// Orbit camera around the world origin.
// Input methods: beginDrag/drag/endDrag/scroll/update(dt)
// No direct GLFW coupling.
class OrbitCamera {
  public:
    // Camera parameters
    float yaw{0.0F};    // radians, horizontal rotation
    float pitch{0.3F};  // radians, vertical rotation (clamped)
    float distance{2.5F}; // in planet-radius units

    // Returns view matrix (world → camera).
    glm::mat4 viewMatrix() const;

    // Returns perspective projection matrix.
    glm::mat4 projMatrix(float aspect) const;

    // Combined MVP (model = identity).
    glm::mat4 mvpMatrix(float aspect) const { return projMatrix(aspect) * viewMatrix(); }

    // World-space camera position.
    glm::vec3 position() const;

    // Input feed methods — call each frame from the scene.
    void beginDrag(float mouseX, float mouseY);
    void drag(float mouseX, float mouseY);
    void endDrag();
    void scroll(float delta); // positive = zoom in

    // Advance inertia + auto-rotate; dt in seconds.
    void update(float dt);

  private:
    bool dragging{false};
    float prevMouseX{0.0F};
    float prevMouseY{0.0F};

    float yawVel{0.0F};
    float pitchVel{0.0F};

    float idleTime{0.0F};

    static constexpr float kMinDist = 1.05F;
    static constexpr float kMaxDist = 8.0F;
    static constexpr float kFovDeg  = 45.0F;
    static constexpr float kNear    = 0.05F;
    static constexpr float kFar     = 20.0F;
    static constexpr float kInertia = 0.88F;
    static constexpr float kDragSens = 0.008F;
    static constexpr float kScrollSens = 0.15F;
    static constexpr float kIdleDelay = 3.0F;
    static constexpr float kAutoYaw   = 0.05F; // rad/s
};

} // namespace planetview
