#pragma once

// Tile -> RGBA color mapping used by the base tier (PlanetColorizer) to bake
// per-rhombus textures. Pure CPU, no GL.

#include "PlanetColorizer.h" // ColorMode

#include <cstdint>

namespace worldgen {
struct GeneratedWorld;
}

namespace planetview {

struct RGBA8 { uint8_t r, g, b, a; };

// Color for a single tile id under the given mode. validFields / seaLevel come
// from the snapshot. Out-of-range / invalid ids fall back to neutral gray.
RGBA8 colorForTile(uint32_t tileId, ColorMode mode,
                   const worldgen::GeneratedWorld& world);

} // namespace planetview
