#include "PlanetMesh.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace planetview {

namespace {

constexpr float kPi = 3.14159265358979F;

struct Vec3f { float x, y, z; };

// Maps (rhombus, u, v) in [0,1]^2 to a unit-sphere direction.
// 10 rhombi in HEALPix-inspired layout: 4 north-polar, 4 equatorial, 2 south-polar.
// This is the same mapping used by PlanetGenerator for tile placement.
Vec3f rhombusPoint(uint32_t r, float u, float v) {
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
    return { cosLat * std::cos(lon), cosLat * std::sin(lon), std::sin(lat) };
}

} // namespace

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

void PlanetMesh::build(uint32_t subdivision) {
    if (isBuilt()) release();

    uint32_t v = std::min(subdivision, 128U);
    vertsPerSide = v + 1;
    uint32_t vps = vertsPerSide;

    // Build shared index buffer (same for all rhombi).
    // Each quad = 2 triangles; (v)^2 quads.
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(v) * v * 6U);
    for (uint32_t row = 0; row < v; ++row) {
        for (uint32_t col = 0; col < v; ++col) {
            uint32_t tl = row * vps + col;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + vps;
            uint32_t br = bl + 1;
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }
    indexCount = static_cast<uint32_t>(indices.size());

    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
                 indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Build per-rhombus VBOs. Vertex: vec3 position + vec2 uv = 5 floats.
    struct Vertex { float px, py, pz, u, v; };
    std::vector<Vertex> verts(static_cast<size_t>(vps) * vps);

    for (uint32_t r = 0; r < 10U; ++r) {
        for (uint32_t row = 0; row < vps; ++row) {
            for (uint32_t col = 0; col < vps; ++col) {
                float uCoord = static_cast<float>(col) / static_cast<float>(v);
                float vCoord = static_cast<float>(row) / static_cast<float>(v);
                Vec3f p = rhombusPoint(r, uCoord, vCoord);
                // Normalise to unit sphere.
                float len = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
                if (len > 0.0F) { p.x /= len; p.y /= len; p.z /= len; }
                size_t idx = static_cast<size_t>(row) * vps + col;
                verts[idx] = { p.x, p.y, p.z, uCoord, vCoord };
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
