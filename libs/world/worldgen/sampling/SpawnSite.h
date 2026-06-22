#pragma once

// Picks the colonist's drop point within the 2D landing area: dry land right
// beside clean water. A colony dies without water, so the spawn is biased to a
// riverbank, then a lake shore, then (saltwater) coast. Because a river tile's
// center projects to the 2D origin, a river-through landing tile puts the channel
// at the origin and the bank a few meters off — so this reliably lands the
// colonist by the river at any resolution.

#include "worldgen/data/GeneratedWorld.h"

#include <memory>

namespace worldgen {

struct SpawnSite {
    double xMeters{};   // offset from the landing origin (+x east, +y north)
    double yMeters{};
    bool   nearWater{}; // false = no water found within the search radius (dry fallback at origin)
    bool   freshWater{};// true = adjacent water is fresh (river or lake), not ocean
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
