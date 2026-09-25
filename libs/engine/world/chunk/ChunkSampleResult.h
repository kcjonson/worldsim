#pragma once

// ChunkSampleResult - Biome data sampled from the 3D world for a chunk.
// Used temporarily during Chunk::generate(); tile data is stored in a flat array.

#include "world/Biome.h"
#include "world/BiomeWeights.h"
#include "world/chunk/ChunkCoordinate.h"

#include <worldgen/sampling/PondNetwork2D.h>
#include <worldgen/sampling/RiverNetwork2D.h>

#include <array>
#include <cstdint>
#include <vector>

namespace engine::world {

inline constexpr int32_t kSectorGridSize = 32;

// Corner lattice of the chunk's 3x3 neighborhood: the 4x4 grid of corner points
// shared by this chunk and its 8 neighbors (each unit cell is one chunk width).
// Lattice index (li, lj), li/lj in [0, kNeighborhoodLatticeSize), sits at
// chunk-corner-grid position (coord.x - 1 + li, coord.y - 1 + lj). A neighbor
// chunk at offset (dx, dy) in [-1, 1] reads its own 4 corners at lattice
// (dx+1, dy+1) .. (dx+2, dy+2). See neighborCornerBiomes/neighborCornerElevations.
inline constexpr int32_t kNeighborhoodLatticeSize = 4;

struct ChunkSampleResult {
    std::array<BiomeWeights, 4> cornerBiomes{};
    std::array<float, 4>        cornerElevations{};
    std::array<BiomeWeights, kSectorGridSize * kSectorGridSize> sectorGrid{};

    // Corner biome/elevation samples for the chunk's full 3x3 neighborhood, stored
    // once as the 4x4 lattice of corners the neighborhood shares (see
    // kNeighborhoodLatticeSize above). Filled by the world sampler through the same
    // per-position formula each neighbor chunk uses for its own corners, so a
    // neighbor's sector grid rebuilt from this lattice is bit-identical to the one
    // that neighbor computes for itself (terrain-polygons-architecture.md D4/D14).
    // Used by ApronField to build apron tiles without waiting on neighbor chunks.
    std::array<BiomeWeights, kNeighborhoodLatticeSize * kNeighborhoodLatticeSize> neighborhoodCornerBiomes{};
    std::array<float, kNeighborhoodLatticeSize * kNeighborhoodLatticeSize>        neighborhoodCornerElevations{};

    // The 4 corners (NW, NE, SW, SE, matching ChunkCorner's order) of the neighbor
    // chunk at offset (dx, dy) from this one, dx/dy in [-1, 1], as that neighbor's
    // own ChunkCoordinate::corner() samples would read.
    [[nodiscard]] std::array<BiomeWeights, 4> neighborCornerBiomes(int32_t dx, int32_t dy) const {
        const int32_t li = dx + 1;
        const int32_t lj = dy + 1;
        return {
            neighborhoodCornerBiomes[static_cast<size_t>(lj * kNeighborhoodLatticeSize + li)],
            neighborhoodCornerBiomes[static_cast<size_t>(lj * kNeighborhoodLatticeSize + li + 1)],
            neighborhoodCornerBiomes[static_cast<size_t>((lj + 1) * kNeighborhoodLatticeSize + li)],
            neighborhoodCornerBiomes[static_cast<size_t>((lj + 1) * kNeighborhoodLatticeSize + li + 1)],
        };
    }

    [[nodiscard]] std::array<float, 4> neighborCornerElevations(int32_t dx, int32_t dy) const {
        const int32_t li = dx + 1;
        const int32_t lj = dy + 1;
        return {
            neighborhoodCornerElevations[static_cast<size_t>(lj * kNeighborhoodLatticeSize + li)],
            neighborhoodCornerElevations[static_cast<size_t>(lj * kNeighborhoodLatticeSize + li + 1)],
            neighborhoodCornerElevations[static_cast<size_t>((lj + 1) * kNeighborhoodLatticeSize + li)],
            neighborhoodCornerElevations[static_cast<size_t>((lj + 1) * kNeighborhoodLatticeSize + li + 1)],
        };
    }

    // River channel segments (2D world meters) whose footprint touches this
    // chunk, synthesized from the coarse 3D drainage graph by RiverNetwork2D.
    // Empty for the vast majority of chunks. Consumed per tile by riverHalfWidthAt().
    std::vector<worldgen::RiverNetwork2D::Segment> riverSegments;

    // Channel half-width (meters) covering (worldXMeters, worldYMeters), or 0 if
    // the point is outside every gathered channel. The widest covering channel
    // wins (so confluences read as the larger river). Width drives both the water
    // override and its rendered depth. Linear scan with a cheap AABB reject before
    // the distance/sqrt, so the many short feeder segments stay affordable per tile.
    [[nodiscard]] float riverHalfWidthAt(double worldXMeters, double worldYMeters) const {
        float best = 0.0f;
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
            const float halfWidth =
                static_cast<float>(static_cast<double>(s.halfWidth0) +
                                   (static_cast<double>(s.halfWidth1) - static_cast<double>(s.halfWidth0)) * t);
            if (ex * ex + ey * ey <= static_cast<double>(halfWidth) * static_cast<double>(halfWidth) &&
                halfWidth > best) {
                best = halfWidth;
            }
        }
        return best;
    }

    // Sparse standing-water ponds whose footprint touches this chunk, from
    // PondNetwork2D. Empty for most chunks. Consumed per tile by pondDepthAt().
    std::vector<worldgen::PondNetwork2D::Pond> pondBlobs;

    // Cosmetic water-depth byte at (worldXMeters, worldYMeters) if inside a pond,
    // else 0 (deepest covering pond wins). Each Pond::sampleDepth AABB-rejects
    // before any trig, so the handful of ponds stays affordable per tile.
    [[nodiscard]] uint8_t pondDepthAt(double worldXMeters, double worldYMeters) const {
        uint8_t best = 0;
        for (const auto& p : pondBlobs) {
            best = std::max(best, worldgen::PondNetwork2D::sampleDepth(p, worldXMeters, worldYMeters));
        }
        return best;
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

// Fill `result`'s neighborhood corner lattice (kNeighborhoodLatticeSize^2 shared
// corners of the chunk's 3x3 neighborhood) by calling the given per-world-position
// biome/elevation functions at each lattice point. Both MockWorldSampler and
// GeneratedWorldSampler have their own private per-position sampling methods, so
// this stays a template over callables rather than an IWorldSampler method; it is
// the one place the lattice-index-to-world-position math is spelled out (D14: one
// path for every sampler).
template <typename BiomeAtFn, typename ElevAtFn>
void fillNeighborhoodCorners(ChunkSampleResult& result, ChunkCoordinate coord, BiomeAtFn&& biomeAt, ElevAtFn&& elevAt) {
    for (int32_t lj = 0; lj < kNeighborhoodLatticeSize; ++lj) {
        for (int32_t li = 0; li < kNeighborhoodLatticeSize; ++li) {
            const WorldPosition pos{
                static_cast<float>(coord.x - 1 + li) * kChunkWorldSize,
                static_cast<float>(coord.y - 1 + lj) * kChunkWorldSize
            };
            const size_t idx = static_cast<size_t>(lj * kNeighborhoodLatticeSize + li);
            result.neighborhoodCornerBiomes[idx] = biomeAt(pos);
            result.neighborhoodCornerElevations[idx] = elevAt(pos);
        }
    }
}

// Build a ChunkSampleResult whose own corners and full 3x3 neighborhood are all
// the same biome/elevation, with the sector grid computed. For callers that
// hand-build sample data without a real IWorldSampler (tests): without this, the
// neighborhood corner lattice would stay default-constructed (empty BiomeWeights),
// which is a safe-but-meaningless fallback for anything built from it, such as an
// ApronField's apron tiles.
[[nodiscard]] inline ChunkSampleResult makeUniformChunkSampleResult(const BiomeWeights& biome, float elevationMeters) {
    ChunkSampleResult result;
    result.cornerBiomes.fill(biome);
    result.cornerElevations.fill(elevationMeters);
    result.neighborhoodCornerBiomes.fill(biome);
    result.neighborhoodCornerElevations.fill(elevationMeters);
    result.computeSectorGrid();
    return result;
}

} // namespace engine::world
