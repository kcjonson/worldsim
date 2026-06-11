#pragma once

namespace worldgen {

struct GeneratedWorld;

struct LatLon {
    double latDeg{};
    double lonDeg{};
};

// Deterministically pick a landing site for the 2D gameplay world.
// Preference order (lowest TileId wins within each tier, no RNG):
//   1. coast-flagged land tile with |lat| <= 45 deg
//   2. any land tile
//   3. (0, 0)
// Land = not ocean/lake-flagged and elevation >= sea level.
// Requires Elevation and Flags valid in world.validFields.
LatLon findDefaultLandingSite(const GeneratedWorld& world);

} // namespace worldgen
