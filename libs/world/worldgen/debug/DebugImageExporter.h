#pragma once

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/WorldData.h"

#include <cstdint>
#include <string>

namespace worldgen {

enum class WorldFieldOrMode : uint32_t {
    Elevation    = static_cast<uint32_t>(WorldField::Elevation),
    Temperature  = static_cast<uint32_t>(WorldField::TemperatureMean),
    Precipitation= static_cast<uint32_t>(WorldField::Precipitation),
    Biome        = static_cast<uint32_t>(WorldField::Biome),
    PlateId      = static_cast<uint32_t>(WorldField::PlateId),
    Ocean        = static_cast<uint32_t>(WorldField::Flags),
    // Continental crust + plate boundaries:
    //   continental crust → dark green, oceanic → deep blue,
    //   plate boundary (neighbor with different plateId) → 1px black overlay.
    Crust        = 0x8000u,
};

// Write a width x (width/2) 24-bit BMP equirectangular projection of the world.
// Each pixel maps lat/lon to the nearest tile; mode selects the visualization.
// Returns true on success.
bool exportEquirectangularBmp(const GeneratedWorld& world,
                              WorldFieldOrMode mode,
                              const std::string& path,
                              int width = 2048);

} // namespace worldgen
