#pragma once

#include "worldgen/grid/SphereGrid.h" // TileId

#include <cstdint>

namespace worldgen {

struct GeneratedWorld;

struct LatLon {
    double latDeg{};
    double lonDeg{};
};

// Coarse water classification for a single tile, derived from its own flags
// plus its immediate neighbors. This is the survival-critical signal the
// landing pane reports: whether a site has drinkable fresh water.
//
// "Reliable, not exact": the 2D chunk layer draws rivers/lakes from the SAME
// coarse flags (kFlagRiver / kFlagLake / flowAccum), so "this tile has a river"
// is a fact; only the precise bank is located on landing. There is deliberately
// no distance-to-freshwater field (see W-3 in water-hydrology.md).
//
// Priority (most useful first): fresh water on/near the tile wins over coast,
// coast wins over dry.
enum class WaterClass : uint8_t {
    River,   // kFlagRiver on the tile or a neighbor — freshwater, drinkable
    Lake,    // kFlagLake on the tile or a neighbor — freshwater, drinkable
    Coastal, // an ocean neighbor — saltwater (not drinkable without work)
    RainFed, // no surface water — colony depends on rainfall (precipitation)
};

// True for freshwater (river or lake): the only directly-drinkable classes.
inline constexpr bool isFreshwater(WaterClass w) {
    return w == WaterClass::River || w == WaterClass::Lake;
}

// Classify the tile's water situation from its coarse flags + neighbors.
// Requires Flags valid in world.validFields; reads Elevation as a fallback for
// the water test. River/Lake are checked on the tile and its neighbors (a river
// flagged on the next tile over is still "nearby"); Coastal needs an ocean
// neighbor; otherwise RainFed.
WaterClass classifyWater(const GeneratedWorld& world, TileId tile);

// Habitability rating, coarsest-to-harshest. A guide for the player, not a sim.
enum class Habitability : uint8_t {
    Easy,     // freshwater + mild climate + decent rain
    Moderate, // workable: coast, or rain-fed with real rainfall, temperate
    Hard,     // marginal: dry, or cold/hot enough to bite
    Harsh,    // freezing/scorching desert — survival challenge
};

const char* habitabilityToString(Habitability h);
const char* waterClassToString(WaterClass w);

// Combine the water class with temperature and rainfall into a single rating.
// Documented, readable heuristic (see LandingSite.cpp). Reads TemperatureMean
// and Precipitation when valid; degrades gracefully when they are not.
Habitability rateHabitability(const GeneratedWorld& world, TileId tile, WaterClass water);

// Deterministically pick a landing site for the 2D gameplay world.
// Preference order (lowest TileId wins within each tier, no RNG):
//   1. temperate freshwater: land, |lat| <= 45 deg, river/lake on tile or neighbor
//   2. temperate coast: land, |lat| <= 45 deg, at least one water neighbor
//   3. temperate inland: land, |lat| <= 45 deg
//   4. any land tile
//   5. (0, 0)
// Land = not ocean/lake-flagged and elevation >= sea level. Coast is
// computed from grid neighbors rather than kFlagCoast (the flag marks ocean
// coasts only; this also counts lake shores).
// Requires Elevation and Flags valid in world.validFields.
LatLon findDefaultLandingSite(const GeneratedWorld& world);

} // namespace worldgen
