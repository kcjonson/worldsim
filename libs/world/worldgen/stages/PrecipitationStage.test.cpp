// PrecipitationStage tests — M3d.
//
// Synthetic-world harness: hand-built StageContext on an n=24 grid (5760
// tiles), elevation set directly. Tests that exercise the real pipeline
// coupling run AtmosphereStage first to populate wind/temperature; tests
// that need controlled inputs hand-set temperatureMean/windDir instead.

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"
#include "worldgen/pipeline/GenerationStage.h"
#include "worldgen/stages/AtmosphereStage.h"
#include "worldgen/stages/PrecipitationStage.h"
#include "worldgen/stages/SnowStage.h"

#include <math/DeterministicMath.h>
#include <random/HashNoise.h>
#include <threading/TaskPool.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace worldgen {

namespace {

constexpr uint64_t kAtmoSeed   = 0xA7305EEDC0FFEEULL;
constexpr uint64_t kPrecipSeed = 0x9417AC1F5EED42ULL;
constexpr uint8_t  kWindEast   = 64;  // westerlies: wind blows toward the east

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

    // f(latDeg, lonDeg) -> elevation meters
    template <typename Fn>
    void setElevation(Fn&& f) {
        for (TileId t = 0; t < grid.tileCount(); ++t) {
            double lat{}, lon{};
            grid.latLonOf(t, lat, lon);
            world.data.elevation[t] = static_cast<float>(f(lat, lon));
        }
    }

    void setUniformAtmosphere(int16_t tempTenthsC, uint8_t windDir) {
        std::fill(world.data.temperatureMean.begin(),
                  world.data.temperatureMean.end(), tempTenthsC);
        std::fill(world.data.windDir.begin(), world.data.windDir.end(), windDir);
        std::fill(world.data.windSpeed.begin(), world.data.windSpeed.end(),
                  static_cast<uint8_t>(5));
    }

    StageContext makeContext(uint64_t stageSeed) {
        return StageContext{params, world.derived, grid,  world.data,
                            world,  pool,          stageSeed,
                            [](float) {},          cancel};
    }

    void runAtmosphere() {
        AtmosphereStage stage;
        StageContext ctx = makeContext(kAtmoSeed);
        stage.run(ctx);
    }

    void runPrecipitation() {
        PrecipitationStage stage;
        StageContext ctx = makeContext(kPrecipSeed);
        stage.run(ctx);
    }
};

// Everything the stage applies EXCEPT the orographic multiplier, so
// stored/expected isolates the orographic effect per tile.
// Kept in sync with PrecipitationStage.cpp (latitudeBase + pass 1).
double expectedNoOrographic(const TestWorld& w, TileId t, uint64_t stageSeed) {
    const Vec3d c = w.grid.tileCenter(t);
    const double lat = foundation::det_math::asin(c.z) / (3.14159265358979323846 / 180.0);
    const double absLat = lat < 0.0 ? -lat : lat;

    double base = 0.0;
    if (absLat < 15.0)      base = 2000.0 - absLat * 20.0;
    else if (absLat < 30.0) base = 2000.0 - absLat * 60.0 + 200.0;
    else if (absLat < 60.0) base = 200.0 + (absLat - 30.0) * 18.3;
    else                    base = 750.0 - (absLat - 60.0) * 7.5;

    const auto seed32 = static_cast<uint32_t>(stageSeed ^ (stageSeed >> 32));
    const float noise = foundation::valueNoise3(
        static_cast<float>(c.x) * 3.0f,
        static_cast<float>(c.y) * 3.0f,
        static_cast<float>(c.z) * 3.0f,
        seed32);
    base *= 0.6 + 0.8 * static_cast<double>(noise);
    base *= 0.5 + w.params.waterAmount;

    const double tempC = static_cast<double>(w.world.data.temperatureMean[t]) * 0.1;
    double evap = 0.8 + 0.4 * (tempC + 50.0) / 110.0;
    if (evap < 0.8) evap = 0.8;
    if (evap > 1.2) evap = 1.2;
    return base * evap;
}

uint32_t fieldBit(WorldField f) { return static_cast<uint32_t>(f); }

} // namespace

// ============================================================================
// Latitude band shape on flat land (AtmosphereStage provides temps/wind)
// ============================================================================

TEST(PrecipitationStage, LatitudeBandShapeOnFlatLand) {
    TestWorld w;
    w.setElevation([](double, double) { return 100.0; }); // all land
    w.runAtmosphere();
    w.runPrecipitation();

    double sumEq = 0.0, sumSub = 0.0, sumMid = 0.0;
    uint32_t cntEq = 0, cntSub = 0, cntMid = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);
        const double absLat = lat < 0.0 ? -lat : lat;
        const double p = w.world.data.precipitation[t];
        if (absLat < 10.0)                       { sumEq  += p; ++cntEq;  }
        else if (absLat >= 27.0 && absLat < 33.0){ sumSub += p; ++cntSub; }
        else if (absLat >= 42.0 && absLat < 55.0){ sumMid += p; ++cntMid; }
    }
    ASSERT_GT(cntEq, 50u);
    ASSERT_GT(cntSub, 50u);
    ASSERT_GT(cntMid, 50u);

    const double meanEq  = sumEq / cntEq;
    const double meanSub = sumSub / cntSub;
    const double meanMid = sumMid / cntMid;

    EXPECT_GT(meanEq, 1200.0) << "equatorial land should be wet";
    EXPECT_LT(meanSub, 600.0) << "subtropical (27-33 deg) interior should be dry";
    EXPECT_GT(meanEq, 1.8 * meanSub);
    EXPECT_GT(meanMid, 1.2 * meanSub) << "midlatitudes recover from the subtropical minimum";
    EXPECT_GT(meanEq, meanMid);
}

// ============================================================================
// Evaporation scaling: cold world drier than warm world, same band/seed
// ============================================================================

TEST(PrecipitationStage, EvaporationScalesWithTemperature) {
    auto flat = [](double, double) { return 100.0; };

    TestWorld cold;
    cold.setElevation(flat);
    cold.setUniformAtmosphere(-300, kWindEast); // -30.0 C
    cold.runPrecipitation();

    TestWorld warm;
    warm.setElevation(flat);
    warm.setUniformAtmosphere(250, kWindEast);  // +25.0 C
    warm.runPrecipitation();

    double sumCold = 0.0, sumWarm = 0.0;
    for (TileId t = 0; t < cold.grid.tileCount(); ++t) {
        sumCold += cold.world.data.precipitation[t];
        sumWarm += warm.world.data.precipitation[t];
        EXPECT_GE(warm.world.data.precipitation[t],
                  cold.world.data.precipitation[t]);
    }
    // Expected ratio = evap(25C)/evap(-30C) = 1.0727/0.8727 ≈ 1.229
    const double ratio = sumWarm / sumCold;
    EXPECT_GT(ratio, 1.15);
    EXPECT_LT(ratio, 1.30);
}

// ============================================================================
// Orographic: 3000 m ridge against the westerlies at lat ~45.
// Windward crest >= 1.4x its latitude base; leeward plain <= 0.5x windward.
// ============================================================================

namespace {

// Meridional ridge: ocean west of lon 14, windward plain [14,20), ridge
// [20,30) at 3000 m, leeward plain [30,40), ocean east of 40; land confined
// to lat [35,58). At n=24 a tile is ~2.7 deg (~3.4-4.4 deg of lon across the
// sampled latitudes), so the 4-hop upwind march from the western crest
// reaches ocean, and every leeward tile sees the ridge.
double ridgeWorldElevation(double lat, double lon) {
    if (lat < 35.0 || lat >= 58.0) return -3000.0;
    if (lon >= 14.0 && lon < 20.0) return 200.0;
    if (lon >= 20.0 && lon < 30.0) return 3000.0;
    if (lon >= 30.0 && lon < 40.0) return 200.0;
    return -3000.0;
}

void buildRidgeWorld(TestWorld& w) {
    w.setElevation(ridgeWorldElevation);
    w.setUniformAtmosphere(100, kWindEast); // 10.0 C everywhere; upwind = west
}

} // namespace

TEST(PrecipitationStage, RainShadowAndWindwardBoost) {
    TestWorld w;
    buildRidgeWorld(w);
    w.runPrecipitation();

    double sumWindward = 0.0, sumLeeward = 0.0;
    uint32_t cntWindward = 0, cntLeeward = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);

        const double ratio = static_cast<double>(w.world.data.precipitation[t]) /
                             expectedNoOrographic(w, t, kPrecipSeed);
        if (lat >= 38.0 && lat < 54.0 &&
            lon >= 20.0 && lon < 23.5) {           // western (windward) crest
            sumWindward += ratio; ++cntWindward;
        } else if (lat >= 40.0 && lat < 50.0 &&
                   lon >= 30.0 && lon < 38.0) {    // leeward plain
            sumLeeward += ratio; ++cntLeeward;
        }
    }
    ASSERT_GT(cntWindward, 3u);
    ASSERT_GT(cntLeeward, 4u);

    const double windwardRatio = sumWindward / cntWindward;
    const double leewardRatio  = sumLeeward / cntLeeward;

    EXPECT_GE(windwardRatio, 1.4)
        << "windward crest should be boosted (got " << windwardRatio << "x)";
    EXPECT_LE(leewardRatio, 0.5 * windwardRatio)
        << "rain shadow: leeward " << leewardRatio
        << "x vs windward " << windwardRatio << "x";
}

// ============================================================================
// Downhill: steepest strictly-lower neighbor, sinks and ocean are 0xFF
// ============================================================================

TEST(PrecipitationStage, DownhillSteepestNeighborAndSinks) {
    TestWorld w;
    // Northern hemisphere land sloping down toward the equator; southern
    // hemisphere ocean.
    w.setElevation([](double lat, double) { return lat * 60.0; });

    // Hand-made pit: strictly below all its neighbors -> sink.
    const TileId pit = w.grid.fromLatLon(45.0, 90.0);
    w.world.data.elevation[pit] = 1.0f;

    w.setUniformAtmosphere(100, kWindEast);
    w.runPrecipitation();

    const auto& data = w.world.data;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        std::array<TileId, 8> nbs{};
        const uint32_t cnt = w.grid.neighbors(t, nbs);

        if (data.elevation[t] < w.world.seaLevelMeters) {
            EXPECT_EQ(data.downhill[t], 0xFFu) << "ocean tile " << t;
            EXPECT_EQ(data.flowAccum[t], 0.0f) << "ocean tile " << t;
            continue;
        }

        // Reference: strictly lowest neighbor, ties by lowest TileId.
        float    bestE   = data.elevation[t];
        uint32_t bestIdx = 0xFFu;
        TileId   bestId  = kInvalidTile;
        for (uint32_t k = 0; k < cnt; ++k) {
            const float e = data.elevation[nbs[k]];
            if (e < bestE || (bestIdx != 0xFFu && e == bestE && nbs[k] < bestId)) {
                bestE = e; bestIdx = k; bestId = nbs[k];
            }
        }
        EXPECT_EQ(data.downhill[t], static_cast<uint8_t>(bestIdx))
            << "tile " << t;
        if (bestIdx != 0xFFu) {
            EXPECT_LT(bestIdx, cnt);
        }
    }

    EXPECT_EQ(data.downhill[pit], 0xFFu) << "local minimum must be a sink";
}

// ============================================================================
// flowAccum: monotonic accumulation along a downhill chain
// ============================================================================

TEST(PrecipitationStage, FlowAccumMonotonicAlongChain) {
    TestWorld w;
    w.setElevation([](double lat, double) { return lat * 60.0; });
    w.setUniformAtmosphere(100, kWindEast);
    w.runPrecipitation();

    const auto& data = w.world.data;

    // Every land tile carries at least its own seed amount.
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        if (data.elevation[t] < w.world.seaLevelMeters) continue;
        EXPECT_GE(data.flowAccum[t],
                  static_cast<float>(data.precipitation[t]) / 1000.0f - 1e-4f);
    }

    // Walk a chain from high latitude to the coast.
    TileId cur = w.grid.fromLatLon(60.0, 0.0);
    ASSERT_GE(data.elevation[cur], w.world.seaLevelMeters);
    int steps = 0;
    while (data.downhill[cur] != 0xFFu) {
        std::array<TileId, 8> nbs{};
        const uint32_t cnt = w.grid.neighbors(cur, nbs);
        ASSERT_LT(data.downhill[cur], cnt);
        const TileId next = nbs[data.downhill[cur]];
        EXPECT_LT(data.elevation[next], data.elevation[cur]);
        if (data.elevation[next] < w.world.seaLevelMeters) break; // outlet reached
        EXPECT_GT(data.flowAccum[next], data.flowAccum[cur])
            << "flow must strictly increase downstream (step " << steps << ")";
        cur = next;
        ASSERT_LT(++steps, 10000);
    }
    EXPECT_GT(steps, 3) << "chain should descend several tiles";
}

// ============================================================================
// Determinism: bit-identical at different thread counts
// ============================================================================

TEST(PrecipitationStage, DeterministicAcrossThreadCounts) {
    TestWorld w1(24, 1);
    TestWorld w2(24, 5);
    buildRidgeWorld(w1);
    buildRidgeWorld(w2);
    w1.runPrecipitation();
    w2.runPrecipitation();

    EXPECT_EQ(w1.world.data.precipitation, w2.world.data.precipitation);
    EXPECT_EQ(w1.world.data.downhill, w2.world.data.downhill);
    ASSERT_EQ(w1.world.data.flowAccum.size(), w2.world.data.flowAccum.size());
    for (size_t t = 0; t < w1.world.data.flowAccum.size(); ++t) {
        // Bit-identical, not just approximately equal.
        EXPECT_EQ(w1.world.data.flowAccum[t], w2.world.data.flowAccum[t])
            << "flowAccum differs at tile " << t;
    }
}

// ============================================================================
// Field ownership: PrecipitationStage sets exactly Precipitation|FlowAccum|
// Downhill; SnowStage no longer claims FlowAccum/Downhill.
// ============================================================================

TEST(PrecipitationStage, ValidFieldsOwnership) {
    TestWorld w;
    w.setElevation([](double, double) { return 100.0; });
    w.setUniformAtmosphere(100, kWindEast);

    ASSERT_EQ(w.world.validFields, 0u);
    w.runPrecipitation();
    EXPECT_EQ(w.world.validFields,
              fieldBit(WorldField::Precipitation) |
              fieldBit(WorldField::FlowAccum) |
              fieldBit(WorldField::Downhill));

    const uint32_t beforeSnow = w.world.validFields;
    SnowStage snow;
    StageContext ctx = w.makeContext(0xBEEFULL);
    snow.run(ctx);
    EXPECT_EQ(w.world.validFields,
              beforeSnow |
              fieldBit(WorldField::Flags) |
              fieldBit(WorldField::SnowCover))
        << "SnowStage must add only Flags and SnowCover";
}

} // namespace worldgen
