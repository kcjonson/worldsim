#include "worldgen/sampling/LandingSite.h"

#include "worldgen/data/GeneratedWorld.h"

#include <array>
#include <cassert>
#include <cmath>

namespace worldgen {

namespace {

bool isWaterTile(const GeneratedWorld& world, TileId t) {
    if ((world.data.flags[t] & (kFlagOcean | kFlagLake)) != 0) return true;
    return world.data.elevation[t] < world.seaLevelMeters;
}

// Coast = land tile with at least one water neighbor. Computed from the grid
// rather than kFlagCoast, which no pipeline stage currently sets.
bool isCoastTile(const GeneratedWorld& world, TileId t) {
    std::array<TileId, 8> nbrs{};
    uint32_t count = world.grid->neighbors(t, nbrs);
    for (uint32_t i = 0; i < count; ++i) {
        if (isWaterTile(world, nbrs[i])) return true;
    }
    return false;
}

} // namespace

LatLon findDefaultLandingSite(const GeneratedWorld& world) {
    constexpr uint32_t kRequiredFields =
        static_cast<uint32_t>(WorldField::Elevation) |
        static_cast<uint32_t>(WorldField::Flags);
    assert(world.grid != nullptr);
    assert((world.validFields & kRequiredFields) == kRequiredFields);

    constexpr double kMaxPreferredLatDeg = 45.0;

    const SphereGrid& grid = *world.grid;
    const uint32_t tileCount = grid.tileCount();

    // Tiers, all first-match by ascending TileId for determinism:
    // 1. temperate coast: land, |lat| <= 45, water neighbor
    // 2. temperate inland: land, |lat| <= 45
    // 3. any land
    // 4. (0,0) on an all-water world
    TileId firstLand = kInvalidTile;
    TileId firstTemperate = kInvalidTile;
    for (TileId t = 0; t < tileCount; ++t) {
        if (isWaterTile(world, t)) continue;
        if (firstLand == kInvalidTile) firstLand = t;

        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(t, latDeg, lonDeg);
        if (std::abs(latDeg) > kMaxPreferredLatDeg) continue;
        if (firstTemperate == kInvalidTile) firstTemperate = t;

        if (isCoastTile(world, t)) return {latDeg, lonDeg};
    }

    TileId chosen = firstTemperate != kInvalidTile ? firstTemperate : firstLand;
    if (chosen != kInvalidTile) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(chosen, latDeg, lonDeg);
        return {latDeg, lonDeg};
    }

    return {0.0, 0.0};
}

} // namespace worldgen
