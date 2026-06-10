#include "PlanetColorizer.h"

#include <world/worldgen/data/GeneratedWorld.h>
#include <world/worldgen/data/WorldData.h>
#include <world/worldgen/debug/ColorMaps.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace planetview {

namespace {

struct RGBA { uint8_t r, g, b, a; };

// Convert a worldgen Rgb to our internal RGBA.
RGBA toRGBA(worldgen::Rgb c) { return {c.r, c.g, c.b, 255}; }

RGBA neutralGray() { return {128, 128, 128, 255}; }

// Check whether a WorldField bit is set in validFields.
bool hasField(uint32_t validFields, worldgen::WorldField f) {
    return (validFields & static_cast<uint32_t>(f)) != 0;
}

RGBA colorForTile(uint32_t tileId, ColorMode mode,
                  const worldgen::WorldData& data, uint32_t validFields,
                  float seaLevelMeters) {
    switch (mode) {
        case ColorMode::Terrain: {
            if (!hasField(validFields, worldgen::WorldField::Elevation))
                return neutralGray();
            return toRGBA(worldgen::elevationColor(data.elevation[tileId], seaLevelMeters));
        }
        case ColorMode::Temperature: {
            if (!hasField(validFields, worldgen::WorldField::TemperatureMean))
                return neutralGray();
            float tempC = static_cast<float>(data.temperatureMean[tileId]) * 0.1f;
            return toRGBA(worldgen::temperatureColor(tempC));
        }
        case ColorMode::Precipitation: {
            if (!hasField(validFields, worldgen::WorldField::Precipitation))
                return neutralGray();
            float precip = static_cast<float>(data.precipitation[tileId]);
            return toRGBA(worldgen::precipitationColor(precip));
        }
        case ColorMode::Biome: {
            if (!hasField(validFields, worldgen::WorldField::Biome))
                return neutralGray();
            uint8_t b = data.biome[tileId];
            if (b >= static_cast<uint8_t>(worldgen::Biome::Count))
                return neutralGray();
            auto c = worldgen::kBiomeColors[b];
            return {c.r, c.g, c.b, 255};
        }
        case ColorMode::Plates: {
            if (!hasField(validFields, worldgen::WorldField::PlateId))
                return neutralGray();
            uint8_t plateId = data.plateId[tileId];
            if (plateId == 255) return neutralGray();
            RGBA base = toRGBA(worldgen::plateColor(plateId));
            // Boundary emphasis: darken tiles near plate boundaries.
            if (hasField(validFields, worldgen::WorldField::BoundaryDistance)) {
                uint16_t dist = data.boundaryDistance[tileId];
                if (dist <= 2) {
                    // Within 2 tiles of a boundary — darken by 40%.
                    base.r = static_cast<uint8_t>(base.r * 0.6f);
                    base.g = static_cast<uint8_t>(base.g * 0.6f);
                    base.b = static_cast<uint8_t>(base.b * 0.6f);
                }
            }
            return base;
        }
        case ColorMode::Snow: {
            if (!hasField(validFields, worldgen::WorldField::SnowCover))
                return neutralGray();
            uint8_t snow = data.snowCover[tileId];
            if (snow == 0) return {60, 100, 50, 255};
            // Blend from bare ground to full snow.
            float t = static_cast<float>(snow) / 255.0f;
            auto r = static_cast<uint8_t>(60 + (240 - 60) * t);
            auto g = static_cast<uint8_t>(100 + (245 - 100) * t);
            auto b = static_cast<uint8_t>(50 + (255 - 50) * t);
            return {r, g, b, 255};
        }
        case ColorMode::Combined: {
            // Biome base + ocean depth + snow whitening.
            if (!hasField(validFields, worldgen::WorldField::Biome))
                return neutralGray();
            uint8_t biomeIdx = data.biome[tileId];
            if (biomeIdx >= static_cast<uint8_t>(worldgen::Biome::Count))
                return neutralGray();
            auto bc = worldgen::kBiomeColors[biomeIdx];
            RGBA base = {bc.r, bc.g, bc.b, 255};

            // Ocean depth shading.
            bool isOcean = (biomeIdx == static_cast<uint8_t>(worldgen::Biome::Ocean) ||
                            biomeIdx == static_cast<uint8_t>(worldgen::Biome::Lake));
            if (isOcean && hasField(validFields, worldgen::WorldField::Elevation)) {
                float elev = data.elevation[tileId];
                float depth = seaLevelMeters - elev;
                float t = std::clamp(depth / 4000.0f, 0.0f, 1.0f) * 0.7f;
                base.r = static_cast<uint8_t>(base.r * (1.0f - t) + 10 * t);
                base.g = static_cast<uint8_t>(base.g * (1.0f - t) + 30 * t);
                base.b = static_cast<uint8_t>(base.b * (1.0f - t) + 80 * t);
            }

            // Snow whitening.
            if (hasField(validFields, worldgen::WorldField::SnowCover)) {
                uint8_t snow = data.snowCover[tileId];
                if (snow > 0) {
                    float t = static_cast<float>(snow) / 255.0f * 0.6f;
                    base.r = static_cast<uint8_t>(base.r * (1.0f - t) + 240 * t);
                    base.g = static_cast<uint8_t>(base.g * (1.0f - t) + 245 * t);
                    base.b = static_cast<uint8_t>(base.b * (1.0f - t) + 255 * t);
                }
            }
            return base;
        }
        default:
            return neutralGray();
    }
}

} // anonymous namespace

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
    std::vector<RGBA> buf(static_cast<size_t>(texSize) * texSize, neutralGray());
    for (uint32_t r = 0; r < 10U; ++r) {
        glBindTexture(GL_TEXTURE_2D, textures[r]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                     static_cast<GLsizei>(texSize), static_cast<GLsizei>(texSize),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PlanetColorizer::update(const worldgen::GeneratedWorld& world, ColorMode mode) {
    if (texSize == 0) return;
    const worldgen::WorldData& data = world.data;
    if (data.elevation.empty()) return; // nothing allocated yet

    // n = grid subdivision; TileId = r*n*n + j*n + i
    uint32_t n = world.grid ? world.grid->subdivision() : 0;
    if (n == 0) return;

    std::vector<RGBA> buf(static_cast<size_t>(texSize) * texSize);

    for (uint32_t r = 0; r < 10U; ++r) {
        for (uint32_t j = 0; j < texSize; ++j) {
            for (uint32_t i = 0; i < texSize; ++i) {
                // Map texel (i,j) → tile indices within rhombus r.
                uint32_t ti = std::min(static_cast<uint32_t>(
                    static_cast<float>(i) / static_cast<float>(texSize) * static_cast<float>(n)), n - 1U);
                uint32_t tj = std::min(static_cast<uint32_t>(
                    static_cast<float>(j) / static_cast<float>(texSize) * static_cast<float>(n)), n - 1U);
                // TileId encoding from SphereGrid: r*n*n + j*n + i
                uint32_t tileId = r * n * n + tj * n + ti;
                buf[j * texSize + i] = colorForTile(tileId, mode, data,
                                                    world.validFields,
                                                    world.seaLevelMeters);
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
