#pragma once

// Detail tier of the two-tier hex renderer: a camera-following page cache that
// answers "zoomed in" at any subdivision n. See PlanetPageMath.h for the page /
// texel layout.
//
// GPU resources:
//   - Atlas: GL_TEXTURE_2D_ARRAY, 130x130 x kAtlasLayers, RGBA8. Holds resident
//     pages; LRU eviction (PlanetLru) keeps memory constant in n.
//   - Page table: GL_TEXTURE_2D_ARRAY of GL_R16UI, pagesPerSide x pagesPerSide
//     x 10 (one layer per rhombus). Entry 0 = not resident; layer+1 = resident.
//
// CPU flow per frame:
//   beginFrame()  — start a fresh visible/request set.
//   requestPage() — scheduler calls this for each visible (+ prefetch) page;
//                   resident pages are touched, others queued for bake.
//   endFrame()    — bake + upload up to kPagesPerFrame (8) pages serially on
//                   the render thread (each 130x130 page is sub-millisecond;
//                   no TaskPool used here), then push page-table updates.
// To drop all pages on a snapshot or color-mode change, call setWorld().

#include "PlanetColorizer.h" // ColorMode
#include "PlanetLru.h"
#include "PlanetPageMath.h"

#include <GL/glew.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace worldgen {
struct GeneratedWorld;
}

namespace planetview {

class PlanetDetailCache {
  public:
    static constexpr int kAtlasLayers = 256;

    PlanetDetailCache() = default;
    ~PlanetDetailCache();

    PlanetDetailCache(const PlanetDetailCache&) = delete;
    PlanetDetailCache& operator=(const PlanetDetailCache&) = delete;

    // Allocate GL resources for subdivision n. Call once after the context and
    // after the grid is known. Pages bake serially on the render thread (each is
    // tiny), so no TaskPool is needed.
    void init(uint32_t subdivision);

    bool isReady() const { return atlasTex != 0; }

    // Swap in a new world snapshot / color mode. Invalidates all pages.
    void setWorld(std::shared_ptr<const worldgen::GeneratedWorld> world, ColorMode mode);

    // --- per-frame scheduler API ---
    void beginFrame();
    // Request page (rhombus, pi, pj). Resident -> touched; otherwise queued
    // (deduplicated) for baking, capped per frame.
    void requestPage(uint32_t rhombus, uint32_t pi, uint32_t pj);
    void endFrame();

    // Bind atlas to `atlasUnit` and page table to `tableUnit`.
    void bind(GLuint atlasUnit, GLuint tableUnit) const;

    uint32_t pagesPerSideValue() const { return pagesPerSide_; }

    // Pack/unpack a page key (rhombus<<48 | pj<<24 | pi). Public for tests.
    static uint64_t packKey(uint32_t rhombus, uint32_t pi, uint32_t pj) {
        return (static_cast<uint64_t>(rhombus) << 48) |
               (static_cast<uint64_t>(pj) << 24) | static_cast<uint64_t>(pi);
    }

  private:
    struct PageReq {
        uint64_t key{};
        uint32_t rhombus{}, pi{}, pj{};
    };

    uint32_t subdivision_{0};
    uint32_t pagesPerSide_{0};

    std::shared_ptr<const worldgen::GeneratedWorld> world_;
    ColorMode mode_{ColorMode::Terrain};

    GLuint atlasTex{0};   // GL_TEXTURE_2D_ARRAY 130x130 x kAtlasLayers RGBA8
    GLuint tableTex{0};   // GL_TEXTURE_2D_ARRAY pagesPerSide^2 x 10, R16UI

    PlanetLru lru;

    // Page-table CPU mirror (10 * pagesPerSide^2 entries), so we can push small
    // updates and clear cheaply. Layout: rhombus * pagesPerSide^2 + pj*pps + pi.
    std::vector<uint16_t> tableCpu;
    bool tableDirty{false};

    // Per-frame request bookkeeping.
    std::vector<uint64_t> requestedThisFrame; // for dedup
    std::vector<PageReq>  bakeQueue;          // pages to bake (not yet resident)

    // Scratch RGBA8 buffer reused across page bakes (130*130*4).
    std::vector<uint8_t> bakeScratch;

    void release();
    void uploadPage(const std::vector<uint8_t>& texels, int layer);
    void setTableEntry(uint32_t rhombus, uint32_t pi, uint32_t pj, uint16_t entry);
    void uploadTable();

    static void bakePageTexels(std::vector<uint8_t>& out, uint32_t rhombus,
                               uint32_t pi, uint32_t pj,
                               const worldgen::GeneratedWorld& world, ColorMode mode);

  public:
    // Pure texel-baker exposed for tests: fills `out` (kPageTexels^2 * 4) with
    // RGBA8 for page (rhombus, pi, pj). Border texels go through canonicalTile.
    static void bakePageForTest(std::vector<uint8_t>& out, uint32_t rhombus,
                                uint32_t pi, uint32_t pj,
                                const worldgen::GeneratedWorld& world, ColorMode mode) {
        bakePageTexels(out, rhombus, pi, pj, world, mode);
    }
};

} // namespace planetview
