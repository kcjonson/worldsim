#pragma once

// Picks the crew's drop within the 2D landing area: dry land kLandingWaterDistanceMeters from the
// nearest water, preferring a river, then a lake, then the (salt) coast. docs/technical/landing-sites.md
// is the contract.

#include "worldgen/data/GeneratedWorld.h"

#include <cstdint>
#include <memory>

namespace worldgen {

inline constexpr double kLandingWaterDistanceMeters = 30.0;
inline constexpr double kLandingWaterDistanceToleranceMeters = 3.0;

enum class SpawnWater : uint8_t { None, River, Lake, Ocean };

struct SpawnSite {
    double     xMeters{}; // offset from the landing origin (+x east, +y north)
    double     yMeters{};
    SpawnWater water{SpawnWater::None}; // the nearest water's kind; None: no water in reach, the drop is the origin
    float      waterDistanceMeters{};   // to the nearest water's edge; zero for None
    // The drop is kLandingWaterDistanceMeters from its water within the tolerance. False for the narrow-land
    // fallback (no dry point that far from water) and for None.
    bool clear{};
};

// The dry point, nearest the origin, whose nearest water of any kind is kLandingWaterDistanceMeters off
// within kLandingWaterDistanceToleranceMeters (clear); river before lake before ocean. When no dry point is
// that far from water, the dry point farthest from it, in the same order (not clear). The origin with
// SpawnWater::None when no water is in reach. Requires Elevation, Biome, Flags valid; uses
// FlowAccum/Downhill (rivers) when present.
SpawnSite findWatersideSpawn(const std::shared_ptr<const GeneratedWorld>& world, double landingLatDeg,
                             double landingLonDeg, double searchRadiusMeters = 200.0);

} // namespace worldgen
