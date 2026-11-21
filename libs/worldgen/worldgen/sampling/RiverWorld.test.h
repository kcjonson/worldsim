#pragma once

// Synthetic worlds for the landing tests: a block of grassland in an ocean, and rivers carved across it.

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_set>

namespace worldgen::test_support {

inline double dotProduct(const Vec3d& first, const Vec3d& second) {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

struct GrasslandBlock {
    uint32_t subdivision{24};
    double landLatDeg{60.0}; // land where |lat| is below this
    double landLonDeg{25.0}; // and |lon| below this
    uint64_t seed{0x1A4D1ULL};
};

// Grassland in the block, ocean elsewhere; a mild, wet climate everywhere (15 C mean, 10 C half-amplitude,
// 900 mm of rain) and every field the landing code reads valid.
inline std::shared_ptr<GeneratedWorld> grasslandWorld(const GrasslandBlock& block) {
    auto world = std::make_shared<GeneratedWorld>();
    world->params.gridSubdivision = block.subdivision;
    world->params.seed = block.seed;
    world->derived = derive(world->params);
    world->grid = std::make_shared<SphereGrid>(block.subdivision);
    world->data.allocate(world->grid->tileCount());
    world->seaLevelMeters = 0.0F;
    world->validFields = static_cast<uint32_t>(WorldField::Elevation) | static_cast<uint32_t>(WorldField::Biome) |
                         static_cast<uint32_t>(WorldField::Flags) | static_cast<uint32_t>(WorldField::FlowAccum) |
                         static_cast<uint32_t>(WorldField::Downhill) | static_cast<uint32_t>(WorldField::TemperatureMean) |
                         static_cast<uint32_t>(WorldField::TemperatureRange) | static_cast<uint32_t>(WorldField::Precipitation);
    for (TileId tile = 0; tile < world->grid->tileCount(); ++tile) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        world->grid->latLonOf(tile, latDeg, lonDeg);
        const bool land = std::abs(latDeg) < block.landLatDeg && std::abs(lonDeg) < block.landLonDeg;
        world->data.elevation[tile] = land ? 500.0F : -3000.0F;
        world->data.biome[tile] = static_cast<uint8_t>(land ? Biome::TemperateGrassland : Biome::Ocean);
        world->data.flags[tile] = land ? 0 : kFlagOcean;
        world->data.temperatureMean[tile] = 150;
        world->data.temperatureRange[tile] = 100;
        world->data.precipitation[tile] = 900;
    }
    return world;
}

// A river from source toward mouth, one tile a step to the neighbour nearest the mouth, its flow growing
// downstream. The mouth itself is not flagged.
inline void carveRiver(GeneratedWorld& world, TileId source, TileId mouth) {
    const SphereGrid& grid = *world.grid;
    const Vec3d target = grid.tileCenter(mouth);
    std::unordered_set<TileId> visited;
    TileId current = source;
    float flow = 90.0F;
    constexpr int kLongestRiverTiles = 1000;
    for (int step = 0; step < kLongestRiverTiles; ++step) {
        if (current == mouth || !visited.insert(current).second) {
            break;
        }
        std::array<TileId, 6> neighbours{};
        const uint32_t count = grid.neighbors(current, neighbours);
        int best = -1;
        double bestAlignment = -2.0;
        for (uint32_t i = 0; i < count; ++i) {
            const double alignment = dotProduct(grid.tileCenter(neighbours[i]), target);
            if (alignment > bestAlignment) {
                bestAlignment = alignment;
                best = static_cast<int>(i);
            }
        }
        if (best < 0) {
            break;
        }
        world.data.flags[current] |= kFlagRiver;
        world.data.flowAccum[current] = flow;
        world.data.downhill[current] = static_cast<uint8_t>(best);
        flow += 15.0F;
        current = neighbours[static_cast<uint32_t>(best)];
    }
}

inline void addLake(GeneratedWorld& world, TileId tile) {
    world.data.flags[tile] |= kFlagLake;
    world.data.biome[tile] = static_cast<uint8_t>(Biome::Lake);
}

} // namespace worldgen::test_support
