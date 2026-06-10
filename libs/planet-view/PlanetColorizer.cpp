#include "PlanetColorizer.h"

#include <world/generation/GeneratedWorld.h>
#include <algorithm>
#include <cmath>

namespace planetview {

namespace {

struct RGBA { uint8_t r, g, b, a; };

// ── Shared palette helpers (mirrors logic in ColorMaps.h if that exists) ──

RGBA lerpColor(RGBA a, RGBA b, float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    return {
        static_cast<uint8_t>(a.r + (b.r - a.r) * t),
        static_cast<uint8_t>(a.g + (b.g - a.g) * t),
        static_cast<uint8_t>(a.b + (b.b - a.b) * t),
        255
    };
}

RGBA hsvToRgb(float h, float s, float v) {
    h = std::fmod(h, 360.0F);
    float c = v * s;
    float x = c * (1.0F - std::abs(std::fmod(h / 60.0F, 2.0F) - 1.0F));
    float m = v - c;
    float rr, gg, bb;
    if      (h < 60.0F)  { rr = c; gg = x; bb = 0; }
    else if (h < 120.0F) { rr = x; gg = c; bb = 0; }
    else if (h < 180.0F) { rr = 0; gg = c; bb = x; }
    else if (h < 240.0F) { rr = 0; gg = x; bb = c; }
    else if (h < 300.0F) { rr = x; gg = 0; bb = c; }
    else                 { rr = c; gg = 0; bb = x; }
    return {
        static_cast<uint8_t>((rr + m) * 255.0F),
        static_cast<uint8_t>((gg + m) * 255.0F),
        static_cast<uint8_t>((bb + m) * 255.0F),
        255
    };
}

// Hypsometric: deep ocean → ocean → beach → lowland → highland → peak
RGBA terrainColor(float elevation, float seaLevel) {
    float e = elevation - seaLevel;
    if (e < -4000.0F) return {10,  30,  80,  255}; // abyssal
    if (e < -2000.0F) return lerpColor({10, 30, 80, 255}, {30, 80, 150, 255}, (e + 4000.0F) / 2000.0F);
    if (e < 0.0F)     return lerpColor({30, 80, 150, 255}, {80, 160, 210, 255}, (e + 2000.0F) / 2000.0F);
    if (e < 50.0F)    return {230, 210, 170, 255}; // beach
    if (e < 500.0F)   return lerpColor({100, 160, 80, 255}, {80, 130, 60, 255}, e / 500.0F);
    if (e < 2000.0F)  return lerpColor({80, 130, 60, 255}, {140, 130, 110, 255}, (e - 500.0F) / 1500.0F);
    if (e < 3500.0F)  return lerpColor({140, 130, 110, 255}, {200, 200, 200, 255}, (e - 2000.0F) / 1500.0F);
    return {240, 245, 255, 255}; // snow-capped peaks
}

RGBA temperatureColor(float temp) {
    // -30 → 45 C: purple → blue → green → yellow → red
    float t = std::clamp((temp + 30.0F) / 75.0F, 0.0F, 1.0F);
    if (t < 0.25F) return lerpColor({100, 0, 180, 255}, {0, 80, 200, 255}, t * 4.0F);
    if (t < 0.5F)  return lerpColor({0, 80, 200, 255}, {60, 180, 80, 255}, (t - 0.25F) * 4.0F);
    if (t < 0.75F) return lerpColor({60, 180, 80, 255}, {230, 210, 40, 255}, (t - 0.5F) * 4.0F);
    return lerpColor({230, 210, 40, 255}, {210, 40, 20, 255}, (t - 0.75F) * 4.0F);
}

RGBA precipitationColor(float precip) {
    // 0 → 3000 mm/yr: tan → green → blue
    float t = std::clamp(precip / 3000.0F, 0.0F, 1.0F);
    if (t < 0.33F) return lerpColor({200, 180, 130, 255}, {130, 180, 90, 255}, t / 0.33F);
    if (t < 0.66F) return lerpColor({130, 180, 90, 255}, {40, 140, 60, 255}, (t - 0.33F) / 0.33F);
    return lerpColor({40, 140, 60, 255}, {30, 100, 180, 255}, (t - 0.66F) / 0.34F);
}

static const RGBA kBiomePalette[] = {
    {30,  90,  180, 255}, // Ocean
    {10,  30,   80, 255}, // DeepOcean
    {230, 210, 170, 255}, // Beach
    {210, 180,  90, 255}, // Desert
    {180, 160,  70, 255}, // Savanna
    {90,  150,  60, 255}, // Grassland
    {130, 130,  70, 255}, // Shrubland
    {40,  110,  50, 255}, // TemperateForest
    {20,  100,  40, 255}, // TropicalForest
    {60,  100,  80, 255}, // Taiga
    {180, 200, 200, 255}, // Tundra
    {240, 245, 255, 255}, // Ice
    {140, 130, 120, 255}, // Mountain
};

RGBA biomeColor(uint8_t biome) {
    if (biome >= 13) return {128, 128, 128, 255};
    return kBiomePalette[biome];
}

RGBA plateColor(uint8_t plate) {
    float hue = static_cast<float>(plate) / 12.0F * 360.0F;
    return hsvToRgb(hue, 0.7F, 0.85F);
}

RGBA combinedColor(const worldgen::TileData& td, float seaLevel) {
    RGBA base = biomeColor(td.biome);
    // Ocean depth shading
    if (td.isOcean) {
        float depth = std::clamp((seaLevel - td.elevation) / 4000.0F, 0.0F, 1.0F);
        base = lerpColor(base, {10, 30, 80, 255}, depth * 0.7F);
    }
    // Snow whitening
    if (td.hasSnow) {
        base = lerpColor(base, {240, 245, 255, 255}, 0.6F);
    }
    return base;
}

RGBA neutralGray() { return {128, 128, 128, 255}; }

RGBA colorForMode(const worldgen::TileData& td, ColorMode mode,
                  const worldgen::ValidFields& valid, float seaLevel) {
    switch (mode) {
        case ColorMode::Terrain:
            return valid.elevation ? terrainColor(td.elevation, seaLevel) : neutralGray();
        case ColorMode::Temperature:
            return valid.temperature ? temperatureColor(td.temperature) : neutralGray();
        case ColorMode::Precipitation:
            return valid.precipitation ? precipitationColor(td.precipitation) : neutralGray();
        case ColorMode::Biome:
            return valid.biome ? biomeColor(td.biome) : neutralGray();
        case ColorMode::Plates:
            return valid.plates ? plateColor(td.plate) : neutralGray();
        case ColorMode::Snow:
            if (!valid.snow) return neutralGray();
            return td.hasSnow ? RGBA{240, 245, 255, 255} : RGBA{60, 100, 50, 255};
        case ColorMode::Combined:
            return (valid.biome && valid.elevation) ? combinedColor(td, seaLevel) : neutralGray();
        default:
            return neutralGray();
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
        default:                       return "Unknown";
    }
}

PlanetColorizer::~PlanetColorizer() { release(); }

void PlanetColorizer::release() {
    for (auto& t : textures) {
        if (t) { glDeleteTextures(1, &t); t = 0; }
    }
    texSize = 0;
}

void PlanetColorizer::init(uint32_t subdivision) {
    release();
    texSize = std::min(subdivision, 2048U);
    glGenTextures(10, textures);
    for (uint32_t r = 0; r < 10U; ++r) {
        glBindTexture(GL_TEXTURE_2D, textures[r]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Allocate with neutral gray until update() is called.
        std::vector<RGBA> buf(static_cast<size_t>(texSize) * texSize, neutralGray());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(texSize), static_cast<GLsizei>(texSize),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PlanetColorizer::update(const worldgen::GeneratedWorld& world, ColorMode mode) {
    if (texSize == 0) return;
    uint32_t n = world.data.subdivision;
    if (n == 0) return;

    std::vector<RGBA> buf(static_cast<size_t>(texSize) * texSize);

    for (uint32_t r = 0; r < 10U; ++r) {
        for (uint32_t j = 0; j < texSize; ++j) {
            for (uint32_t i = 0; i < texSize; ++i) {
                // Map texel (i,j) → tile (ti, tj) in the world data.
                uint32_t ti = std::min(static_cast<uint32_t>(static_cast<float>(i) / texSize * n), n - 1U);
                uint32_t tj = std::min(static_cast<uint32_t>(static_cast<float>(j) / texSize * n), n - 1U);
                const worldgen::TileData& td = world.data.tile(r, ti, tj);
                buf[j * texSize + i] = colorForMode(td, mode, world.valid, world.data.seaLevel);
            }
        }
        glBindTexture(GL_TEXTURE_2D, textures[r]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        static_cast<GLsizei>(texSize), static_cast<GLsizei>(texSize),
                        GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PlanetColorizer::bind(uint32_t rhombus, GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textures[rhombus]);
}

} // namespace planetview
