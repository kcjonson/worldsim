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
// computed from grid neighbors (kFlagCoast is not produced by any stage yet).
// Requires Elevation and Flags valid in world.validFields.
LatLon findDefaultLandingSite(const GeneratedWorld& world);

} // namespace worldgen
