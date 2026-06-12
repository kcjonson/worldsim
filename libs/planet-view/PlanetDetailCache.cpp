#include "PlanetDetailCache.h"

#include "PlanetTileColor.h"

#include <world/worldgen/data/GeneratedWorld.h>
#include <world/worldgen/grid/SphereGrid.h>

#include <algorithm>

namespace planetview {

namespace {
constexpr int kPagesPerFrame = 8; // bake + upload budget per frame
}

PlanetDetailCache::~PlanetDetailCache() { release(); }

void PlanetDetailCache::release() {
    if (atlasTex) { glDeleteTextures(1, &atlasTex); atlasTex = 0; }
    if (tableTex) { glDeleteTextures(1, &tableTex); tableTex = 0; }
    tableCpu.clear();
    bakeQueue.clear();
    requestedThisFrame.clear();
    world_.reset();
}

void PlanetDetailCache::init(uint32_t newSubdivision) {
    release();
    subdivision_ = newSubdivision;
    pagesPerSide_ = pagesPerSide(newSubdivision);

    lru.init(kAtlasLayers);

    // Atlas array texture: 130x130 x kAtlasLayers RGBA8. NEAREST sampling — the
    // shader does its own per-pixel hex lookup, no interpolation across tiles.
    glGenTextures(1, &atlasTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8,
                 kPageTexels, kPageTexels, kAtlasLayers,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Page table: pagesPerSide^2 x 10 (rhombus per layer), R16UI, NEAREST.
    glGenTextures(1, &tableTex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tableTex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_R16UI,
                 static_cast<GLsizei>(pagesPerSide_),
                 static_cast<GLsizei>(pagesPerSide_), 10,
                 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    tableCpu.assign(static_cast<size_t>(pagesPerSide_) * pagesPerSide_ * 10, 0);
    uploadTable();

    bakeScratch.resize(static_cast<size_t>(kPageTexels) * kPageTexels * 4);
}

void PlanetDetailCache::setWorld(std::shared_ptr<const worldgen::GeneratedWorld> world,
                                 ColorMode mode) {
    world_ = std::move(world);
    mode_ = mode;
    // Invalidate every page — they re-bake on demand from the new snapshot.
    lru.clear();
    std::fill(tableCpu.begin(), tableCpu.end(), uint16_t{0});
    tableDirty = true;
    bakeQueue.clear();
}

void PlanetDetailCache::beginFrame() {
    requestedThisFrame.clear();
    bakeQueue.clear();
}

void PlanetDetailCache::requestPage(uint32_t rhombus, uint32_t pi, uint32_t pj) {
    if (!isReady() || !world_) return;
    if (pi >= pagesPerSide_ || pj >= pagesPerSide_) return;

    uint64_t key = packKey(rhombus, pi, pj);
    if (std::find(requestedThisFrame.begin(), requestedThisFrame.end(), key) !=
        requestedThisFrame.end())
        return;
    requestedThisFrame.push_back(key);

    if (lru.resident(key)) {
        lru.touch(key);
        return;
    }
    // Enqueue every non-resident visible page; endFrame bakes up to the per-frame
    // budget and the rest carry to subsequent frames (they stay visible, so they
    // are re-requested and bake over a few frames — bounded by the budget).
    bakeQueue.push_back({key, rhombus, pi, pj});
}

namespace {
// Fill texel rows [tyBegin, tyEnd) of a page into `dst` (kPageTexels^2 * 4).
// Single source of truth for both the parallel runtime bake and the test baker.
void bakePageRows(uint8_t* dst, uint32_t rhombus, uint32_t pi, uint32_t pj,
                  const worldgen::GeneratedWorld& world, ColorMode mode,
                  size_t tyBegin, size_t tyEnd) {
    const worldgen::SphereGrid& grid = *world.grid;
    for (size_t ty = tyBegin; ty < tyEnd; ++ty) {
        for (int tx = 0; tx < kPageTexels; ++tx) {
            int i, j;
            pageTexelToVertex(pi, pj, tx, static_cast<int>(ty), i, j);
            uint32_t tileId = grid.canonicalTile(rhombus, i, j);
            RGBA8 c = colorForTile(tileId, mode, world);
            size_t o = (ty * kPageTexels + tx) * 4;
            dst[o + 0] = c.r;
            dst[o + 1] = c.g;
            dst[o + 2] = c.b;
            dst[o + 3] = c.a;
        }
    }
}
} // namespace

void PlanetDetailCache::bakePageTexels(std::vector<uint8_t>& out, uint32_t rhombus,
                                       uint32_t pi, uint32_t pj,
                                       const worldgen::GeneratedWorld& world,
                                       ColorMode mode) {
    out.resize(static_cast<size_t>(kPageTexels) * kPageTexels * 4);
    bakePageRows(out.data(), rhombus, pi, pj, world, mode, 0, kPageTexels);
}

void PlanetDetailCache::uploadPage(const std::vector<uint8_t>& texels, int layer) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTex);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                    0, 0, layer,
                    kPageTexels, kPageTexels, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void PlanetDetailCache::setTableEntry(uint32_t rhombus, uint32_t pi, uint32_t pj,
                                      uint16_t entry) {
    size_t idx = static_cast<size_t>(rhombus) * pagesPerSide_ * pagesPerSide_ +
                 static_cast<size_t>(pj) * pagesPerSide_ + pi;
    if (idx < tableCpu.size()) {
        tableCpu[idx] = entry;
        tableDirty = true;
    }
}

void PlanetDetailCache::uploadTable() {
    glBindTexture(GL_TEXTURE_2D_ARRAY, tableTex);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0,
                    0, 0, 0,
                    static_cast<GLsizei>(pagesPerSide_),
                    static_cast<GLsizei>(pagesPerSide_), 10,
                    GL_RED_INTEGER, GL_UNSIGNED_SHORT, tableCpu.data());
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    tableDirty = false;
}

void PlanetDetailCache::endFrame() {
    if (!isReady() || !world_) return;

    int processed = 0;
    for (const PageReq& req : bakeQueue) {
        if (processed >= kPagesPerFrame) break;

        // Allocate an atlas slot (may evict an LRU page).
        uint64_t evicted = PlanetLru::kNoKey;
        int layer = lru.allocate(req.key, evicted);
        if (evicted != PlanetLru::kNoKey) {
            uint32_t er = static_cast<uint32_t>(evicted >> 48);
            uint32_t epi = static_cast<uint32_t>(evicted & 0xFFFFFF);
            uint32_t epj = static_cast<uint32_t>((evicted >> 24) & 0xFFFFFF);
            setTableEntry(er, epi, epj, 0);
        }

        // Bake serially (a 130x130 page is sub-millisecond) and upload. We do
        // NOT use the shared TaskPool here: the colorizer's async base-tier bake
        // may be driving it from another thread, and the pool services only one
        // parallelFor at a time.
        bakeScratch.resize(static_cast<size_t>(kPageTexels) * kPageTexels * 4);
        bakePageRows(bakeScratch.data(), req.rhombus, req.pi, req.pj,
                     *world_, mode_, 0, kPageTexels);
        uploadPage(bakeScratch, layer);
        setTableEntry(req.rhombus, req.pi, req.pj, encodePageEntry(layer));
        ++processed;
    }

    if (tableDirty) uploadTable();
}

void PlanetDetailCache::bind(GLuint atlasUnit, GLuint tableUnit) const {
    glActiveTexture(GL_TEXTURE0 + atlasUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTex);
    glActiveTexture(GL_TEXTURE0 + tableUnit);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tableTex);
}

} // namespace planetview
