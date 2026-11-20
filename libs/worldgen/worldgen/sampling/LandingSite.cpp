#include "worldgen/sampling/LandingSite.h"

#include "worldgen/data/GeneratedWorld.h"

#include <math/DeterministicMath.h>
#include <utils/Log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace worldgen {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadiansPerDegree = kPi / 180.0;
constexpr double kDegreesPerRadian = 180.0 / kPi;

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

Vec3d unitVectorOf(LatLon point) {
    const double lat = point.latDeg * kRadiansPerDegree;
    const double lon = point.lonDeg * kRadiansPerDegree;
    const double cosLat = foundation::det_math::cos(lat);
    return {cosLat * foundation::det_math::cos(lon), cosLat * foundation::det_math::sin(lon),
            foundation::det_math::sin(lat)};
}

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Through SphereGrid::latLonOf for a tile centre, so an anchor on a tile's centre is bit-identical
// to the lat/lon every other caller has for that tile.
LatLon centreOf(const SphereGrid& grid, TileId tile) {
    LatLon centre;
    grid.latLonOf(tile, centre.latDeg, centre.lonDeg);
    return centre;
}

// On the great circle from the land tile's centre toward the lake tile's centre, kLakeShoreInsetMeters
// short of where the grid stops calling the point the land tile (the 2D lake shore). The grid decides
// containment in its lattice chart, so that edge is found by bisection on its own lookup, not taken
// as the centres' midpoint.
LatLon lakeAnchor(const GeneratedWorld& world, TileId land, TileId lake) {
    const SphereGrid& grid = *world.grid;
    const Vec3d from = grid.tileCenter(land);
    const Vec3d to = grid.tileCenter(lake);
    const double cosArc = std::clamp(dot(from, to), -1.0, 1.0);
    const Vec3d across{to.x - cosArc * from.x, to.y - cosArc * from.y, to.z - cosArc * from.z};
    const double acrossLength = foundation::det_math::sqrt(dot(across, across));
    const double arc = foundation::det_math::atan2(acrossLength, cosArc);
    const double radius = world.derived.planetRadiusMeters;

    const auto pointAt = [&](double angle) {
        const double along = foundation::det_math::cos(angle);
        const double side = foundation::det_math::sin(angle) / acrossLength;
        return Vec3d{along * from.x + side * across.x, along * from.y + side * across.y,
                     along * from.z + side * across.z};
    };

    double inside = 0.0;
    double outside = arc;
    while ((outside - inside) * radius > kLakeEdgeToleranceMeters) {
        const double middle = 0.5 * (inside + outside);
        if (grid.fromUnitVector(pointAt(middle)) == land) {
            inside = middle;
        } else {
            outside = middle;
        }
    }

    const Vec3d point = pointAt(std::max(0.0, inside - kLakeShoreInsetMeters / radius));
    return {foundation::det_math::asin(std::clamp(point.z, -1.0, 1.0)) * kDegreesPerRadian,
            foundation::det_math::atan2(point.y, point.x) * kDegreesPerRadian};
}

// Neighbours of `tile` that pass `keep`, nearest centre to the pick first, ties to the lowest TileId.
template <typename Keep>
std::vector<TileId> neighboursNearestFirst(const SphereGrid& grid, TileId tile, const Vec3d& pick, Keep keep) {
    std::array<TileId, 6> neighbours{};
    const uint32_t count = grid.neighbors(tile, neighbours);
    std::vector<std::pair<double, TileId>> ranked;
    for (uint32_t i = 0; i < count; ++i) {
        if (keep(neighbours[i])) ranked.emplace_back(-dot(grid.tileCenter(neighbours[i]), pick), neighbours[i]);
    }
    std::sort(ranked.begin(), ranked.end());
    std::vector<TileId> out;
    out.reserve(ranked.size());
    for (const auto& [distance, neighbour] : ranked) out.push_back(neighbour);
    return out;
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

// The old rating findDefaultLandingSite ranks by: water class, rainfall, and the mean temperature.
Habitability defaultSiteHabitability(const GeneratedWorld& world, TileId tile, WaterClass water) {
    int score = 0;

    switch (water) {
        case WaterClass::River:
        case WaterClass::Lake:    score += 3; break;
        case WaterClass::Coastal: score += 1; break;
        case WaterClass::RainFed: score += 0; break;
    }

    uint16_t rainMm = 0;
    if (hasField(world, WorldField::Precipitation) && tile < world.data.precipitation.size()) {
        rainMm = world.data.precipitation[tile];
    }
    if (rainMm >= 600)      score += 2;
    else if (rainMm >= 250) score += 1;
    else if (rainMm < 100 && water == WaterClass::RainFed) score -= 1;

    if (hasField(world, WorldField::TemperatureMean) && tile < world.data.temperatureMean.size()) {
        float tempC = world.data.temperatureMean[tile] / 10.0f;
        if (tempC < -10.0f || tempC > 35.0f)     score -= 2;
        else if (tempC < 0.0f || tempC > 28.0f)  score -= 1;
    }

    if (score >= 4) return Habitability::Easy;
    if (score >= 2) return Habitability::Moderate;
    if (score >= 0) return Habitability::Hard;
    return Habitability::Harsh;
}

} // namespace

WaterClass classifyWater(const GeneratedWorld& world, TileId tile) {
    if (!world.grid) {
        LOG_ERROR(World, "classifyWater: world has no grid");
        return WaterClass::RainFed;
    }

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

TileAssessment assessLandingTile(const GeneratedWorld& world, const BiomeProvisionTable& provisions,
                                 TileId tile, WaterClass verifiedWater) {
    TileAssessment assessment;
    assessment.water = verifiedWater;

    if (hasField(world, WorldField::Biome) && tile < world.data.biome.size()) {
        const std::size_t biome = world.data.biome[tile];
        if (biome < provisions.size()) {
            assessment.hasFood = provisions[biome].foodSourcesPerSquareKilometer >= kMinimumFoodSourcesPerSquareKilometer;
            assessment.hasWood = provisions[biome].woodSourcesPerSquareKilometer >= kMinimumWoodSourcesPerSquareKilometer;
        }
    }

    const bool haveMean = hasField(world, WorldField::TemperatureMean) && tile < world.data.temperatureMean.size();
    const bool haveRange = hasField(world, WorldField::TemperatureRange) && tile < world.data.temperatureRange.size();
    bool brutal = false;
    if (haveMean) {
        const double mean = world.data.temperatureMean[tile] / 10.0;
        const double halfAmplitude = haveRange ? world.data.temperatureRange[tile] / 10.0 : 0.0;
        assessment.livableTemperature = haveRange && mean >= kLivableMeanMinimumCelsius &&
                                        mean <= kLivableMeanMaximumCelsius &&
                                        mean - halfAmplitude >= kLivableColdestSeasonCelsius &&
                                        mean + halfAmplitude <= kLivableHottestSeasonCelsius;
        brutal = mean < kBrutalMeanMinimumCelsius || mean > kBrutalMeanMaximumCelsius ||
                 mean - halfAmplitude < kBrutalColdestSeasonCelsius ||
                 mean + halfAmplitude > kBrutalHottestSeasonCelsius;
    }

    int score = 0;
    switch (verifiedWater) {
        case WaterClass::River:
        case WaterClass::Lake:    score += 3; break;
        case WaterClass::Coastal: score += 1; break;
        case WaterClass::RainFed: break;
    }

    uint16_t rainMillimeters = 0;
    if (hasField(world, WorldField::Precipitation) && tile < world.data.precipitation.size()) {
        rainMillimeters = world.data.precipitation[tile];
    }
    if (rainMillimeters >= kAmpleRainfallMillimeters) {
        score += 2;
    } else if (rainMillimeters >= kModerateRainfallMillimeters) {
        score += 1;
    } else if (rainMillimeters < kDesertRainfallMillimeters && verifiedWater == WaterClass::RainFed) {
        score -= 1;
    }

    if (!assessment.livableTemperature) score -= brutal ? 2 : 1;
    if (!assessment.hasFood) score -= 1;
    if (!assessment.hasWood) score -= 1;

    assessment.habitability = score >= 4   ? Habitability::Easy
                            : score >= 2   ? Habitability::Moderate
                            : score >= 0   ? Habitability::Hard
                                           : Habitability::Harsh;
    return assessment;
}

LandingSite resolveLandingSite(const std::shared_ptr<const GeneratedWorld>& world,
                               const BiomeProvisionTable& provisions, LatLon pick) {
    const GeneratedWorld& planet = *world;
    const SphereGrid& grid = *planet.grid;
    const TileId picked = grid.fromLatLon(pick.latDeg, pick.lonDeg);

    auto landAt = [&](LatLon anchor) -> std::optional<LandingSite> {
        const SpawnSite spawn = findRiverbankSpawn(world, anchor.latDeg, anchor.lonDeg);
        if (spawn.water != SpawnWater::River && spawn.water != SpawnWater::Lake) return std::nullopt;
        const bool river = spawn.water == SpawnWater::River;
        const TileId tile = grid.fromLatLon(anchor.latDeg, anchor.lonDeg);
        return LandingSite{anchor, tile, river ? FreshWaterFeature::River : FreshWaterFeature::Lake, spawn,
                           assessLandingTile(planet, provisions, tile, river ? WaterClass::River : WaterClass::Lake)};
    };

    if (hasField(planet, WorldField::Flags) && !isWaterTile(planet, picked)) {
        const Vec3d pickDirection = unitVectorOf(pick);

        if ((planet.data.flags[picked] & kFlagRiver) != 0) {
            if (auto site = landAt(centreOf(grid, picked))) return *site;
        }
        const auto isLake = [&](TileId t) { return (planet.data.flags[t] & kFlagLake) != 0; };
        for (const TileId lake : neighboursNearestFirst(grid, picked, pickDirection, isLake)) {
            if (auto site = landAt(lakeAnchor(planet, picked, lake))) return *site;
        }
        const auto isLandRiver = [&](TileId t) {
            return (planet.data.flags[t] & kFlagRiver) != 0 && !isWaterTile(planet, t);
        };
        for (const TileId river : neighboursNearestFirst(grid, picked, pickDirection, isLandRiver)) {
            if (auto site = landAt(centreOf(grid, river))) return *site;
        }
    }

    const WaterClass water = hasOceanNeighbor(planet, picked) ? WaterClass::Coastal : WaterClass::RainFed;
    return LandingSite{pick, picked, FreshWaterFeature::None, findRiverbankSpawn(world, pick.latDeg, pick.lonDeg),
                       assessLandingTile(planet, provisions, picked, water)};
}

LatLon findDefaultLandingSite(const GeneratedWorld& world,
                              std::optional<Biome> preferredBiome) {
    constexpr uint32_t kRequiredFields =
        static_cast<uint32_t>(WorldField::Elevation) |
        static_cast<uint32_t>(WorldField::Flags);
    if (!world.grid || (world.validFields & kRequiredFields) != kRequiredFields) {
        LOG_ERROR(World, "findDefaultLandingSite: missing grid or required fields "
                         "(validFields=0x%x); returning origin",
                  world.validFields);
        return LatLon{0.0, 0.0};
    }

    constexpr double kMaxPreferredLatDeg = 45.0;
    // Bonus added to the score of any tile whose biome matches preferredBiome.
    // Value 5 is decisive: it exceeds the full habitability spread (+4 to -4 = 8)
    // with enough margin that even a Harsh preferred-biome tile beats an Easy
    // non-preferred tile on the same water tier. A same-tier preferred tile wins
    // by 5 points vs. any non-preferred tile, and degrades gracefully (best
    // preferred tile, else best green tile) when no preferred tile exists.
    constexpr int kBiomeBias = 5;

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
        int score = waterScore(world, t, wc) +
                    habitabilityScore(defaultSiteHabitability(world, t, wc));
        if (preferredBiome.has_value() && haveBiome &&
                static_cast<Biome>(world.data.biome[t]) == *preferredBiome) {
            score += kBiomeBias;
        }
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
