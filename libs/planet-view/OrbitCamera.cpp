#include "OrbitCamera.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace planetview {

glm::vec3 OrbitCamera::position() const {
    float cp = std::cos(pitch);
    return glm::vec3(
        distance * cp * std::cos(yaw),
        distance * cp * std::sin(yaw),
        distance * std::sin(pitch)
    );
}

glm::mat4 OrbitCamera::viewMatrix() const {
    return glm::lookAt(position(), glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, 1.0F));
}

glm::mat4 OrbitCamera::projMatrix(float aspect) const {
    return glm::perspective(glm::radians(kFovDeg), aspect, kNear, kFar);
}

void OrbitCamera::beginDrag(float mouseX, float mouseY) {
    dragging = true;
    prevMouseX = mouseX;
    prevMouseY = mouseY;
}

void OrbitCamera::drag(float mouseX, float mouseY) {
    if (!dragging) return;
    float dx = mouseX - prevMouseX;
    float dy = mouseY - prevMouseY;
    prevMouseX = mouseX;
    prevMouseY = mouseY;

    yawVel   = -dx * kDragSens;
    pitchVel = -dy * kDragSens;

    yaw   += yawVel;
    pitch += pitchVel;
    pitch = std::clamp(pitch, -1.5F, 1.5F);
    idleTime = 0.0F;
}

void OrbitCamera::endDrag() {
    dragging = false;
}

void OrbitCamera::scroll(float delta) {
    distance -= delta * kScrollSens * distance;
    distance = std::clamp(distance, kMinDist, kMaxDist);
    idleTime = 0.0F;
}

void OrbitCamera::update(float dt) {
    if (!dragging) {
        // Apply inertia.
        yawVel   *= kInertia;
        pitchVel *= kInertia;
        yaw   += yawVel * dt * 60.0F;
        pitch += pitchVel * dt * 60.0F;
        pitch = std::clamp(pitch, -1.5F, 1.5F);

        // Auto-rotate after idle.
        idleTime += dt;
        if (idleTime > kIdleDelay) {
            float blend = std::min((idleTime - kIdleDelay) / 2.0F, 1.0F);
            yaw += kAutoYaw * dt * blend;
        }
    } else {
        idleTime = 0.0F;
    }
}

} // namespace planetview
