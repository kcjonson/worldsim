#pragma once

#include <GL/glew.h>
#include <cstdint>
#include <vector>

namespace planetview {

// One per rhombus: a (V+1)x(V+1) vertex grid projected onto the unit sphere.
// Vertex layout: vec3 position (unit sphere), vec2 uv (rhombus-local [0,1]).
// All rhombi share the same index buffer (same strip topology).
struct RhombusMesh {
    GLuint vao{0};
    GLuint vbo{0};
    uint32_t vertexCount{0};
};

class PlanetMesh {
  public:
    PlanetMesh() = default;
    ~PlanetMesh();

    PlanetMesh(const PlanetMesh&) = delete;
    PlanetMesh& operator=(const PlanetMesh&) = delete;

    PlanetMesh(PlanetMesh&&) noexcept;
    PlanetMesh& operator=(PlanetMesh&&) noexcept;

    // Build the 10-rhombus mesh. V = min(subdivision, 128).
    void build(uint32_t subdivision);

    bool isBuilt() const { return indexCount > 0; }

    const RhombusMesh& rhombus(uint32_t r) const { return rhombi[r]; }
    GLuint sharedIbo() const { return ibo; }
    uint32_t indexCount{ 0 };
    uint32_t vertsPerSide{ 0 }; // V+1

  private:
    RhombusMesh rhombi[10];
    GLuint ibo{0};

    void release();
};

} // namespace planetview
