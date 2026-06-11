#include "worldgen/sampling/SphericalProjection.h"

#include <math/DeterministicMath.h>

#include <gtest/gtest.h>

#include <cmath>

namespace worldgen {

namespace {
constexpr double kEarthRadiusMeters = 6.371e6;
constexpr double kPi = 3.14159265358979323846;
} // namespace

TEST(SphericalProjection, RoundTripNearLandingSite) {
    SphericalProjection projection(kEarthRadiusMeters, 35.5, -118.2);
    const double points[][2] = {
        {0.0, 0.0},
        {25600.0, -15360.0},
        {-40000.0, 40000.0},
        {123.25, -987.5},
    };
    for (const auto& p : points) {
        LatLon latLon = projection.worldToLatLon(p[0], p[1]);
        WorldPos2d back = projection.latLonToWorld(latLon);
        EXPECT_NEAR(back.x, p[0], 0.01);
        EXPECT_NEAR(back.y, p[1], 0.01);
    }
}

TEST(SphericalProjection, AxesPointEastAndNorth) {
    SphericalProjection projection(kEarthRadiusMeters, 10.0, 20.0);

    LatLon north = projection.worldToLatLon(0.0, 10000.0);
    EXPECT_GT(north.latDeg, 10.0);
    EXPECT_NEAR(north.lonDeg, 20.0, 1e-9);

    LatLon east = projection.worldToLatLon(10000.0, 0.0);
    EXPECT_GT(east.lonDeg, 20.0);
    EXPECT_NEAR(east.latDeg, 10.0, 1e-9);
}

TEST(SphericalProjection, LongitudeScaledByCosLatitude) {
    // cos(60 deg) = 0.5: the same eastward distance spans twice the longitude.
    SphericalProjection equator(kEarthRadiusMeters, 0.0, 0.0);
    SphericalProjection midLatitude(kEarthRadiusMeters, 60.0, 0.0);

    LatLon atEquator = equator.worldToLatLon(10000.0, 0.0);
    LatLon atSixty = midLatitude.worldToLatLon(10000.0, 0.0);
    EXPECT_NEAR(atSixty.lonDeg, atEquator.lonDeg * 2.0, 1e-6);
}

TEST(SphericalProjection, LongitudeWrapsAtAntimeridian) {
    SphericalProjection projection(kEarthRadiusMeters, 0.0, 179.5);

    constexpr double kEastward = 200000.0;  // ~1.8 deg of longitude at the equator
    LatLon latLon = projection.worldToLatLon(kEastward, 0.0);
    EXPECT_LT(latLon.lonDeg, -178.0);
    EXPECT_GT(latLon.lonDeg, -179.0);

    WorldPos2d back = projection.latLonToWorld(latLon);
    EXPECT_NEAR(back.x, kEastward, 0.01);
    EXPECT_NEAR(back.y, 0.0, 0.01);
}

TEST(SphericalProjection, LatitudeClampsAtPoles) {
    SphericalProjection north(kEarthRadiusMeters, 80.0, 0.0);
    LatLon overNorth = north.worldToLatLon(0.0, 3.0e6);  // ~27 deg past the origin
    EXPECT_DOUBLE_EQ(overNorth.latDeg, 90.0);

    SphericalProjection south(kEarthRadiusMeters, -80.0, 0.0);
    LatLon overSouth = south.worldToLatLon(0.0, -3.0e6);
    EXPECT_DOUBLE_EQ(overSouth.latDeg, -90.0);
}

TEST(SphericalProjection, PolarLandingSiteStaysFinite) {
    SphericalProjection projection(kEarthRadiusMeters, 90.0, 0.0);
    LatLon latLon = projection.worldToLatLon(1000.0, 1000.0);
    EXPECT_TRUE(std::isfinite(latLon.latDeg));
    EXPECT_TRUE(std::isfinite(latLon.lonDeg));
    EXPECT_LE(latLon.latDeg, 90.0);
}

TEST(SphericalProjection, UnitVectorMatchesSphereGridConvention) {
    using foundation::det_math::cos;
    using foundation::det_math::sin;

    SphericalProjection projection(kEarthRadiusMeters, 35.5, -118.2);
    Vec3d v = projection.worldToUnitVector(0.0, 0.0);

    double lat = 35.5 * kPi / 180.0;
    double lon = -118.2 * kPi / 180.0;
    EXPECT_NEAR(v.x, cos(lat) * cos(lon), 1e-12);
    EXPECT_NEAR(v.y, cos(lat) * sin(lon), 1e-12);
    EXPECT_NEAR(v.z, sin(lat), 1e-12);
}

} // namespace worldgen
