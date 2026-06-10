#include "PlanetMesh.h"

#include <world/worldgen/grid/SphereGrid.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace planetview {

PlanetMesh::PlanetMesh(PlanetMesh&& other) noexcept
    : ibo(other.ibo)
    , indexCount(other.indexCount)
    , vertsPerSide(other.vertsPerSide)
{
    for (int i = 0; i < 10; ++i) rhombi[i] = other.rhombi[i];
    other.ibo = 0;
    other.indexCount = 0;
    other.vertsPerSide = 0;
    for (auto& rm : other.rhombi) rm = {};
}

PlanetMesh& PlanetMesh::operator=(PlanetMesh&& other) noexcept {
    if (this != &other) {
        release();
        ibo = other.ibo;
        indexCount = other.indexCount;
        vertsPerSide = other.vertsPerSide;
        for (int i = 0; i < 10; ++i) rhombi[i] = other.rhombi[i];
        other.ibo = 0;
        other.indexCount = 0;
        other.vertsPerSide = 0;
        for (auto& rm : other.rhombi) rm = {};
    }
    return *this;
}

PlanetMesh::~PlanetMesh() {
    release();
}

void PlanetMesh::release() {
    for (auto& rm : rhombi) {
        if (rm.vao) { glDeleteVertexArrays(1, &rm.vao); rm.vao = 0; }
        if (rm.vbo) { glDeleteBuffers(1, &rm.vbo); rm.vbo = 0; }
        rm.vertexCount = 0;
    }
    if (ibo) { glDeleteBuffers(1, &ibo); ibo = 0; }
    indexCount = 0;
    vertsPerSide = 0;
}

void PlanetMesh::build(uint32_t subdivision, const worldgen::SphereGrid& grid) {
    if (isBuilt()) release();

    uint32_t v = std::min(subdivision, 128U);
    vertsPerSide = v + 1;
    uint32_t vps = vertsPerSide;

    // Shared index buffer — same topology for all 10 rhombi.
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(v) * v * 6U);
    for (uint32_t row = 0; row < v; ++row) {
        for (uint32_t col = 0; col < v; ++col) {
            uint32_t tl = row * vps + col;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + vps;
            uint32_t br = bl + 1;
            indices.push_back(tl); indices.push_back(tr); indices.push_back(bl);
            indices.push_back(tr); indices.push_back(br); indices.push_back(bl);
        }
    }
    indexCount = static_cast<uint32_t>(indices.size());

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Per-rhombus VBOs. Vertex: vec3 position (unit sphere) + vec2 uv = 5 floats.
    // Positions come from grid.rhombusPointOnSphere() — the icosahedral barycentric
    // mapping — so edge vertices on neighboring rhombi are numerically identical,
    // eliminating seam artifacts.
    struct Vertex { float px, py, pz, u, v; };
    std::vector<Vertex> verts(static_cast<size_t>(vps) * vps);

    for (uint32_t r = 0; r < 10U; ++r) {
        for (uint32_t row = 0; row < vps; ++row) {
            for (uint32_t col = 0; col < vps; ++col) {
                double uCoord = static_cast<double>(col) / static_cast<double>(v);
                double vCoord = static_cast<double>(row) / static_cast<double>(v);
                worldgen::Vec3d p = grid.rhombusPointOnSphere(r, uCoord, vCoord);
                // Already unit from rhombusPointOnSphere, but cast to float.
                size_t idx = static_cast<size_t>(row) * vps + col;
                verts[idx] = {
                    static_cast<float>(p.x),
                    static_cast<float>(p.y),
                    static_cast<float>(p.z),
                    static_cast<float>(uCoord),
                    static_cast<float>(vCoord)
                };
            }
        }

        auto& rm = rhombi[r];
        rm.vertexCount = static_cast<uint32_t>(verts.size());

        glGenVertexArrays(1, &rm.vao);
        glGenBuffers(1, &rm.vbo);

        glBindVertexArray(rm.vao);
        glBindBuffer(GL_ARRAY_BUFFER, rm.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                     verts.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

        // location 0: position (vec3)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(0));
        // location 1: uv (vec2)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(sizeof(float) * 3));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

} // namespace planetview
