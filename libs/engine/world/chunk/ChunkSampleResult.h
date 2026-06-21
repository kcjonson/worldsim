#pragma once

// ChunkSampleResult - Biome data sampled from the 3D world for a chunk.
// Used temporarily during Chunk::generate(); tile data is stored in a flat array.

#include "world/Biome.h"
#include "world/BiomeWeights.h"
#include "world/chunk/ChunkCoordinate.h"

#include <worldgen/sampling/RiverNetwork2D.h>

#include <array>
#include <cstdint>
#include <vector>

namespace engine::world {

inline constexpr int32_t kSectorGridSize = 32;

struct ChunkSampleResult {
    std::array<BiomeWeights, 4> cornerBiomes{};
    std::array<float, 4>        cornerElevations{};
    std::array<BiomeWeights, kSectorGridSize * kSectorGridSize> sectorGrid{};

    // River channel segments (2D world meters) whose footprint touches this
    // chunk, synthesized from the coarse 3D drainage graph by RiverNetwork2D.
    // Empty for the vast majority of chunks. Consumed per tile by isRiverAt().
    std::vector<worldgen::RiverNetwork2D::Segment> riverSegments;

    // True when (worldXMeters, worldYMeters) falls inside a gathered river
    // channel. Linear scan with a cheap AABB reject before the distance/sqrt, so
    // the many short feeder segments stay affordable per tile.
    [[nodiscard]] bool isRiverAt(double worldXMeters, double worldYMeters) const {
        for (const auto& s : riverSegments) {
            const float hwMax = std::max(s.halfWidth0, s.halfWidth1);
            if (worldXMeters < std::min(s.x0, s.x1) - hwMax ||
                worldXMeters > std::max(s.x0, s.x1) + hwMax ||
                worldYMeters < std::min(s.y0, s.y1) - hwMax ||
                worldYMeters > std::max(s.y0, s.y1) + hwMax) {
                continue;
            }
            const double dx = s.x1 - s.x0;
            const double dy = s.y1 - s.y0;
            const double len2 = dx * dx + dy * dy;
            double t = 0.0;
            if (len2 > 0.0) {
                t = ((worldXMeters - s.x0) * dx + (worldYMeters - s.y0) * dy) / len2;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
            }
            const double cx = s.x0 + dx * t;
            const double cy = s.y0 + dy * t;
            const double ex = worldXMeters - cx;
            const double ey = worldYMeters - cy;
            const double halfWidth =
                static_cast<double>(s.halfWidth0) +
                (static_cast<double>(s.halfWidth1) - static_cast<double>(s.halfWidth0)) * t;
            if (ex * ex + ey * ey <= halfWidth * halfWidth) return true;
        }
        return false;
    }

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
