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
    // Pull the near plane in as we approach the surface so deep zoom keeps depth
    // precision without clipping the globe. distance is in planet radii; the gap
    // to the surface is (distance - 1).
    float surfaceGap = std::max(distance - 1.0F, 0.0F);
    float nearPlane = std::clamp(0.2F * surfaceGap, kNearMin, kNearMax);
    return glm::perspective(glm::radians(kFovDeg), aspect, nearPlane, kFar);
}

void OrbitCamera::setMinDistance(float newMin) {
    minDist = std::max(newMin, kMinDistFloor);
    distance = std::clamp(distance, minDist, kMaxDist);
}

void OrbitCamera::beginDrag(float mouseX, float mouseY) {
    dragging = true;
    userInteracted = true;
    prevMouseX = mouseX;
    prevMouseY = mouseY;
}

void OrbitCamera::clampPitch() {
    float r = 0.0F;
    if (distance < kPitchUnlockDistance) {
        const float span = kPitchUnlockDistance - minDist;
        float t = span > 0.0F ? (kPitchUnlockDistance - distance) / span : 1.0F;
        t = std::clamp(t, 0.0F, 1.0F);
        r = t * kMaxPitchOffset;
    }
    const float lo = std::max(kHomePitch - r, -kPolePitch);
    const float hi = std::min(kHomePitch + r,  kPolePitch);
    pitch = std::clamp(pitch, lo, hi);
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
    clampPitch();
    idleTime = 0.0F;
}

void OrbitCamera::endDrag() {
    dragging = false;
}

void OrbitCamera::scroll(float delta) {
    distance -= delta * kScrollSens * distance;
    distance = std::clamp(distance, minDist, kMaxDist);
    // Zooming out tightens the allowed band, easing pitch back toward home.
    clampPitch();
    userInteracted = true;
    idleTime = 0.0F;
}

void OrbitCamera::nudge(float dYaw, float dPitch) {
    yaw   += dYaw;
    pitch += dPitch;
    clampPitch();
    userInteracted = true;
    idleTime = 0.0F;
}

void OrbitCamera::update(float dt) {
    if (!dragging) {
        // Apply inertia.
        yawVel   *= kInertia;
        pitchVel *= kInertia;
        yaw   += yawVel * dt * 60.0F;
        pitch += pitchVel * dt * 60.0F;
        clampPitch();

        // Gentle reveal spin while idle, but only until the user first touches
        // the globe -- after any interaction it stays put and never resumes.
        idleTime += dt;
        if (!userInteracted && idleTime > kIdleDelay) {
            float blend = std::min((idleTime - kIdleDelay) / 2.0F, 1.0F);
            yaw += kAutoYaw * dt * blend;
        }
    } else {
        idleTime = 0.0F;
    }
}

} // namespace planetview
