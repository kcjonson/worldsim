#pragma once

namespace worldgen {

struct GeneratedWorld;

struct LatLon {
    double latDeg{};
    double lonDeg{};
};

// Deterministically pick a landing site for the 2D gameplay world.
// Preference order (lowest TileId wins within each tier, no RNG):
//   1. temperate coast: land, |lat| <= 45 deg, at least one water neighbor
//   2. temperate inland: land, |lat| <= 45 deg
//   3. any land tile
//   4. (0, 0)
// Land = not ocean/lake-flagged and elevation >= sea level. Coast is
// computed from grid neighbors rather than kFlagCoast (the flag marks ocean
// coasts only; this also counts lake shores).
// Requires Elevation and Flags valid in world.validFields.
LatLon findDefaultLandingSite(const GeneratedWorld& world);

} // namespace worldgen
