#include "PlanetGenerator.h"

#include <cmath>
#include <random>

namespace worldgen {

namespace {

// Simple hash-based noise — no external deps.
float hash3(float x, float y, float z) {
    int ix = static_cast<int>(x * 1000.0F);
    int iy = static_cast<int>(y * 1000.0F);
    int iz = static_cast<int>(z * 1000.0F);
    uint32_t h = static_cast<uint32_t>(ix * 1619 + iy * 31337 + iz * 6971);
    h ^= h >> 13;
    h *= 0x9e3779b9U;
    h ^= h >> 11;
    return static_cast<float>(h & 0xFFFFU) / 65535.0F;
}

// Fractal noise on unit sphere position.
float fbm(float x, float y, float z, int octaves) {
    float val = 0.0F;
    float amp = 1.0F;
    float freq = 1.0F;
    float maxAmp = 0.0F;
    for (int o = 0; o < octaves; ++o) {
        val += hash3(x * freq + static_cast<float>(o) * 3.7F,
                     y * freq + static_cast<float>(o) * 2.1F,
                     z * freq + static_cast<float>(o) * 1.5F) * amp;
        maxAmp += amp;
        amp *= 0.5F;
        freq *= 2.1F;
    }
    return val / maxAmp;
}

// Map a (rhombus, i, j) to a unit-sphere direction.
// Rhombi 0-9 of the HEALPix-like diamond grid.
// For the stub we approximate with equal-area mapping per rhombus.
// 10 rhombi cover the sphere in 2 bands of 4 equatorial + 2 polar caps
// (simplified — not true HEALPix, but visually convincing for M3f).
struct Vec3 { float x, y, z; };

Vec3 rhombusPoint(uint32_t rhombus, float u, float v) {
    // Map (rhombus, u, v) to lat/lon then to unit sphere.
    // 10 rhombi: 4 north-polar, 2 equatorial rows of 4, 2 south-polar
    // Simplified: tile the sphere in rows.
    const float pi = 3.14159265358979F;
    float lon{0.0F}, lat{0.0F};

    if (rhombus < 4) {
        // North polar cap: lat 90->45
        float baseLon = static_cast<float>(rhombus) * 0.5F * pi;
        lon = baseLon + u * 0.5F * pi;
        lat = (0.5F - v * 0.5F) * pi * 0.5F + 0.25F * pi; // 45-90
        lon += v * 0.25F * pi; // skew
    } else if (rhombus < 8) {
        // Equatorial belt: lat 45->-45
        float baseLon = static_cast<float>(rhombus - 4) * 0.5F * pi;
        lon = baseLon + u * 0.5F * pi;
        lat = (1.0F - v) * pi * 0.5F - 0.25F * pi; // -45 to 45
    } else {
        // South polar cap: lat -45->-90
        float baseLon = static_cast<float>(rhombus - 8) * pi;
        lon = baseLon + u * pi;
        lat = -(0.5F * pi * 0.5F + v * 0.25F * pi); // -45 to -90
    }

    float cosLat = std::cos(lat);
    return {cosLat * std::cos(lon), cosLat * std::sin(lon), std::sin(lat)};
}

uint8_t classifyBiome(float elevation, float temp, float precip, bool isOcean, bool hasSnow, float seaLevel) {
    if (isOcean) {
        return (elevation < seaLevel - 2000.0F) ? Biome::DeepOcean : Biome::Ocean;
    }
    if (hasSnow || temp < -10.0F) {
        return (elevation > 3000.0F) ? Biome::Ice : Biome::Tundra;
    }
    if (elevation > 3500.0F) return Biome::Mountain;
    if (elevation < 20.0F)   return Biome::Beach;
    if (temp > 20.0F) {
        if (precip < 250.0F) return Biome::Desert;
        if (precip < 500.0F) return Biome::Savanna;
        return (precip > 1500.0F) ? Biome::TropicalForest : Biome::Grassland;
    }
    if (temp > 5.0F) {
        if (precip < 300.0F) return Biome::Shrubland;
        return (precip > 800.0F) ? Biome::TemperateForest : Biome::Grassland;
    }
    return Biome::Taiga;
}

} // namespace

GeneratedWorld PlanetGenerator::generate(const PlanetGeneratorParams& params, ProgressCallback progress) {
    GeneratedWorld world;
    world.generatorName = "PlanetGeneratorStub";
    world.seed = params.seed;

    WorldData& data = world.data;
    data.subdivision = params.subdivision;
    data.seaLevel = params.seaLevel;
    data.radius = params.radius;

    const uint32_t n = params.subdivision;
    const uint32_t totalTiles = 10U * n * n;
    data.tiles.resize(totalTiles);

    const float seedOffset = static_cast<float>(params.seed) * 0.137F;

    for (uint32_t r = 0; r < 10U; ++r) {
        for (uint32_t j = 0; j < n; ++j) {
            for (uint32_t i = 0; i < n; ++i) {
                float u = (static_cast<float>(i) + 0.5F) / static_cast<float>(n);
                float v = (static_cast<float>(j) + 0.5F) / static_cast<float>(n);

                Vec3 p = rhombusPoint(r, u, v);
                float px = p.x + seedOffset;
                float py = p.y + seedOffset * 0.7F;
                float pz = p.z + seedOffset * 1.3F;

                float continent = fbm(px, py, pz, 6);
                // Elevate continents, lower ocean floor
                float elevation = (continent - 0.5F) * 8000.0F;

                float temperature = 25.0F * (1.0F - std::abs(p.z)) - elevation * 0.006F
                                  + fbm(px * 1.5F, py * 1.5F, pz * 1.5F, 3) * 8.0F - 4.0F;

                float precipitation = 600.0F * fbm(px * 2.0F + 10.0F, py * 2.0F, pz * 2.0F, 4)
                                    + 200.0F * (1.0F - std::abs(p.z));

                bool isOcean = elevation < params.seaLevel;
                bool hasSnow = temperature < -5.0F && !isOcean;
                float adjustedElev = isOcean ? elevation : elevation + 0.0F;

                uint8_t plate = static_cast<uint8_t>(static_cast<int>(fbm(px * 0.3F, py * 0.3F, pz * 0.3F, 2) * 12.0F) % 12);

                TileData& td = data.tile(r, i, j);
                td.elevation    = adjustedElev;
                td.temperature  = temperature;
                td.precipitation = precipitation;
                td.moisture     = std::min(1.0F, precipitation / 1200.0F);
                td.isOcean      = isOcean;
                td.hasSnow      = hasSnow;
                td.plate        = plate;
                td.biome        = classifyBiome(elevation, temperature, precipitation, isOcean, hasSnow, params.seaLevel);
            }
        }

        if (progress) {
            progress(static_cast<float>(r + 1) / 10.0F, "Generating planet...");
        }
    }

    world.valid.elevation     = true;
    world.valid.temperature   = true;
    world.valid.precipitation = true;
    world.valid.biome         = true;
    world.valid.plates        = true;
    world.valid.snow          = true;

    return world;
}

} // namespace worldgen
