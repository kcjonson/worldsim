#pragma once

#include <cstdint>
#include <vector>

namespace worldgen {

// Identifies a single tile by rhombus index and local grid coordinates.
struct TileId {
    uint32_t rhombus{0};
    uint32_t i{0}; // column within rhombus
    uint32_t j{0}; // row within rhombus
};

// Per-tile data fields produced by the planet generator.
// Fields are stored flat: index = rhombus * (n*n) + j * n + i
// where n = subdivision level of the grid.
struct TileData {
    float elevation{0.0F};     // metres above sea level (negative = below)
    float temperature{0.0F};   // degrees Celsius
    float precipitation{0.0F}; // mm/year
    float moisture{0.0F};      // 0-1 soil moisture
    uint8_t biome{0};          // BiomeId (see GeneratedWorld)
    uint8_t plate{0};          // tectonic plate index
    bool isOcean{false};
    bool hasSnow{false};
};

// Aggregate world data: 10 rhombi, each with n x n tiles.
struct WorldData {
    uint32_t subdivision{0}; // n: tiles per rhombus edge
    float seaLevel{0.0F};    // elevation threshold for ocean
    float radius{6371000.0F}; // planet radius in metres

    // Flat storage: [rhombus][j][i], stride = subdivision^2 per rhombus.
    // Total size = 10 * subdivision * subdivision.
    std::vector<TileData> tiles;

    // Returns true if the world has been generated (non-empty).
    bool isValid() const { return subdivision > 0 && !tiles.empty(); }

    // Access tile by TileId.
    const TileData& tile(uint32_t rhombus, uint32_t i, uint32_t j) const {
        return tiles[rhombus * subdivision * subdivision + j * subdivision + i];
    }

    TileData& tile(uint32_t rhombus, uint32_t i, uint32_t j) {
        return tiles[rhombus * subdivision * subdivision + j * subdivision + i];
    }
};

} // namespace worldgen
