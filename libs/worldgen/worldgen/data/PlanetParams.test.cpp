#include "worldgen/data/PlanetParams.h"

#include <gtest/gtest.h>

#include <cmath>

namespace worldgen {

TEST(PlanetParams, PresetEarthLike) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    EXPECT_NEAR(p.starMass, 1.0, 0.01);
    EXPECT_NEAR(p.waterAmount, 0.70, 0.01);
    EXPECT_NEAR(p.planetRadius, 1.0, 0.01);
    EXPECT_EQ(p.tectonicPlateCount, 12);
}

TEST(PlanetParams, PresetDesertWorld) {
    PlanetParams p = PlanetParams::preset(Preset::DesertWorld);
    EXPECT_LT(p.waterAmount, 0.20);
    EXPECT_GT(p.semiMajorAxis, 1.0);
}

TEST(PlanetParams, PresetOceanWorld) {
    PlanetParams p = PlanetParams::preset(Preset::OceanWorld);
    EXPECT_GT(p.waterAmount, 0.85);
    EXPECT_EQ(p.tectonicPlateCount, 30);
}

TEST(PlanetParams, PresetFrozenWorld) {
    PlanetParams p = PlanetParams::preset(Preset::FrozenWorld);
    EXPECT_LT(p.starMass, 0.5);
    EXPECT_GT(p.semiMajorAxis, 1.0);
}

TEST(PlanetParams, PresetVolcanicWorld) {
    PlanetParams p = PlanetParams::preset(Preset::VolcanicWorld);
    EXPECT_LT(p.planetAge, 1e9);
    EXPECT_EQ(p.tectonicPlateCount, 20);
}

TEST(PlanetParams, PresetAncientGarden) {
    PlanetParams p = PlanetParams::preset(Preset::AncientGarden);
    EXPECT_GT(p.planetAge, 7e9);
    EXPECT_NEAR(p.eccentricity, 0.0, 1e-9);
    EXPECT_EQ(p.tectonicPlateCount, 6);
}

TEST(DerivedPlanetValues, EarthLikeSolarConstant) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    DerivedPlanetValues d = derive(p);
    // Earth's solar constant ~1361 W/m^2; allow 10% tolerance
    EXPECT_NEAR(d.solarConstant, 1361.0, 136.0)
        << "Earth-like solar constant should be ~1361 W/m^2";
}

TEST(DerivedPlanetValues, EarthLikeEquilibriumTemp) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    DerivedPlanetValues d = derive(p);
    // Earth's equilibrium temperature ~255 K; allow 10%
    EXPECT_NEAR(d.equilibriumTemperatureK, 255.0, 25.0)
        << "Earth-like eq temp should be ~255 K";
}

TEST(DerivedPlanetValues, GravitySanity) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    DerivedPlanetValues d = derive(p);
    EXPECT_NEAR(d.gravity, 9.81, 0.1);
}

TEST(DerivedPlanetValues, PlanetRadiusMeters) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    DerivedPlanetValues d = derive(p);
    EXPECT_NEAR(d.planetRadiusMeters, 6.371e6, 1000.0);
}

} // namespace worldgen
