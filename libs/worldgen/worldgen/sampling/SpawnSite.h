#pragma once

// Picks the colonist's drop point within the 2D landing area: dry land right
// beside clean water. A colony dies without water, so the spawn is biased to a
// riverbank, then a lake shore, then (saltwater) coast. Because a river tile's
// center projects to the 2D origin, a river-through landing tile puts the channel
// at the origin and the bank a few meters off — so this reliably lands the
// colonist by the river at any resolution.

#include "worldgen/data/GeneratedWorld.h"

#include <cstdint>
#include <memory>

namespace worldgen {

enum class SpawnWater : uint8_t { None, River, Lake, Ocean };

struct SpawnSite {
    double     xMeters{}; // offset from the landing origin (+x east, +y north)
    double     yMeters{};
    SpawnWater water{SpawnWater::None}; // None: no water within the search radius, the drop is the origin
    // River: distance to the channel edge. Lake, Ocean: the shortest 1 m axis probe, out to the
    // bank distance, that lands in water. None: zero.
    float waterDistanceMeters{};
};

// Closest dry-land cell beside water near the landing origin. Fresh water
// (riverbank, then lake shore) outranks salt coast; falls back to the origin
// when no water is within searchRadiusMeters. Requires Elevation, Biome, Flags
// valid; uses FlowAccum/Downhill (rivers) when present.
SpawnSite findRiverbankSpawn(const std::shared_ptr<const GeneratedWorld>& world,
                             double landingLatDeg, double landingLonDeg,
                             double searchRadiusMeters = 200.0,
                             double bankDistanceMeters = 6.0);

} // namespace worldgen
