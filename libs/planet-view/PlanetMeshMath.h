#pragma once

// Pure-math helpers for rhombus→sphere mapping used by PlanetMesh and PlanetGenerator.
// No GL dependency — safe to include in tests.

#include <cmath>

namespace planetview {

struct SpherePt { float x, y, z; };

// Maps (rhombus [0-9], u [0,1], v [0,1]) to a unit-sphere point.
// This is the canonical mapping shared by PlanetMesh and PlanetGenerator.
inline SpherePt rhombusToSphere(unsigned r, float u, float v) {
    constexpr float kPi = 3.14159265358979F;
    float lon{0.0F}, lat{0.0F};

    if (r < 4) {
        float baseLon = static_cast<float>(r) * 0.5F * kPi;
        lon = baseLon + u * 0.5F * kPi;
        lat = (0.5F - v * 0.5F) * kPi * 0.5F + 0.25F * kPi;
        lon += v * 0.25F * kPi;
    } else if (r < 8) {
        float baseLon = static_cast<float>(r - 4) * 0.5F * kPi;
        lon = baseLon + u * 0.5F * kPi;
        lat = (1.0F - v) * kPi * 0.5F - 0.25F * kPi;
    } else {
        float baseLon = static_cast<float>(r - 8) * kPi;
        lon = baseLon + u * kPi;
        lat = -(0.5F * kPi * 0.5F + v * 0.25F * kPi);
    }

    float cosLat = std::cos(lat);
    float len    = cosLat; // z = sin(lat); len of (cosLat,cosLat,sinLat) = 1
    (void)len;
    SpherePt p{ cosLat * std::cos(lon), cosLat * std::sin(lon), std::sin(lat) };
    // Normalise (should already be unit but floating point).
    float mag = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
    if (mag > 0.0F) { p.x /= mag; p.y /= mag; p.z /= mag; }
    return p;
}

// Expected vertex count for a mesh built with subdivision n:
// Each rhombus has (min(n,128)+1)^2 vertices.
inline unsigned expectedVertexCount(unsigned subdivision) {
    unsigned v = (subdivision < 128U ? subdivision : 128U) + 1U;
    return 10U * v * v;
}

// Expected index count: 10 * min(n,128)^2 * 6 (2 triangles per quad, 6 indices each).
inline unsigned expectedIndexCount(unsigned subdivision) {
    unsigned v = (subdivision < 128U ? subdivision : 128U);
    return 10U * v * v * 6U;
}

} // namespace planetview
