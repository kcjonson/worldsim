#pragma once

// ChunkSampleResult - Biome data sampled from the 3D world for a chunk.
// Used temporarily during Chunk::generate(); tile data is stored in a flat array.

#include "world/Biome.h"
#include "world/BiomeWeights.h"
#include "world/chunk/ChunkCoordinate.h"

#include <array>
#include <cstdint>

namespace engine::world {

inline constexpr int32_t kSectorGridSize = 32;

struct ChunkSampleResult {
    std::array<BiomeWeights, 4> cornerBiomes{};
    std::array<float, 4>        cornerElevations{};
    std::array<BiomeWeights, kSectorGridSize * kSectorGridSize> sectorGrid{};

    void computeSectorGrid() {
        for (int32_t sy = 0; sy < kSectorGridSize; ++sy) {
            for (int32_t sx = 0; sx < kSectorGridSize; ++sx) {
                float u = static_cast<float>(sx) / static_cast<float>(kSectorGridSize - 1);
                float v = static_cast<float>(sy) / static_cast<float>(kSectorGridSize - 1);
                sectorGrid[static_cast<size_t>(sy * kSectorGridSize + sx)] = bilinearInterpolate(u, v);
            }
        }
    }

    [[nodiscard]] BiomeWeights getTileBiome(uint16_t localX, uint16_t localY) const {
        int32_t sectorX = std::min(static_cast<int32_t>(localX / 16), kSectorGridSize - 1);
        int32_t sectorY = std::min(static_cast<int32_t>(localY / 16), kSectorGridSize - 1);
        return sectorGrid[static_cast<size_t>(sectorY * kSectorGridSize + sectorX)];
    }

    [[nodiscard]] float getTileElevation(uint16_t localX, uint16_t localY) const {
        float u = static_cast<float>(localX) / static_cast<float>(kChunkSize - 1);
        float v = static_cast<float>(localY) / static_cast<float>(kChunkSize - 1);
        float top    = cornerElevations[0] * (1.0F - u) + cornerElevations[1] * u;
        float bottom = cornerElevations[2] * (1.0F - u) + cornerElevations[3] * u;
        return top * (1.0F - v) + bottom * v;
    }

  private:
    // Bilinear interpolation of sparse BiomeWeights.
    // Merges all entries from the four corners and interpolates per unique biome key.
    [[nodiscard]] BiomeWeights bilinearInterpolate(float u, float v) const {
        // Collect the set of biome keys present across all 4 corners.
        uint8_t keys[BiomeWeights::kMaxEntries * 4];
        uint8_t keyCount = 0;

        auto addKey = [&](uint8_t k) {
            for (uint8_t i = 0; i < keyCount; ++i)
                if (keys[i] == k) return;
            if (keyCount < sizeof(keys)) keys[keyCount++] = k;
        };

        for (const auto& bw : cornerBiomes)
            for (uint8_t i = 0; i < bw.count; ++i)
                addKey(bw.entries[i].biome);

        BiomeWeights result;
        for (uint8_t ki = 0; ki < keyCount; ++ki) {
            auto b = static_cast<Biome>(keys[ki]);
            float nw = cornerBiomes[0].get(b);
            float ne = cornerBiomes[1].get(b);
            float sw = cornerBiomes[2].get(b);
            float se = cornerBiomes[3].get(b);
            float top    = nw * (1.0F - u) + ne * u;
            float bottom = sw * (1.0F - u) + se * u;
            float w = top * (1.0F - v) + bottom * v;
            if (w > 0.001F) result.set(b, w);
        }
        result.normalize();
        return result;
    }
};

} // namespace engine::world
