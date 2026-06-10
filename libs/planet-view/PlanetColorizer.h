#pragma once

#include <GL/glew.h>
#include <cstdint>
#include <memory>

namespace worldgen {
struct GeneratedWorld;
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
    Count        = 7
};

const char* colorModeName(ColorMode m);

// Owns 10 per-rhombus RGBA8 textures. Texel (i, j) of rhombus r encodes
// a color derived from TileId(r, i, j). Decoupled from mesh — colors are
// sampled by UV in the fragment shader, enabling LOD swaps without recoloring.
class PlanetColorizer {
  public:
    PlanetColorizer() = default;
    ~PlanetColorizer();

    PlanetColorizer(const PlanetColorizer&) = delete;
    PlanetColorizer& operator=(const PlanetColorizer&) = delete;

    // Build textures sized min(subdivision, 2048) x min(subdivision, 2048).
    // Must be called once after GL context is available.
    void init(uint32_t subdivision);

    // Re-bake all 10 textures for the given world and mode.
    void update(const worldgen::GeneratedWorld& world, ColorMode mode);

    bool isReady() const { return textures[0] != 0; }

    // Bind rhombus r's texture to the given texture unit.
    void bind(uint32_t rhombus, GLuint unit) const;

    GLuint textureHandle(uint32_t rhombus) const { return textures[rhombus]; }

  private:
    GLuint textures[10]{};
    uint32_t texSize{0}; // texture dimension

    void release();
};

} // namespace planetview
