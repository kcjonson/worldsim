#pragma once

// Biome taxonomy - 21 values, frozen as of M3h.
// Assets and engine reference biomes by these names.

#include <cstdint>

namespace worldgen {

enum class Biome : uint8_t {
    Ocean,
    Lake,
    TropicalRainforest,
    TropicalSeasonalForest,
    TemperateDeciduousForest,
    TemperateRainforest,
    BorealForest,
    MontaneForest,
    TropicalSavanna,
    TemperateGrassland,
    AlpineGrassland,
    HotDesert,
    ColdDesert,
    SemiDesert,
    XericShrubland,
    ArcticTundra,
    AlpineTundra,
    PolarDesert,
    TemperateWetland,
    TropicalWetland,
    Beach,

    Count
};

constexpr const char* biomeToString(Biome biome) {
    switch (biome) {
        case Biome::Ocean:                   return "Ocean";
        case Biome::Lake:                    return "Lake";
        case Biome::TropicalRainforest:      return "TropicalRainforest";
        case Biome::TropicalSeasonalForest:  return "TropicalSeasonalForest";
        case Biome::TemperateDeciduousForest:return "TemperateDeciduousForest";
        case Biome::TemperateRainforest:     return "TemperateRainforest";
        case Biome::BorealForest:            return "BorealForest";
        case Biome::MontaneForest:           return "MontaneForest";
        case Biome::TropicalSavanna:         return "TropicalSavanna";
        case Biome::TemperateGrassland:      return "TemperateGrassland";
        case Biome::AlpineGrassland:         return "AlpineGrassland";
        case Biome::HotDesert:               return "HotDesert";
        case Biome::ColdDesert:              return "ColdDesert";
        case Biome::SemiDesert:              return "SemiDesert";
        case Biome::XericShrubland:          return "XericShrubland";
        case Biome::ArcticTundra:            return "ArcticTundra";
        case Biome::AlpineTundra:            return "AlpineTundra";
        case Biome::PolarDesert:             return "PolarDesert";
        case Biome::TemperateWetland:        return "TemperateWetland";
        case Biome::TropicalWetland:         return "TropicalWetland";
        case Biome::Beach:                   return "Beach";
        case Biome::Count:                   return "Count";
    }
    return "Unknown";
}

constexpr bool isWater(Biome biome) {
    return biome == Biome::Ocean || biome == Biome::Lake;
}

} // namespace worldgen
