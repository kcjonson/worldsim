#include "PlanetColorizer.h"

#include "PlanetTileColor.h"

#include <world/worldgen/data/GeneratedWorld.h>
#include <world/worldgen/grid/SphereGrid.h>

#include <threading/TaskPool.h>

#include <algorithm>
#include <chrono>
#include <future>

namespace planetview {

namespace {
// Base-tier resolution cap = the product subdivision max (PlanetIO kMaxSubdivision
// = 2048). At this cap mip 0 carries one texel per tile for every generatable world,
// so the hardware trilinear mip chain (GL_LINEAR_MIPMAP_LINEAR) stays crisp at every
// zoom -- and the camera can't zoom past ~50 px/tile, so a finer level is never seen.
constexpr uint32_t kBaseMax = 2048;

// Fill texel rows [jb, je) of rhombus `r`'s base texture into `dst` (texSize^2*4).
// Texel (i,j) maps to an owned chart vertex via canonicalTile, so seam/pole
// vertices resolve to the same tile the CPU assigns. For n > texSize the
// texel->vertex map downsamples (nearest); the mip chain answers coarser views.
// Single source of truth for the parallel runtime bake and the test baker.
void bakeRhombusRows(uint8_t* dst, uint32_t r, uint32_t texSize, uint32_t n,
                     const worldgen::SphereGrid& grid,
                     const worldgen::GeneratedWorld& world, ColorMode mode,
                     size_t jb, size_t je) {
    for (size_t j = jb; j < je; ++j) {
        for (uint32_t i = 0; i < texSize; ++i) {
            // Nearest-vertex-at-texel-center: texel k's center sits at uv =
            // (k+0.5)/texSize; the nearest chart vertex is
            //   floor((k+0.5)*n/texSize) = (2k+1)*n / (2*texSize)  [integer].
            // This keeps the range in [0..n-1] (never reaches the seam vertex
            // n), so i=0 and j=n have no texel-center coverage by construction
            // — those are seam vertices owned by the adjacent rhombus's texture.
            // Poles likewise have no owned texel center in the base tier; the
            // detail tier covers them via canonicalTile on its border texels.
            uint32_t ti = (2U * i + 1U) * n / (2U * texSize);
            uint32_t tj = (2U * static_cast<uint32_t>(j) + 1U) * n / (2U * texSize);
            uint32_t tileId = grid.canonicalTile(
                r, static_cast<int>(ti) + 1, static_cast<int>(tj));
            RGBA8 c = colorForTile(tileId, mode, world);
            size_t o = (static_cast<size_t>(j) * texSize + i) * 4;
            dst[o + 0] = c.r;
            dst[o + 1] = c.g;
            dst[o + 2] = c.b;
            dst[o + 3] = c.a;
        }
    }
}
} // namespace

const char* colorModeName(ColorMode m) {
    switch (m) {
        case ColorMode::Terrain:       return "Terrain";
        case ColorMode::Temperature:   return "Temperature";
        case ColorMode::Precipitation: return "Precipitation";
        case ColorMode::Biome:         return "Biome";
        case ColorMode::Plates:        return "Plates";
        case ColorMode::Snow:          return "Snow";
        case ColorMode::Combined:      return "Combined";
        case ColorMode::Hydrology:     return "Hydrology";
        default:                       return "Unknown";
    }
}

PlanetColorizer::~PlanetColorizer() {
    release(); // drains bakeFuture and resets bake state before GL teardown
}

void PlanetColorizer::release() {
    // Drain any in-flight async bake before tearing down GL resources, so the
    // worker never writes into a BakeResult that has been abandoned. Non-
    // throwing: if the bake threw, swallow here (the GL teardown below is still
    // valid; the error was already logged or will be diagnosed by init failing).
    if (bakeFuture.valid()) {
        try { bakeFuture.get(); } catch (...) {}
    }

    // Reset all bake state to constructed defaults so a subsequent init() +
    // requestBake() starts clean without stale generation artifacts.
    baking = false;
    dirty = false;
    inFlight.reset();
    ready.reset();
    pendingWorld.reset();
    taskPool = nullptr;
    uploadCursor = 0;

    for (auto& t : textures) {
        if (t) { glDeleteTextures(1, &t); t = 0; }
    }
    texSize = 0;
    contentReady = false;
}

void PlanetColorizer::init(uint32_t newSubdivision) {
    release();
    subdivision = newSubdivision;
    texSize = std::min(newSubdivision, kBaseMax);
    glGenTextures(10, textures);
    std::vector<uint8_t> buf(static_cast<size_t>(texSize) * texSize * 4, 128);
    for (auto& px : buf) px = 128;
    for (size_t i = 3; i < buf.size(); i += 4) buf[i] = 255; // alpha
    for (uint32_t r = 0; r < 10U; ++r) {
        glBindTexture(GL_TEXTURE_2D, textures[r]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(texSize), static_cast<GLsizei>(texSize),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PlanetColorizer::bakeInto(BakeResult& out, uint32_t texSize, uint32_t n,
                               const worldgen::GeneratedWorld& world, ColorMode mode,
                               foundation::TaskPool& pool) {
    const worldgen::SphereGrid& grid = *world.grid;
    for (uint32_t r = 0; r < 10U; ++r) {
        auto& dst = out.rhombi[r];
        dst.resize(static_cast<size_t>(texSize) * texSize * 4);
        uint8_t* p = dst.data();
        pool.parallelFor(0, texSize, 16, [&](size_t jb, size_t je) {
            bakeRhombusRows(p, r, texSize, n, grid, world, mode, jb, je);
        });
    }
}

void PlanetColorizer::bakeRhombusForTest(std::vector<uint8_t>& out, uint32_t rhombus,
                                         uint32_t texSize, uint32_t n,
                                         const worldgen::GeneratedWorld& world,
                                         ColorMode mode) {
    out.resize(static_cast<size_t>(texSize) * texSize * 4);
    bakeRhombusRows(out.data(), rhombus, texSize, n, *world.grid, world, mode, 0,
                    texSize);
}

void PlanetColorizer::requestBake(std::shared_ptr<const worldgen::GeneratedWorld> world,
                                  ColorMode mode, foundation::TaskPool& pool) {
    if (texSize == 0 || !world || !world->grid) return;
    if (world->data.elevation.empty()) return;

    pendingWorld = std::move(world);
    pendingMode = mode;
    taskPool = &pool;

    if (baking) {
        // A bake is running; mark dirty so exactly one fresh bake follows it.
        dirty = true;
        return;
    }
    scheduleBake();
}

void PlanetColorizer::scheduleBake() {
    baking = true;
    dirty = false;

    auto world = pendingWorld;
    ColorMode mode = pendingMode;
    uint32_t ts = texSize;
    uint32_t n = subdivision;
    foundation::TaskPool& pool = *taskPool;

    inFlight = std::make_shared<BakeResult>();
    auto result = inFlight;

    bakeFuture = std::async(std::launch::async,
        [result, world, mode, ts, n, &pool]() {
            bakeInto(*result, ts, n, *world, mode, pool);
        });
}

bool PlanetColorizer::uploadPending() {
    if (texSize == 0) return false;

    // Reap a finished bake.
    if (baking && inFlight && bakeFuture.valid() &&
        bakeFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        bakeFuture.get();
        ready = inFlight;
        inFlight.reset();
        uploadCursor = 0;
        baking = false;
    }

    if (!ready) {
        // No buffers waiting — if a rebake was requested mid-bake, kick it now.
        if (!baking && dirty) scheduleBake();
        return false;
    }

    // Upload a couple of rhombi per frame, regenerating mips for each. A single
    // 2048^2 glGenerateMipmap is a few ms, so drop to one rhombus/frame at the
    // largest sizes to avoid a frame spike on a full re-bake (e.g. mode cycle).
    const int uploadsPerFrame = (texSize >= 1536) ? 1 : 2;
    int uploaded = 0;
    while (uploadCursor < 10 && uploaded < uploadsPerFrame) {
        uint32_t r = static_cast<uint32_t>(uploadCursor);
        glBindTexture(GL_TEXTURE_2D, textures[r]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        static_cast<GLsizei>(texSize), static_cast<GLsizei>(texSize),
                        GL_RGBA, GL_UNSIGNED_BYTE, ready->rhombi[r].data());
        glGenerateMipmap(GL_TEXTURE_2D);
        ++uploadCursor;
        ++uploaded;
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (uploadCursor >= 10) {
        ready.reset();
        contentReady = true;
        // If a snapshot/mode change arrived during this bake, run one more.
        if (!baking && dirty) scheduleBake();
    }
    return uploaded > 0;
}

void PlanetColorizer::bind(uint32_t rhombus, GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textures[rhombus]);
}

} // namespace planetview
