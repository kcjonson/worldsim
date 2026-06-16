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

    // Set the closest orbit distance (planet radii). GlobeView picks this from
    // the grid subdivision so a single tile can reach ~50 px even at high n.
    void setMinDistance(float minDist);

    // Input feed methods — call each frame from the scene.
    void beginDrag(float mouseX, float mouseY);
    void drag(float mouseX, float mouseY);
    void endDrag();
    void scroll(float delta); // positive = zoom in

    // Keyboard pan: nudge yaw/pitch by radians (pitch obeys the same zoom gate
    // as drag, so up/down does nothing until zoomed in). Resets idle.
    void nudge(float dYaw, float dPitch);

    // Advance inertia + auto-rotate; dt in seconds.
    void update(float dt);

  private:
    // Re-clamp pitch to the currently allowed band. Up/down is locked to the
    // home orientation until the globe overfills the viewport, then opens up
    // proportionally without ever reaching the poles.
    void clampPitch();

    bool dragging{false};
    float prevMouseX{0.0F};
    float prevMouseY{0.0F};

    float yawVel{0.0F};
    float pitchVel{0.0F};

    float idleTime{0.0F};

    // Runtime min distance (set by GlobeView from n). The hard floor keeps the
    // camera just outside the unit sphere; high-n grids push closer.
    float minDist{1.05F};

    static constexpr float kMinDistFloor = 1.002F;
    static constexpr float kMaxDist = 8.0F;
    static constexpr float kFovDeg  = 45.0F;
    static constexpr float kNearMin = 0.0005F;
    static constexpr float kNearMax = 0.05F;
    static constexpr float kFar     = 20.0F;
    static constexpr float kInertia = 0.88F;
    static constexpr float kDragSens = 0.008F;
    static constexpr float kScrollSens = 0.15F;
    static constexpr float kIdleDelay = 3.0F;
    static constexpr float kAutoYaw   = 0.05F; // rad/s

    // Pitch (latitude) gate. The unit sphere fills the vertical viewport when
    // distance <= 1/sin(kFovDeg/2); above that the whole globe is visible and
    // up/down is locked to kHomePitch (spin-only). 1/sin(22.5deg) = 2.6131.
    static constexpr float kPitchUnlockDistance = 2.6131F;
    static constexpr float kHomePitch       = 0.3F; // resting tilt (matches default pitch)
    static constexpr float kPolePitch       = 1.5F; // hard pole-safety clamp (~86deg)
    static constexpr float kMaxPitchOffset  = 1.8F; // full-zoom swing from home (capped by kPolePitch)
};

} // namespace planetview
