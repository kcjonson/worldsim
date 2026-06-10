#pragma once

// Biome - re-exports worldgen::Biome as the engine's canonical biome type.
// worldgen::Biome is the single definition; this header is the engine's include seam.

#include <worldgen/data/Biome.h>

namespace engine::world {

using Biome = ::worldgen::Biome;

// Re-export helpers so existing call sites don't need to change.
using ::worldgen::biomeToString;
using ::worldgen::isWater;

// String-to-biome lookup used by the asset system (PlacementExecutor).
// Returns TemperateGrassland as the default when the name is unrecognised.
constexpr Biome stringToBiome(const char* name) {
    auto eq = [](const char* a, const char* b) -> bool {
        while (*a && *b) {
            if (*a != *b) return false;
            ++a; ++b;
        }
        return *a == *b;
    };

    if (eq(name, "Ocean"))                    return Biome::Ocean;
    if (eq(name, "Lake"))                     return Biome::Lake;
    if (eq(name, "TropicalRainforest"))       return Biome::TropicalRainforest;
    if (eq(name, "TropicalSeasonalForest"))   return Biome::TropicalSeasonalForest;
    if (eq(name, "TemperateDeciduousForest")) return Biome::TemperateDeciduousForest;
    if (eq(name, "TemperateRainforest"))      return Biome::TemperateRainforest;
    if (eq(name, "BorealForest"))             return Biome::BorealForest;
    if (eq(name, "MontaneForest"))            return Biome::MontaneForest;
    if (eq(name, "TropicalSavanna"))          return Biome::TropicalSavanna;
    if (eq(name, "TemperateGrassland"))       return Biome::TemperateGrassland;
    if (eq(name, "AlpineGrassland"))          return Biome::AlpineGrassland;
    if (eq(name, "HotDesert"))                return Biome::HotDesert;
    if (eq(name, "ColdDesert"))               return Biome::ColdDesert;
    if (eq(name, "SemiDesert"))               return Biome::SemiDesert;
    if (eq(name, "XericShrubland"))           return Biome::XericShrubland;
    if (eq(name, "ArcticTundra"))             return Biome::ArcticTundra;
    if (eq(name, "AlpineTundra"))             return Biome::AlpineTundra;
    if (eq(name, "PolarDesert"))              return Biome::PolarDesert;
    if (eq(name, "TemperateWetland"))         return Biome::TemperateWetland;
    if (eq(name, "TropicalWetland"))          return Biome::TropicalWetland;
    if (eq(name, "Beach"))                    return Biome::Beach;
    return Biome::TemperateGrassland;
}

} // namespace engine::world
