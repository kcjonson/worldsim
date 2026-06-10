#pragma once

// Geometry helpers for the planet mesh.
// No GL dependency — safe to include in tests.

namespace planetview {

// Expected vertex count for a mesh built with subdivision n:
// Each rhombus has (min(n,128)+1)^2 vertices; 10 rhombi total.
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
