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
constexpr uint32_t kBaseMax = 1024; // base-tier cap (mips cover smaller detail)
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
        default:                       return "Unknown";
    }
}

PlanetColorizer::~PlanetColorizer() {
    if (bakeFuture.valid()) bakeFuture.wait();
    release();
}

void PlanetColorizer::release() {
    for (auto& t : textures) {
        if (t) { glDeleteTextures(1, &t); t = 0; }
    }
    texSize = 0;
    contentReady = false;
    ready.reset();
    inFlight.reset();
    uploadCursor = 0;
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

// Bake one rhombus's base texture. Texel (i,j) maps to an owned chart vertex via
// canonicalTile, so seam/pole vertices resolve to the same tile the CPU assigns.
// For n > texSize the texel->vertex map downsamples (nearest); the mip chain then
// answers the still-coarser zoomed-out views.
void PlanetColorizer::bakeInto(BakeResult& out, uint32_t texSize, uint32_t n,
                               const worldgen::GeneratedWorld& world, ColorMode mode,
                               foundation::TaskPool& pool) {
    const worldgen::SphereGrid& grid = *world.grid;
    for (uint32_t r = 0; r < 10U; ++r) {
        auto& dst = out.rhombi[r];
        dst.resize(static_cast<size_t>(texSize) * texSize * 4);
        pool.parallelFor(0, texSize, 16, [&](size_t jb, size_t je) {
            for (size_t j = jb; j < je; ++j) {
                for (uint32_t i = 0; i < texSize; ++i) {
                    uint32_t ti = (texSize > 1) ? (i * (n - 1U)) / (texSize - 1U) : 0U;
                    uint32_t tj = (texSize > 1)
                                      ? (static_cast<uint32_t>(j) * (n - 1U)) / (texSize - 1U)
                                      : 0U;
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
        });
    }
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

    // Upload up to 2 rhombi per frame, regenerating mips for each.
    int uploaded = 0;
    while (uploadCursor < 10 && uploaded < 2) {
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
