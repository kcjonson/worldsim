#pragma once

#include "worldgen/data/Biome.h"
#include "worldgen/grid/SphereGrid.h" // TileId

#include <cstdint>
#include <optional>

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

// Deterministically pick a landing site for the 2D gameplay world: a green,
// watered spot, never a desert, beach, or tundra. Among temperate (|lat| <= 45)
// vegetated land tiles (forest/grassland/savanna/wetland), scores by clean-water
// proximity plus a habitability rating (water class + climate + rainfall) and
// takes the highest (ties to lowest TileId, no RNG). A
// river running THROUGH the tile scores best: the tile center is the 2D origin,
// so the channel passes through spawn and the colonist drops on the bank (see
// findRiverbankSpawn). Falls back to any temperate land, then any land, then
// (0, 0) on a desert/water-only world.
// Land = not ocean/lake-flagged and elevation >= sea level.
// Requires Elevation and Flags valid; uses Biome (desert skip), TemperatureMean,
// and Precipitation (habitability) when present.
//
// preferredBiome: when set, tiles matching that biome receive a +5 bonus added
// on top of the normal water + habitability score, making a preferred-biome tile
// on the same water tier win decisively. The caller (quickstart path) passes
// TemperateDeciduousForest; all other callers omit it and get identical behaviour
// to before.
LatLon findDefaultLandingSite(const GeneratedWorld& world,
                              std::optional<Biome> preferredBiome = std::nullopt);

} // namespace worldgen
