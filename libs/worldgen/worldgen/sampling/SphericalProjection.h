#pragma once

// Local equirectangular projection around a fixed landing site, per
// docs/technical/3d-to-2d-sampling.md. 2D world positions are meters from
// the landing site: +x east, +y north. Longitude is scaled by cos(landing
// latitude), so the mapping is exactly invertible but distorts with distance
// from the landing site; it is intended for gameplay-scale extents.

#include "worldgen/grid/SphereGrid.h"
#include "worldgen/sampling/LandingSite.h"

namespace worldgen {

struct WorldPos2d {
    double x{};
    double y{};
};

class SphericalProjection {
  public:
    // Landing latitude is clamped just short of the poles, where the
    // projection is degenerate (longitude undefined).
    SphericalProjection(double planetRadiusMeters, double landingLatDeg, double landingLonDeg);

    // 2D world position -> lat/lon degrees.
    // Longitude wrapped to [-180, 180), latitude clamped to [-90, 90].
    [[nodiscard]] LatLon worldToLatLon(double xMeters, double yMeters) const;

    // lat/lon degrees -> 2D world position (shortest-path longitude delta).
    [[nodiscard]] WorldPos2d latLonToWorld(LatLon latLon) const;

    // Unit vector for a 2D world position, matching SphereGrid's convention:
    // x = cos(lat)cos(lon), y = cos(lat)sin(lon), z = sin(lat).
    [[nodiscard]] Vec3d worldToUnitVector(double xMeters, double yMeters) const;

  private:
    double radiusMeters;
    double originLatDeg;
    double originLonDeg;
    double cosOriginLat;
};

} // namespace worldgen
