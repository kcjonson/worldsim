#pragma once

#include "OrbitCamera.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <optional>

namespace planetview {

struct LatLon {
    float latDeg{0.0F}; // -90 to 90
    float lonDeg{0.0F}; // -180 to 180
};

// Ray-sphere intersect against the unit sphere; returns world-space hit position.
inline std::optional<glm::vec3> raySphereHit(const glm::vec3& origin, const glm::vec3& dir) {
    // Unit sphere at origin: |P + t*D|^2 = 1
    float b = 2.0F * glm::dot(origin, dir);
    float c = glm::dot(origin, origin) - 1.0F;
    float disc = b * b - 4.0F * c;
    if (disc < 0.0F) return std::nullopt;
    float t = (-b - std::sqrt(disc)) * 0.5F;
    if (t < 0.0F) t = (-b + std::sqrt(disc)) * 0.5F;
    if (t < 0.0F) return std::nullopt;
    return origin + t * dir;
}

// Convert NDC (x,y in [-1,1]) to a world-space ray.
inline glm::vec3 ndcToRay(float ndcX, float ndcY,
                           const glm::mat4& invProj, const glm::mat4& invView) {
    glm::vec4 clipDir(ndcX, ndcY, -1.0F, 1.0F);
    glm::vec4 viewDir = invProj * clipDir;
    viewDir.z = -1.0F; viewDir.w = 0.0F;
    glm::vec4 worldDir = invView * viewDir;
    return glm::normalize(glm::vec3(worldDir));
}

// Try to pick a lat/lon from NDC coordinates.
inline std::optional<LatLon> pick(const OrbitCamera& cam, float aspect, float ndcX, float ndcY) {
    glm::mat4 view = cam.viewMatrix();
    glm::mat4 proj = cam.projMatrix(aspect);
    glm::mat4 invProj = glm::inverse(proj);
    glm::mat4 invView = glm::inverse(view);

    glm::vec3 ray = ndcToRay(ndcX, ndcY, invProj, invView);
    glm::vec3 origin = cam.position();

    auto hit = raySphereHit(origin, ray);
    if (!hit) return std::nullopt;

    glm::vec3 p = glm::normalize(*hit);
    float lat = std::asin(std::clamp(p.z, -1.0F, 1.0F));
    float lon = std::atan2(p.y, p.x); // NOLINT

    constexpr float kRad2Deg = 57.2957795F;
    return LatLon{ lat * kRad2Deg, lon * kRad2Deg };
}

// Project a unit-sphere point to screen pixel coordinates.
// Returns false if the point is behind the camera.
inline bool projectToScreen(const glm::vec3& unitPos, const OrbitCamera& cam,
                             float aspect, int vpW, int vpH,
                             float& outX, float& outY) {
    glm::mat4 mvp = cam.mvpMatrix(aspect);
    glm::vec4 clip = mvp * glm::vec4(unitPos, 1.0F);
    if (clip.w <= 0.0F) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0F || ndc.z > 1.0F) return false;
    outX = (ndc.x * 0.5F + 0.5F) * static_cast<float>(vpW);
    outY = (1.0F - (ndc.y * 0.5F + 0.5F)) * static_cast<float>(vpH);
    return true;
}

// Convert a LatLon to a unit-sphere position.
inline glm::vec3 latLonToUnitSphere(float latDeg, float lonDeg) {
    constexpr float kDeg2Rad = 0.01745329F;
    float lat = latDeg * kDeg2Rad;
    float lon = lonDeg * kDeg2Rad;
    float cosLat = std::cos(lat);
    return { cosLat * std::cos(lon), cosLat * std::sin(lon), std::sin(lat) };
}

} // namespace planetview
