#pragma once

// Detail-page scheduler: turns the camera's view of the globe into the set of
// detail pages (rhombus, pi, pj) worth keeping resident, then drives the cache.
//
// Visibility is approximated conservatively by casting a coarse grid of rays
// through the viewport, ray/sphere-intersecting, and accumulating a per-rhombus
// uv bounding box of the hits. Pages overlapping that box (expanded by a one-
// page prefetch ring) are requested. Pixels-per-tile is estimated from the
// orbit distance; below ~2 px/tile the base tier is enough and we request none.

#include "OrbitCamera.h"
#include "PlanetPageMath.h"

#include <cstdint>
#include <vector>

namespace worldgen {
class SphereGrid;
}

namespace planetview {

class PlanetDetailCache;

// A requested page range within one rhombus (inclusive), in page coordinates.
struct PageRect {
    uint32_t rhombus{};
    uint32_t pi0{}, pj0{}, pi1{}, pj1{}; // inclusive page bounds
};

// Estimated pixels per tile at the given orbit distance and viewport. Uses the
// angular size of one tile (~ 2/(n) radians of arc near the sub-camera point)
// against the vertical field of view. Conservative, monotone in distance.
float estimatePixelsPerTile(float distance, float fovDeg, int viewportH, uint32_t n);

// Compute the page rects to request for the current camera. Pure: no GL, no
// cache mutation — testable. `prefetchRing` expands each rect by that many pages.
std::vector<PageRect> computeVisiblePages(const OrbitCamera& camera, float aspect,
                                          int viewportW, int viewportH,
                                          const worldgen::SphereGrid& grid,
                                          uint32_t n, int prefetchRing);

// Run a full scheduler frame: beginFrame, request the computed pages, endFrame.
void schedulePages(PlanetDetailCache& cache, const OrbitCamera& camera, float aspect,
                   int viewportW, int viewportH, const worldgen::SphereGrid& grid,
                   uint32_t n);

} // namespace planetview
