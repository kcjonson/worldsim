#include "worldgen/sampling/LandingSite.h"

#include "worldgen/data/GeneratedWorld.h"

#include <array>
#include <cassert>
#include <cmath>
#include <limits>

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

// Green, livable biomes the default site lands in: forests, grasslands, savanna,
// and wetlands. Excludes deserts, beach (coastal sand), tundra, and ice/snow --
// arid or barren starts a colony can't live off of. The default scan considers
// only these; everything else is a fallback when no green tile exists.
bool isPreferredLandingBiome(Biome b) {
    switch (b) {
        case Biome::TropicalRainforest:
        case Biome::TropicalSeasonalForest:
        case Biome::TemperateDeciduousForest:
        case Biome::TemperateRainforest:
        case Biome::BorealForest:
        case Biome::MontaneForest:
        case Biome::TropicalSavanna:
        case Biome::TemperateGrassland:
        case Biome::AlpineGrassland:
        case Biome::TemperateWetland:
        case Biome::TropicalWetland:
            return true;
        default:
            return false;
    }
}

// How close clean water is, for ranking landing tiles. A river running THROUGH
// the tile scores highest: that tile's center is the 2D origin, so the channel
// passes through spawn and the colonist drops on the bank (see findRiverbankSpawn).
int waterScore(const GeneratedWorld& world, TileId t, WaterClass wc) {
    if ((world.data.flags[t] & kFlagRiver) != 0) return 6; // channel through the origin
    switch (wc) {
        case WaterClass::River: return 4; // river on a neighbor
        case WaterClass::Lake:  return 4; // lake on/near the tile
        case WaterClass::Coastal: return 2; // saltwater (needs work to drink)
        case WaterClass::RainFed: return 0;
    }
    return 0;
}

int habitabilityScore(Habitability h) {
    switch (h) {
        case Habitability::Easy:     return 4;
        case Habitability::Moderate: return 2;
        case Habitability::Hard:     return 0;
        case Habitability::Harsh:    return -4;
    }
    return 0;
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

    // Score temperate, GREEN land tiles by clean-water proximity plus a
    // habitability rating (water class + climate + rainfall). A river running
    // THROUGH the tile scores highest: the tile center is the 2D origin, so the
    // channel passes through spawn and the colonist drops on the bank (see
    // findRiverbankSpawn). Only vegetated biomes are considered -- deserts, beach,
    // and tundra all read as barren and starve a colony. Highest score wins; ties
    // break to the lowest TileId (strict >) for determinism. Fallbacks keep a
    // result for barren or all-water worlds.
    const bool haveBiome = hasField(world, WorldField::Biome);
    TileId firstLand = kInvalidTile;
    TileId firstTemperate = kInvalidTile;
    TileId best = kInvalidTile;
    int bestScore = std::numeric_limits<int>::min();
    for (TileId t = 0; t < tileCount; ++t) {
        if (isWaterTile(world, t)) continue;
        if (firstLand == kInvalidTile) firstLand = t;

        double latDeg = 0.0;
        double lonDeg = 0.0;
        grid.latLonOf(t, latDeg, lonDeg);
        if (std::abs(latDeg) > kMaxPreferredLatDeg) continue;
        if (firstTemperate == kInvalidTile) firstTemperate = t;

        if (haveBiome && !isPreferredLandingBiome(static_cast<Biome>(world.data.biome[t]))) continue;

        const WaterClass wc = classifyWater(world, t);
        const int score = waterScore(world, t, wc) +
                          habitabilityScore(rateHabitability(world, t, wc));
        if (score > bestScore) {
            bestScore = score;
            best = t;
        }
    }

    TileId chosen = best != kInvalidTile           ? best
                  : firstTemperate != kInvalidTile ? firstTemperate
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
