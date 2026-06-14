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
// Uses Earth-like defaults (rot=1 -> hadleyEdge=30, ferrelEdge=60).
double expectedNoOrographic(const TestWorld& w, TileId t, uint64_t stageSeed) {
    const Vec3d c = w.grid.tileCenter(t);
    const double lat = foundation::det_math::asin(c.z) / (3.14159265358979323846 / 180.0);
    const double absLat = lat < 0.0 ? -lat : lat;

    // Match the parameterized latitudeBase with Earth-like edges (rot=1 -> hadley=30, ferrel=60).
    const double hadleyEdge = 30.0;
    const double ferrelEdge = 60.0;
    const double itczEdge   = 0.5 * hadleyEdge;   // 15
    const double subDryPeak = hadleyEdge;           // 30
    double base = 0.0;
    if (absLat < itczEdge)
        base = 2000.0 - absLat * (20.0 * 15.0 / itczEdge);
    else if (absLat < subDryPeak)
        base = 2000.0 - absLat * (60.0 * 30.0 / subDryPeak) + 200.0;
    else if (absLat < ferrelEdge)
        base = 200.0 + (absLat - subDryPeak) * (18.3 * 30.0 / (ferrelEdge - subDryPeak));
    else
        base = 750.0 - (absLat - ferrelEdge) * (7.5 * 30.0 / (90.0 - ferrelEdge));

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
    // Flat land bordered by ocean so the advection sweep has a moisture source.
    // The bands are keyed to AtmosphereStage's hadleyEdge (~30 deg at Earth-like
    // rotation), so the exact-degree windows are loose; we assert the RELATIVE
    // band SHAPE (the sweep scales the absolute level by the moisture budget):
    // equator wet, subtropics (~hadleyEdge) dry, midlatitudes recover.
    TestWorld w;
    w.setElevation([](double lat, double) {
        return (lat < -55.0 || lat > 55.0) ? -3000.0 : 100.0; // polar oceans
    });
    w.runAtmosphere();
    w.runPrecipitation();

    double sumEq = 0.0, sumSub = 0.0, sumMid = 0.0;
    uint32_t cntEq = 0, cntSub = 0, cntMid = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);
        if (w.world.data.elevation[t] < 0.0f) continue; // land only
        const double absLat = lat < 0.0 ? -lat : lat;
        const double p = w.world.data.precipitation[t];
        if (absLat < 10.0)                        { sumEq  += p; ++cntEq;  }
        else if (absLat >= 25.0 && absLat < 35.0) { sumSub += p; ++cntSub; }
        else if (absLat >= 40.0 && absLat < 52.0) { sumMid += p; ++cntMid; }
    }
    ASSERT_GT(cntEq, 20u);
    ASSERT_GT(cntSub, 20u);
    ASSERT_GT(cntMid, 20u);

    const double meanEq  = sumEq / cntEq;
    const double meanSub = sumSub / cntSub;
    const double meanMid = sumMid / cntMid;

    EXPECT_GT(meanEq, meanSub * 1.5) << "ITCZ much wetter than the subtropical dry zone";
    EXPECT_GT(meanMid, meanSub)      << "midlatitudes recover from the subtropical minimum";
    EXPECT_GT(meanEq, meanMid)       << "equator wetter than midlatitudes";
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
// Orographic: a WIDE 3000 m ridge against an eastward wind (upwind = west).
// The moisture-advection sweep must (a) boost the windward face >= 1.4x its
// latitude base, and (b) keep the lee dry across the FULL ridge width — not just
// the first few tiles. This is the capability the old fixed-4-hop march lacked.
// ============================================================================

namespace {

// Longitude geometry (wind blows east, so moisture arrives from the west):
//   ocean   lon < kRidgeWest              (moisture source right at the coast)
//   RIDGE   kRidgeWest..kRidgeEast at 3000 m, rising straight from the coast so the
//           leading edge gets a fresh, fully-charged ocean parcel and the full
//           windward boost (WIDE: ~40 deg lon ~= 3000+ km)
//   lee plain kRidgeEast..kLeeEast        (must stay dry across its width)
//   ocean   lon >= kLeeEast
// Land confined to lat [30,58) so the band is sampled by many tiles at n=24.
constexpr double kRidgeWest     = -30.0;
constexpr double kRidgeEast     =  10.0; // 40 deg of ridge
constexpr double kLeeEast       =  60.0; // 50 deg of lee
constexpr double kRidgeLatLo    =  30.0;
constexpr double kRidgeLatHi    =  58.0;

double ridgeWorldElevation(double lat, double lon) {
    if (lat < kRidgeLatLo || lat >= kRidgeLatHi) return -3000.0;
    if (lon < kRidgeWest)  return -3000.0;             // ocean (moisture source)
    if (lon < kRidgeEast)  return 3000.0;              // wide ridge rising from the coast
    if (lon < kLeeEast)    return 200.0;               // lee plain
    return -3000.0;                                    // ocean east
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

    // Windward boost: somewhere on the windward face the ocean-charged parcel
    // first rises and wrings out enhanced rain (>=1.4x its latitude base). The
    // exact tile depends on grid quantization, so we take the PEAK windward ratio
    // over the windward face rather than a fixed-tile mean. (The crest and lee are
    // drier — the parcel already rained out — which is the shadow.)
    double peakWindward = 0.0;
    // Lee dryness: sample the lee in THREE longitude bands spanning the full ridge
    // width, to prove the shadow does not fade after a few tiles.
    double sumLeeNear = 0.0, sumLeeMid = 0.0, sumLeeFar = 0.0;
    uint32_t cntLeeNear = 0, cntLeeMid = 0, cntLeeFar = 0;

    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);
        if (lat < 36.0 || lat >= 52.0) continue;

        const double ratio = static_cast<double>(w.world.data.precipitation[t]) /
                             expectedNoOrographic(w, t, kPrecipSeed);
        if (lon >= kRidgeWest - 2.0 && lon < kRidgeWest + 12.0) { // windward face
            if (ratio > peakWindward) peakWindward = ratio;
        } else if (lon >= kRidgeEast + 2.0 && lon < kRidgeEast + 16.0) {
            sumLeeNear += ratio; ++cntLeeNear;               // just past the crest
        } else if (lon >= kRidgeEast + 18.0 && lon < kRidgeEast + 32.0) {
            sumLeeMid += ratio; ++cntLeeMid;                 // mid lee
        } else if (lon >= kRidgeEast + 34.0 && lon < kLeeEast - 2.0) {
            sumLeeFar += ratio; ++cntLeeFar;                 // far lee, near east coast
        }
    }
    ASSERT_GT(cntLeeNear, 2u);
    ASSERT_GT(cntLeeMid, 2u);
    ASSERT_GT(cntLeeFar, 2u);

    const double leeNear = sumLeeNear / cntLeeNear;
    const double leeMid  = sumLeeMid / cntLeeMid;
    const double leeFar  = sumLeeFar / cntLeeFar;

    EXPECT_GE(peakWindward, 1.4)
        << "windward face should be orographically boosted (peak " << peakWindward << "x)";
    // The lee stays dry across the FULL ridge width: every lee band well below the
    // windward peak. (The old 4-hop march only shadowed the first ~4 tiles.)
    EXPECT_LE(leeNear, 0.5 * peakWindward) << "near lee " << leeNear;
    EXPECT_LE(leeMid,  0.5 * peakWindward) << "mid lee "  << leeMid;
    EXPECT_LE(leeFar,  0.55 * peakWindward) << "far lee " << leeFar
        << " — shadow must persist across the whole ridge width";
}

// ============================================================================
// Rain shadow scales with belt width: a wider ridge keeps its lee dry farther.
// Two worlds, identical except the ridge is twice as wide in the second; the lee
// immediately behind each crest must be dry in BOTH (a fixed-hop march would let
// the wide ridge's lee recover).
// ============================================================================

TEST(PrecipitationStage, RainShadowScalesWithWidth) {
    auto build = [](TestWorld& w, double ridgeEastLon) {
        w.setElevation([ridgeEastLon](double lat, double lon) -> double {
            if (lat < 30.0 || lat >= 58.0) return -3000.0;
            if (lon < -60.0) return -3000.0;        // ocean (moisture source)
            if (lon < -50.0) return 200.0;          // windward plain
            if (lon < ridgeEastLon) return 3000.0;  // ridge (variable width)
            if (lon < ridgeEastLon + 30.0) return 200.0; // lee plain
            return -3000.0;
        });
        w.setUniformAtmosphere(100, kWindEast);
    };

    TestWorld narrow, wide;
    build(narrow, -30.0); // 20 deg ridge
    build(wide,   10.0);  // 60 deg ridge (3x wider)
    narrow.runPrecipitation();
    wide.runPrecipitation();

    // Sample the lee just past each crest: dryness must hold for both widths.
    auto leeRatio = [](TestWorld& w, double crestLon) {
        double sum = 0.0; uint32_t cnt = 0;
        for (TileId t = 0; t < w.grid.tileCount(); ++t) {
            double lat{}, lon{};
            w.grid.latLonOf(t, lat, lon);
            if (lat < 36.0 || lat >= 52.0) continue;
            if (lon >= crestLon + 2.0 && lon < crestLon + 16.0) {
                sum += static_cast<double>(w.world.data.precipitation[t]) /
                       expectedNoOrographic(w, t, kPrecipSeed);
                ++cnt;
            }
        }
        return cnt > 0 ? sum / cnt : -1.0;
    };

    const double narrowLee = leeRatio(narrow, -30.0);
    const double wideLee    = leeRatio(wide,   10.0);
    ASSERT_GT(narrowLee, 0.0);
    ASSERT_GT(wideLee, 0.0);

    EXPECT_LT(narrowLee, 0.6) << "narrow-ridge lee should be dry (" << narrowLee << ")";
    EXPECT_LT(wideLee,   0.6) << "wide-ridge lee should be just as dry (" << wideLee
        << ") — the shadow scales with width";
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
        std::array<TileId, 6> nbs{};
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
        std::array<TileId, 6> nbs{};
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
