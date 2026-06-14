#include "worldgen/sampling/LandingSite.h"

#include "worldgen/data/GeneratedWorld.h"

#include <array>
#include <cassert>
#include <cmath>

namespace worldgen {

namespace {

bool hasField(const GeneratedWorld& world, WorldField f) {
    return (world.validFields & static_cast<uint32_t>(f)) != 0;
}

bool isWaterTile(const GeneratedWorld& world, TileId t) {
    if ((world.data.flags[t] & (kFlagOcean | kFlagLake)) != 0) return true;
    return world.data.elevation[t] < world.seaLevelMeters;
}

// Coast = land tile with at least one OCEAN neighbor. Computed from the grid
// rather than kFlagCoast so it works on worlds saved before BiomeStage set the
// flag. Ocean-only (not lake) so it maps to "saltwater" in the signal.
bool hasOceanNeighbor(const GeneratedWorld& world, TileId t) {
    std::array<TileId, 6> nbrs{};
    uint32_t count = world.grid->neighbors(t, nbrs);
    for (uint32_t i = 0; i < count; ++i) {
        if ((world.data.flags[nbrs[i]] & kFlagOcean) != 0) return true;
        // Unflagged ocean fallback for worlds without flags written.
        if ((world.data.flags[nbrs[i]] & (kFlagOcean | kFlagLake)) == 0 &&
            world.data.elevation[nbrs[i]] < world.seaLevelMeters) {
            return true;
        }
    }
    return false;
}

// Coast in the old, lake-inclusive sense: any water neighbor. Kept for
// findDefaultLandingSite's coastal tier (lake shores are good starts too).
bool isCoastTile(const GeneratedWorld& world, TileId t) {
    std::array<TileId, 6> nbrs{};
    uint32_t count = world.grid->neighbors(t, nbrs);
    for (uint32_t i = 0; i < count; ++i) {
        if (isWaterTile(world, nbrs[i])) return true;
    }
    return false;
}

// True if the tile itself or any neighbor carries the given flag.
bool tileOrNeighborHasFlag(const GeneratedWorld& world, TileId t, uint8_t flag) {
    if ((world.data.flags[t] & flag) != 0) return true;
    std::array<TileId, 6> nbrs{};
    uint32_t count = world.grid->neighbors(t, nbrs);
    for (uint32_t i = 0; i < count; ++i) {
        if ((world.data.flags[nbrs[i]] & flag) != 0) return true;
    }
    return false;
}

// Land tile that sits on, or one tile away from, fresh water (river or lake).
bool hasFreshwaterNearby(const GeneratedWorld& world, TileId t) {
    return tileOrNeighborHasFlag(world, t, kFlagRiver) ||
           tileOrNeighborHasFlag(world, t, kFlagLake);
}

} // namespace

WaterClass classifyWater(const GeneratedWorld& world, TileId tile) {
    assert(world.grid != nullptr);

    // River and lake flags only exist when Flags were written. Without them we
    // can only distinguish coast (from elevation) vs rain-fed.
    if (hasField(world, WorldField::Flags)) {
        if (tileOrNeighborHasFlag(world, tile, kFlagRiver)) return WaterClass::River;
        if (tileOrNeighborHasFlag(world, tile, kFlagLake))  return WaterClass::Lake;
    }
    if (hasOceanNeighbor(world, tile)) return WaterClass::Coastal;
    return WaterClass::RainFed;
}

const char* waterClassToString(WaterClass w) {
    switch (w) {
        case WaterClass::River:   return "River";
        case WaterClass::Lake:    return "Lake";
        case WaterClass::Coastal: return "Coastal";
        case WaterClass::RainFed: return "Rain-fed";
    }
    return "Unknown";
}

const char* habitabilityToString(Habitability h) {
    switch (h) {
        case Habitability::Easy:     return "Easy";
        case Habitability::Moderate: return "Moderate";
        case Habitability::Hard:     return "Hard";
        case Habitability::Harsh:    return "Harsh";
    }
    return "Unknown";
}

Habitability rateHabitability(const GeneratedWorld& world, TileId tile, WaterClass water) {
    // Score model (higher = easier). Three readable contributions:
    //   water    : freshwater best, coast workable, rain-fed scaled by rainfall.
    //   rainfall : independent bump — even with a river, a wet site is greener.
    //   climate  : penalty for extreme cold/heat (frostbite or no-water-survives).
    // The thresholds are colony-survival rough cuts, not a simulation; they map
    // to four buckets so the player gets a guide, not false precision.
    int score = 0;

    switch (water) {
        case WaterClass::River:
        case WaterClass::Lake:    score += 3; break; // drinkable on the doorstep
        case WaterClass::Coastal: score += 1; break; // needs desalination/wells
        case WaterClass::RainFed: score += 0; break; // depends entirely on rain
    }

    // Rainfall (mm/yr). Earth-ish: <250 desert, 250-600 semi-arid, >600 ample.
    uint16_t rainMm = 0;
    if (hasField(world, WorldField::Precipitation) && tile < world.data.precipitation.size()) {
        rainMm = world.data.precipitation[tile];
    }
    if (rainMm >= 600)      score += 2;
    else if (rainMm >= 250) score += 1;
    else if (rainMm < 100 && water == WaterClass::RainFed) score -= 1; // true desert, no backup

    // Temperature (mean, Celsius). The habitable band is roughly 0-25 C; outside
    // that, cold or heat starts to dominate survival.
    if (hasField(world, WorldField::TemperatureMean) && tile < world.data.temperatureMean.size()) {
        float tempC = world.data.temperatureMean[tile] / 10.0f;
        if (tempC < -10.0f || tempC > 35.0f)     score -= 2; // brutal
        else if (tempC < 0.0f || tempC > 28.0f)  score -= 1; // hard
    }

    if (score >= 4) return Habitability::Easy;
    if (score >= 2) return Habitability::Moderate;
    if (score >= 0) return Habitability::Hard;
    return Habitability::Harsh;
}

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
    // 1. temperate freshwater: land, |lat| <= 45, river/lake on tile or neighbor
    // 2. temperate coast:      land, |lat| <= 45, water neighbor
    // 3. temperate inland:     land, |lat| <= 45
    // 4. any land
    // 5. (0,0) on an all-water world
    //
    // Freshwater outranks a bare coast: a colony wants drinkable water, and the
    // landing signal reports the same fact, so the suggested start should match
    // what the pane calls the best site. One pass records the first match for
    // each lower tier and returns immediately on the first freshwater tile.
    TileId firstLand = kInvalidTile;
    TileId firstTemperate = kInvalidTile;
    TileId firstTemperateCoast = kInvalidTile;
    for (TileId t = 0; t < tileCount; ++t) {
        if (isWaterTile(world, t)) continue;
        if (firstLand == kInvalidTile) firstLand = t;

        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(t, latDeg, lonDeg);
        if (std::abs(latDeg) > kMaxPreferredLatDeg) continue;
        if (firstTemperate == kInvalidTile) firstTemperate = t;

        if (hasFreshwaterNearby(world, t)) return {latDeg, lonDeg};
        if (firstTemperateCoast == kInvalidTile && isCoastTile(world, t)) {
            firstTemperateCoast = t;
        }
    }

    TileId chosen = firstTemperateCoast != kInvalidTile ? firstTemperateCoast
                  : firstTemperate != kInvalidTile      ? firstTemperate
                                                        : firstLand;
    if (chosen != kInvalidTile) {
        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(chosen, latDeg, lonDeg);
        return {latDeg, lonDeg};
    }

    return {0.0, 0.0};
}

} // namespace worldgen
