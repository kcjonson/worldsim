#pragma once

#include "WorldData.h"
#include <string>

namespace worldgen {

// BiomeId constants.
struct Biome {
    static constexpr uint8_t Ocean        = 0;
    static constexpr uint8_t DeepOcean    = 1;
    static constexpr uint8_t Beach        = 2;
    static constexpr uint8_t Desert       = 3;
    static constexpr uint8_t Savanna      = 4;
    static constexpr uint8_t Grassland    = 5;
    static constexpr uint8_t Shrubland    = 6;
    static constexpr uint8_t TemperateForest = 7;
    static constexpr uint8_t TropicalForest  = 8;
    static constexpr uint8_t Taiga        = 9;
    static constexpr uint8_t Tundra       = 10;
    static constexpr uint8_t Ice          = 11;
    static constexpr uint8_t Mountain     = 12;
    static constexpr uint8_t Count        = 13;

    static const char* name(uint8_t id) {
        switch (id) {
            case Ocean:           return "Ocean";
            case DeepOcean:       return "Deep Ocean";
            case Beach:           return "Beach";
            case Desert:          return "Desert";
            case Savanna:         return "Savanna";
            case Grassland:       return "Grassland";
            case Shrubland:       return "Shrubland";
            case TemperateForest: return "Temperate Forest";
            case TropicalForest:  return "Tropical Forest";
            case Taiga:           return "Taiga";
            case Tundra:          return "Tundra";
            case Ice:             return "Ice";
            case Mountain:        return "Mountain";
            default:              return "Unknown";
        }
    }
};

// Which data fields are valid (generator may not fill all).
struct ValidFields {
    bool elevation{false};
    bool temperature{false};
    bool precipitation{false};
    bool biome{false};
    bool plates{false};
    bool snow{false};
};

// The complete output of a planet generator run.
struct GeneratedWorld {
    WorldData data;
    ValidFields valid;
    std::string generatorName;
    int seed{0};
};

} // namespace worldgen
