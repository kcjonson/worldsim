#pragma once

#include <cstdint>

namespace worldgen {

enum class Biome : uint8_t {
    Ocean                    = 0,
    Lake                     = 1,
    TropicalRainforest       = 2,
    TropicalSeasonalForest   = 3,
    TemperateDeciduousForest = 4,
    TemperateRainforest      = 5,
    BorealForest             = 6,
    MontaneForest            = 7,
    TropicalSavanna          = 8,
    TemperateGrassland       = 9,
    AlpineGrassland          = 10,
    HotDesert                = 11,
    ColdDesert               = 12,
    SemiDesert               = 13,
    XericShrubland           = 14,
    ArcticTundra             = 15,
    AlpineTundra             = 16,
    PolarDesert              = 17,
    TemperateWetland         = 18,
    TropicalWetland          = 19,
    Beach                    = 20,
    Count                    = 21,
};

inline const char* biomeToString(Biome b) {
    switch (b) {
        case Biome::Ocean:                    return "Ocean";
        case Biome::Lake:                     return "Lake";
        case Biome::TropicalRainforest:       return "TropicalRainforest";
        case Biome::TropicalSeasonalForest:   return "TropicalSeasonalForest";
        case Biome::TemperateDeciduousForest: return "TemperateDeciduousForest";
        case Biome::TemperateRainforest:      return "TemperateRainforest";
        case Biome::BorealForest:             return "BorealForest";
        case Biome::MontaneForest:            return "MontaneForest";
        case Biome::TropicalSavanna:          return "TropicalSavanna";
        case Biome::TemperateGrassland:       return "TemperateGrassland";
        case Biome::AlpineGrassland:          return "AlpineGrassland";
        case Biome::HotDesert:                return "HotDesert";
        case Biome::ColdDesert:               return "ColdDesert";
        case Biome::SemiDesert:               return "SemiDesert";
        case Biome::XericShrubland:           return "XericShrubland";
        case Biome::ArcticTundra:             return "ArcticTundra";
        case Biome::AlpineTundra:             return "AlpineTundra";
        case Biome::PolarDesert:              return "PolarDesert";
        case Biome::TemperateWetland:         return "TemperateWetland";
        case Biome::TropicalWetland:          return "TropicalWetland";
        case Biome::Beach:                    return "Beach";
        default:                              return "Unknown";
    }
}

inline bool isWater(Biome b) {
    return b == Biome::Ocean || b == Biome::Lake;
}

inline bool isForest(Biome b) {
    return b == Biome::TropicalRainforest       ||
           b == Biome::TropicalSeasonalForest   ||
           b == Biome::TemperateDeciduousForest ||
           b == Biome::TemperateRainforest       ||
           b == Biome::BorealForest             ||
           b == Biome::MontaneForest;
}

} // namespace worldgen
