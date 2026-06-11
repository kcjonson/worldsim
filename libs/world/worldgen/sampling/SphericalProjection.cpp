#include "worldgen/sampling/SphericalProjection.h"

#include <math/DeterministicMath.h>

#include <algorithm>
#include <cmath>  // std::floor only — permitted (exact on all platforms)

namespace worldgen {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

constexpr double kMaxOriginLatDeg = 89.9;

double normalizeLonDeg(double lonDeg) {
    return lonDeg - 360.0 * std::floor((lonDeg + 180.0) / 360.0);
}

} // namespace

SphericalProjection::SphericalProjection(double planetRadiusMeters,
                                         double landingLatDeg, double landingLonDeg)
    : radiusMeters(planetRadiusMeters),
      originLatDeg(std::clamp(landingLatDeg, -kMaxOriginLatDeg, kMaxOriginLatDeg)),
      originLonDeg(normalizeLonDeg(landingLonDeg)),
      cosOriginLat(foundation::det_math::cos(originLatDeg * kDegToRad)) {}

LatLon SphericalProjection::worldToLatLon(double xMeters, double yMeters) const {
    double latDeg = originLatDeg + (yMeters / radiusMeters) * kRadToDeg;
    double lonDeg = originLonDeg + (xMeters / (radiusMeters * cosOriginLat)) * kRadToDeg;
    return {std::clamp(latDeg, -90.0, 90.0), normalizeLonDeg(lonDeg)};
}

WorldPos2d SphericalProjection::latLonToWorld(LatLon latLon) const {
    double latDelta = latLon.latDeg - originLatDeg;
    double lonDelta = latLon.lonDeg - originLonDeg;
    if (lonDelta > 180.0) lonDelta -= 360.0;
    if (lonDelta < -180.0) lonDelta += 360.0;
    return {
        lonDelta * kDegToRad * radiusMeters * cosOriginLat,
        latDelta * kDegToRad * radiusMeters,
    };
}

Vec3d SphericalProjection::worldToUnitVector(double xMeters, double yMeters) const {
    using foundation::det_math::cos;
    using foundation::det_math::sin;
    LatLon latLon = worldToLatLon(xMeters, yMeters);
    double lat = latLon.latDeg * kDegToRad;
    double lon = latLon.lonDeg * kDegToRad;
    double cosLat = cos(lat);
    return {cosLat * cos(lon), cosLat * sin(lon), sin(lat)};
}

} // namespace worldgen
