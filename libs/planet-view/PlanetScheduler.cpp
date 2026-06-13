#include "PlanetScheduler.h"

#include "PlanetDetailCache.h"
#include "PlanetPicker.h"

#include <world/worldgen/grid/SphereGrid.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace planetview {

namespace {
constexpr int kRaySamples = 25;  // NxN ray grid across the viewport
constexpr float kRequestPxPerTile = 2.0f;
constexpr int kPrefetchRing = 2; // expand each rhombus's page rect by N pages
} // namespace

float estimatePixelsPerTile(float distance, float fovDeg, int viewportH, uint32_t n) {
    if (n == 0 || viewportH <= 0) return 0.0f;
    // Tile angular size at the sub-camera point: a tile spans ~1/n of a rhombus
    // edge, and an icosahedron edge is ~1.107 rad; treat tile arc ~ 1.1/n rad on
    // the unit sphere. Its apparent half-angle from the camera, distance d radii:
    //   the surface point nearest the camera is at radius 1, camera at d, so a
    //   small surface arc s subtends ~ s/(d-1) radians (small-angle, d>1).
    float tileArc = 1.1f / static_cast<float>(n);
    float gap = std::max(distance - 1.0f, 1e-4f);
    float tileAngle = tileArc / gap;
    float fovRad = fovDeg * 3.14159265f / 180.0f;
    float pxPerRad = static_cast<float>(viewportH) / fovRad;
    return tileAngle * pxPerRad;
}

std::vector<PageRect> computeVisiblePages(const OrbitCamera& camera, float aspect,
                                          int viewportW, int viewportH,
                                          const worldgen::SphereGrid& grid,
                                          uint32_t n, int prefetchRing) {
    std::vector<PageRect> out;
    if (n == 0) return out;

    float pxPerTile = estimatePixelsPerTile(camera.distance, 45.0f, viewportH, n);
    if (pxPerTile < kRequestPxPerTile) return out; // base tier suffices

    glm::mat4 view = camera.viewMatrix();
    glm::mat4 proj = camera.projMatrix(aspect);
    glm::mat4 invProj = glm::inverse(proj);
    glm::mat4 invView = glm::inverse(view);
    glm::vec3 origin = camera.position();

    // Per-rhombus uv bounding box of ray hits.
    struct Box { bool hit{false}; double u0{1e9}, v0{1e9}, u1{-1e9}, v1{-1e9}; };
    std::array<Box, 10> boxes;

    for (int sy = 0; sy < kRaySamples; ++sy) {
        for (int sx = 0; sx < kRaySamples; ++sx) {
            float ndcX = (static_cast<float>(sx) / (kRaySamples - 1)) * 2.0f - 1.0f;
            float ndcY = (static_cast<float>(sy) / (kRaySamples - 1)) * 2.0f - 1.0f;
            glm::vec3 ray = ndcToRay(ndcX, ndcY, invProj, invView);
            auto hit = raySphereHit(origin, ray);
            if (!hit) continue;
            glm::vec3 p = glm::normalize(*hit);
            uint32_t rh{};
            double u{}, v{};
            grid.locateRhombusUV({p.x, p.y, p.z}, rh, u, v);
            if (rh >= 10) continue;
            Box& b = boxes[rh];
            b.hit = true;
            b.u0 = std::min(b.u0, u); b.v0 = std::min(b.v0, v);
            b.u1 = std::max(b.u1, u); b.v1 = std::max(b.v1, v);
        }
    }

    uint32_t pps = pagesPerSide(n);
    for (uint32_t r = 0; r < 10; ++r) {
        const Box& b = boxes[r];
        if (!b.hit) continue;
        // uv -> vertex (i in [1..n], j in [0..n-1]) -> page index.
        int i0 = std::max(1, static_cast<int>(std::floor(b.u0 * n)));
        int i1 = std::min(static_cast<int>(n), static_cast<int>(std::ceil(b.u1 * n)));
        int j0 = std::max(0, static_cast<int>(std::floor(b.v0 * n)));
        int j1 = std::min(static_cast<int>(n) - 1, static_cast<int>(std::ceil(b.v1 * n)));
        if (i0 > i1 || j0 > j1) continue;

        uint32_t pi0, pj0, pi1, pj1;
        vertexToPage(i0, j0, pi0, pj0);
        vertexToPage(i1, j1, pi1, pj1);

        int rpi0 = std::max(0, static_cast<int>(pi0) - prefetchRing);
        int rpj0 = std::max(0, static_cast<int>(pj0) - prefetchRing);
        int rpi1 = std::min(static_cast<int>(pps) - 1, static_cast<int>(pi1) + prefetchRing);
        int rpj1 = std::min(static_cast<int>(pps) - 1, static_cast<int>(pj1) + prefetchRing);

        out.push_back({r, static_cast<uint32_t>(rpi0), static_cast<uint32_t>(rpj0),
                       static_cast<uint32_t>(rpi1), static_cast<uint32_t>(rpj1)});
    }
    return out;
}

void schedulePages(PlanetDetailCache& cache, const OrbitCamera& camera, float aspect,
                   int viewportW, int viewportH, const worldgen::SphereGrid& grid,
                   uint32_t n) {
    cache.beginFrame();
    auto rects = computeVisiblePages(camera, aspect, viewportW, viewportH, grid, n,
                                     kPrefetchRing);
    for (const PageRect& pr : rects) {
        for (uint32_t pj = pr.pj0; pj <= pr.pj1; ++pj)
            for (uint32_t pi = pr.pi0; pi <= pr.pi1; ++pi)
                cache.requestPage(pr.rhombus, pi, pj);
    }
    cache.endFrame();
}

} // namespace planetview
