// SnowStage tests — M3e.
//
// Synthetic-world harness on an n=24 grid. Geography tests run the real
// AtmosphereStage so summit/polar temperatures come from the lapse-rate
// model; threshold and coverage tests hand-set temperatureMean for exact
// control. Pipeline order: [Atmosphere ->] Ocean -> Snow.

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"
#include "worldgen/pipeline/GenerationStage.h"
#include "worldgen/stages/AtmosphereStage.h"
#include "worldgen/stages/OceanStage.h"
#include "worldgen/stages/SnowStage.h"

#include <threading/TaskPool.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace worldgen {

namespace {

constexpr uint64_t kAtmoSeed  = 0xA7305EEDC0FFEEULL;
constexpr uint64_t kOceanSeed = 0x0CEA75EED1234ULL;
constexpr uint64_t kSnowSeed  = 0x5104705EEDULL;

struct TestWorld {
    PlanetParams params;
    SphereGrid grid;
    GeneratedWorld world;
    foundation::TaskPool pool;
    std::atomic<bool> cancel{false};

    explicit TestWorld(uint32_t n = 24, unsigned threads = 3)
        : params(PlanetParams::preset(Preset::EarthLike)), grid(n), pool(threads) {
        params.gridSubdivision = n;
        world.params  = params;
        world.derived = derive(params);
        world.data.allocate(grid.tileCount());
        world.seaLevelMeters = 0.0f;
    }

    template <typename Fn>
    void setElevation(Fn&& f) {
        for (TileId t = 0; t < grid.tileCount(); ++t) {
            double lat{}, lon{};
            grid.latLonOf(t, lat, lon);
            world.data.elevation[t] = static_cast<float>(f(lat, lon));
        }
    }

    void fillTemperature(int16_t tempTenthsC) {
        std::fill(world.data.temperatureMean.begin(),
                  world.data.temperatureMean.end(), tempTenthsC);
    }

    StageContext makeContext(uint64_t stageSeed) {
        return StageContext{params, world.derived, grid,  world.data,
                            world,  pool,          stageSeed,
                            [](float) {},          cancel};
    }

    void runAtmosphere() { AtmosphereStage s; auto ctx = makeContext(kAtmoSeed);  s.run(ctx); }
    void runOcean()      { OceanStage s;      auto ctx = makeContext(kOceanSeed); s.run(ctx); }
    void runSnow()       { SnowStage s;       auto ctx = makeContext(kSnowSeed);  s.run(ctx); }
};

uint32_t fieldBit(WorldField f) { return static_cast<uint32_t>(f); }

bool hasSnow(const TestWorld& w, TileId t) {
    return (w.world.data.flags[t] & kFlagPermanentSnow) != 0;
}

// Western hemisphere ocean; eastern land at 0 m with a 7000 m equatorial
// summit block. Pole-to-pole land coverage in the east.
double summitElevation(double lat, double lon) {
    if (lon < 0.0) return -3000.0;
    if (lat >= -5.0 && lat <= 5.0 && lon >= 100.0 && lon <= 140.0) return 7000.0;
    return 0.0;
}

} // namespace

// ============================================================================
// Snow on cold summits and poles, none on warm lowland or any ocean
// ============================================================================

TEST(SnowStage, SummitAndPolarSnowNotOcean) {
    TestWorld w;
    w.setElevation(summitElevation);
    w.runAtmosphere(); // summit ~-15 C via lapse, poles ~-18 C, equator ~+30 C
    w.runOcean();
    w.runSnow();

    uint32_t summit = 0, polarLand = 0, warmLand = 0, oceanTiles = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);
        const double absLat = lat < 0.0 ? -lat : lat;

        if ((w.world.data.flags[t] & kFlagOcean) != 0) {
            EXPECT_EQ(w.world.data.snowCover[t], 0u) << "ocean tile " << t;
            EXPECT_FALSE(hasSnow(w, t)) << "ocean tile " << t;
            ++oceanTiles;
            continue;
        }

        if (w.world.data.elevation[t] == 7000.0f) {
            EXPECT_TRUE(hasSnow(w, t)) << "summit tile " << t;
            EXPECT_GT(w.world.data.snowCover[t], 0u) << "summit tile " << t;
            ++summit;
        } else if (absLat > 82.0) {
            EXPECT_TRUE(hasSnow(w, t)) << "polar land tile " << t << " lat " << lat;
            EXPECT_GT(w.world.data.snowCover[t], 0u);
            ++polarLand;
        } else if (absLat < 30.0) {
            EXPECT_FALSE(hasSnow(w, t)) << "warm land tile " << t << " lat " << lat;
            EXPECT_EQ(w.world.data.snowCover[t], 0u);
            ++warmLand;
        }
    }
    EXPECT_GT(summit, 5u);
    EXPECT_GT(polarLand, 5u);
    EXPECT_GT(warmLand, 50u);
    EXPECT_GT(oceanTiles, 50u);
}

// ============================================================================
// Threshold = -10 - 3*(sqrt(atm) - 1), clamped [-16, -6]
// ============================================================================

TEST(SnowStage, ThresholdTracksAtmosphereStrength) {
    // -9 C land: snow-free at Earth pressure (threshold -10), snowy under a
    // thin atmosphere (atm 0.25 -> threshold -8.5).
    {
        TestWorld w;
        w.setElevation([](double, double) { return 100.0; });
        w.fillTemperature(-90);
        w.runSnow();
        for (TileId t = 0; t < w.grid.tileCount(); ++t) {
            ASSERT_FALSE(hasSnow(w, t)) << "tile " << t << " at atm=1";
        }
    }
    {
        TestWorld w;
        w.params.atmosphereStrength = 0.25;
        w.setElevation([](double, double) { return 100.0; });
        w.fillTemperature(-90);
        w.runSnow();
        for (TileId t = 0; t < w.grid.tileCount(); ++t) {
            ASSERT_TRUE(hasSnow(w, t)) << "tile " << t << " at atm=0.25";
            ASSERT_GT(w.world.data.snowCover[t], 0u);
        }
    }
}

TEST(SnowStage, ThresholdClampsForThickAtmosphere) {
    // atm 25 -> unclamped threshold -22, clamped to -16.
    TestWorld w;
    w.params.atmosphereStrength = 25.0;
    w.setElevation([](double, double) { return 100.0; });

    w.fillTemperature(-155); // -15.5 C: above the clamped threshold
    w.runSnow();
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        ASSERT_FALSE(hasSnow(w, t)) << "tile " << t;
    }

    w.fillTemperature(-170); // -17 C: below it
    w.runSnow();
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        ASSERT_TRUE(hasSnow(w, t)) << "tile " << t;
    }
}

// ============================================================================
// Coverage ramps linearly below threshold, saturating 50 C under it
// ============================================================================

TEST(SnowStage, CoverageRampsBelowThreshold) {
    TestWorld w;
    w.setElevation([](double, double) { return 100.0; });

    w.fillTemperature(-350); // -35 C: halfway down the ramp from -10
    w.runSnow();
    EXPECT_EQ(w.world.data.snowCover[0], 127u);

    w.fillTemperature(-600); // -60 C: saturated
    w.runSnow();
    EXPECT_EQ(w.world.data.snowCover[0], 255u);
}

// ============================================================================
// validFields: exactly Flags | SnowCover
// ============================================================================

TEST(SnowStage, ValidFieldsExactness) {
    TestWorld w;
    w.setElevation([](double, double) { return 100.0; });
    w.fillTemperature(-300);
    ASSERT_EQ(w.world.validFields, 0u);
    w.runSnow();
    EXPECT_EQ(w.world.validFields,
              fieldBit(WorldField::Flags) | fieldBit(WorldField::SnowCover));
}

// ============================================================================
// Determinism across thread counts
// ============================================================================

TEST(SnowStage, DeterministicAcrossThreadCounts) {
    TestWorld w1(24, 1);
    TestWorld w2(24, 5);
    for (TestWorld* w : {&w1, &w2}) {
        w->setElevation(summitElevation);
        w->runAtmosphere();
        w->runOcean();
        w->runSnow();
    }
    EXPECT_EQ(w1.world.data.snowCover, w2.world.data.snowCover);
    EXPECT_EQ(w1.world.data.flags,     w2.world.data.flags);
}

} // namespace worldgen
