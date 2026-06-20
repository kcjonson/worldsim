#pragma once

#include <GL/glew.h>
#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <vector>

namespace worldgen {
struct GeneratedWorld;
}

namespace foundation {
class TaskPool;
}

namespace planetview {

// Visualization modes.
enum class ColorMode : int {
    Terrain      = 0, // Hypsometric elevation + sea level
    Temperature  = 1,
    Precipitation= 2,
    Biome        = 3, // Palette per biome type
    Plates       = 4, // Distinct hue per plate + boundary darkening
    Snow         = 5,
    Combined     = 6, // Biome + snow whitening + ocean depth shading
    Hydrology    = 7, // Drainage network: rivers bright blue, lakes cyan, land tinted by log(flowAccum)
    Ice          = 8, // Cryosphere: sea-ice + glacier thickness (analyst view)
    Count        = 9
};

const char* colorModeName(ColorMode m);

// Base tier of the two-tier hex renderer: 10 per-rhombus RGBA8 textures at
// baseSize = min(n, 1024), with a full mip chain. Answers "zoomed out" — tiles
// are sub-pixel and the mipmapped texture is the correct (shimmer-free) result.
//
// Baking is split so a high-n grid never hitches the render thread:
//   bake(world, mode) runs on a TaskPool worker, producing CPU buffers for all
//   10 rhombi; uploadPending() pushes <= 2 rhombi/frame to GL (+ mip regen).
// Coalescing: a snapshot arriving mid-bake sets a dirty flag; on completion, if
// dirty, exactly one fresh bake is scheduled — never more than one queued.
class PlanetColorizer {
  public:
    PlanetColorizer() = default;
    ~PlanetColorizer();

    PlanetColorizer(const PlanetColorizer&) = delete;
    PlanetColorizer& operator=(const PlanetColorizer&) = delete;

    // Allocate the 10 textures sized baseSize = min(subdivision, 1024).
    // Must be called once after the GL context is available.
    void init(uint32_t subdivision);

    // Request a (re)bake of all 10 textures for the given world and mode. Runs
    // async on `pool`; results upload over subsequent uploadPending() calls.
    void requestBake(std::shared_ptr<const worldgen::GeneratedWorld> world,
                     ColorMode mode, foundation::TaskPool& pool);

    // Pump finished bakes to GL (<= 2 rhombi per call). Call once per frame on
    // the render thread. Returns true if any upload happened.
    bool uploadPending();

    bool isReady() const { return textures[0] != 0; }

    // True once at least one full bake has reached the GPU (all 10 rhombi).
    bool hasContent() const { return contentReady; }

    uint32_t baseSize() const { return texSize; }

    // Bind rhombus r's texture to the given texture unit.
    void bind(uint32_t rhombus, GLuint unit) const;

    // Bake one rhombus's base-tier texels into `out` (texSize*texSize RGBA8),
    // GL-free. Same path the async runtime bake uses. For tests only.
    static void bakeRhombusForTest(std::vector<uint8_t>& out, uint32_t rhombus,
                                   uint32_t texSize, uint32_t n,
                                   const worldgen::GeneratedWorld& world,
                                   ColorMode mode);

  private:
    GLuint textures[10]{};
    uint32_t texSize{0};      // texture dimension (= min(n, 1024))
    uint32_t subdivision{0};  // grid n
    bool contentReady{false};

    // Async bake state.
    struct BakeResult {
        std::array<std::vector<uint8_t>, 10> rhombi; // RGBA8, texSize*texSize each
    };
    std::shared_ptr<BakeResult> ready;   // completed buffers awaiting upload
    int uploadCursor{0};                 // next rhombus index to upload

    // Pending-rebake bookkeeping (single-queue coalescing).
    std::shared_ptr<const worldgen::GeneratedWorld> pendingWorld;
    ColorMode pendingMode{ColorMode::Terrain};
    bool baking{false};
    bool dirty{false};

    // Shared bake handoff (worker writes, render thread reads).
    std::shared_ptr<BakeResult> inFlight;
    std::future<void> bakeFuture;

    foundation::TaskPool* taskPool{nullptr};

    void release();
    void scheduleBake();
    static void bakeInto(BakeResult& out, uint32_t texSize, uint32_t n,
                         const worldgen::GeneratedWorld& world, ColorMode mode,
                         foundation::TaskPool& pool);
};

} // namespace planetview
