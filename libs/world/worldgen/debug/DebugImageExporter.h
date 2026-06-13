#pragma once

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"
#include "worldgen/tectonics/TectonicHistory.h"
#include "worldgen/tectonics/TectonicParams.h"

#include <cstdint>
#include <functional>
#include <string>

namespace worldgen {

struct ExportRgb { uint8_t r, g, b; };

enum class WorldFieldOrMode : uint32_t {
    Elevation       = static_cast<uint32_t>(WorldField::Elevation),
    Temperature     = static_cast<uint32_t>(WorldField::TemperatureMean),
    Precipitation   = static_cast<uint32_t>(WorldField::Precipitation),
    Biome           = static_cast<uint32_t>(WorldField::Biome),
    PlateId         = static_cast<uint32_t>(WorldField::PlateId),
    Ocean           = static_cast<uint32_t>(WorldField::Flags),
    // Continental crust + plate boundaries:
    //   continental crust → dark green, oceanic → deep blue,
    //   plate boundary (neighbor with different plateId) → 1px black overlay.
    Crust           = 0x8000u,
    // BoundaryType per-tile color:
    //   None=grey, ConvergentCC=red, ConvergentCO=orange,
    //   ConvergentOO=yellow, Divergent=blue, Transform=green.
    // Interior tiles colored by continental (dark tan) or oceanic (dark navy).
    BoundaryTypeMap = 0x8001u,
    // Crust age: continental → muted green; oceanic → white (age 0, ridge) to deep
    // blue (age 200+ Myr). Ramp matches the sim-only crustage frames.
    CrustAge        = 0x8002u,
    // Orogeny age: recent continental orogens → hot red; old scars → dark maroon;
    // never-orogenized or oceanic tiles → near-black. Ramp matches sim-only frames.
    OrogenyAge      = 0x8003u,
};

// Shared color ramp for crust age. crustType: uint8_t from WorldData::flags
// (kFlagContinentalCrust) or TectonicHistory::crustType.
// ageMyr: uint16_t from either WorldData::crustAge or computed from TectonicHistory.
inline ExportRgb crustAgeColor(uint16_t ageMyr, bool isContinental) {
    if (isContinental) {
        return {70, 110, 60}; // continental: muted green (age ramp is for ocean only)
    }
    // Oceanic: white/bright near ridge (age 0) → deep blue (age 200+ Myr).
    float t = static_cast<float>(ageMyr) / 200.0f;
    if (t > 1.0f) t = 1.0f;
    auto r = static_cast<uint8_t>(235 - static_cast<int>(t * 235));
    auto g = static_cast<uint8_t>(245 - static_cast<int>(t * 165));
    auto b = static_cast<uint8_t>(255 - static_cast<int>(t *  95));
    return {r, g, b};
}

// Shared color ramp for orogeny age. ageMyr: int32_t age in Myr, or
// tectonics::kOrogenyNever for tiles that have never been orogenized.
// isContinental: oceanic tiles render as near-black.
inline ExportRgb orogenyAgeColor(int32_t ageMyr, bool isContinental) {
    if (!isContinental) return {15, 20, 35}; // ocean: dark background
    if (ageMyr == tectonics::kOrogenyNever) return {30, 35, 45}; // never orogenized
    float t = static_cast<float>(ageMyr) / 800.0f;
    if (t > 1.0f) t = 1.0f;
    // hot (recent) → cool/dark (old): red → maroon.
    auto r = static_cast<uint8_t>(240 - static_cast<int>(t * 150));
    auto g = static_cast<uint8_t>(180 - static_cast<int>(t * 150));
    auto b = static_cast<uint8_t>( 60 - static_cast<int>(t *  40));
    return {r, g, b};
}

// Write a width x (width/2) 24-bit BMP equirectangular projection of the world.
// Each pixel maps lat/lon to the nearest tile; mode selects the visualization.
// Returns true on success.
bool exportEquirectangularBmp(const GeneratedWorld& world,
                              WorldFieldOrMode mode,
                              const std::string& path,
                              int width = 2048);

// Generic equirectangular BMP writer: maps each pixel's lat/lon to the nearest
// tile on `grid` and colors it via `colorOf(tile)`. Shared with worldgen-cli's
// --sim-only mode so BMP plumbing lives in one place.
bool exportEquirectangularBmp(const SphereGrid& grid,
                              const std::function<ExportRgb(TileId)>& colorOf,
                              const std::string& path,
                              int width = 2048);

} // namespace worldgen
