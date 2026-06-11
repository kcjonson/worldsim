// BiomeStage tests — M3e.
//
// Synthetic-world harness: hand-built StageContext on an n=24 grid (5760
// tiles). Classifier-matrix tests hand-set temperature/precipitation for
// exact control; the zonation and showcase tests run the real
// AtmosphereStage (and PrecipitationStage for the showcase) so lapse-rate
// temperatures and orographic precipitation feed the classifier the way the
// pipeline does. Stages always run in pipeline order:
// [Atmosphere ->] [Precipitation ->] Ocean -> Biome [-> Snow].

#include "worldgen/data/Biome.h"
#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"
#include "worldgen/pipeline/GenerationStage.h"
#include "worldgen/pipeline/PlanetGenerator.h"
#include "worldgen/stages/AtmosphereStage.h"
#include "worldgen/stages/BiomeStage.h"
#include "worldgen/stages/OceanStage.h"
#include "worldgen/stages/PrecipitationStage.h"
#include "worldgen/stages/SnowStage.h"

#include <threading/TaskPool.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <set>
#include <thread>

namespace worldgen {

namespace {

constexpr uint64_t kAtmoSeed   = 0xA7305EEDC0FFEEULL;
constexpr uint64_t kPrecipSeed = 0x9417AC1F5EED42ULL;
constexpr uint64_t kOceanSeed  = 0x0CEA75EED1234ULL;
constexpr uint64_t kBiomeSeed  = 0xB10BE5EEDULL;
constexpr uint64_t kSnowSeed   = 0x5104705EEDULL;

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

    void fillClimate(int16_t tempTenthsC, uint16_t precipMmYr) {
        std::fill(world.data.temperatureMean.begin(),
                  world.data.temperatureMean.end(), tempTenthsC);
        std::fill(world.data.precipitation.begin(),
                  world.data.precipitation.end(), precipMmYr);
    }

    void fillDrainage(uint8_t downhill, float flow) {
        std::fill(world.data.downhill.begin(), world.data.downhill.end(),
                  downhill);
        std::fill(world.data.flowAccum.begin(), world.data.flowAccum.end(),
                  flow);
    }

    StageContext makeContext(uint64_t stageSeed) {
        return StageContext{params, world.derived, grid,  world.data,
                            world,  pool,          stageSeed,
                            [](float) {},          cancel};
    }

    void runAtmosphere()    { AtmosphereStage s;    auto ctx = makeContext(kAtmoSeed);   s.run(ctx); }
    void runPrecipitation() { PrecipitationStage s; auto ctx = makeContext(kPrecipSeed); s.run(ctx); }
    void runOcean()         { OceanStage s;         auto ctx = makeContext(kOceanSeed);  s.run(ctx); }
    void runBiome()         { BiomeStage s;         auto ctx = makeContext(kBiomeSeed);  s.run(ctx); }
    void runSnow()          { SnowStage s;          auto ctx = makeContext(kSnowSeed);   s.run(ctx); }
};

uint32_t fieldBit(WorldField f) { return static_cast<uint32_t>(f); }

Biome biomeAt(const TestWorld& w, TileId t) {
    return static_cast<Biome>(w.world.data.biome[t]);
}

bool hasOceanNeighbor(const TestWorld& w, TileId t) {
    std::array<TileId, 8> nbs{};
    const uint32_t cnt = w.grid.neighbors(t, nbs);
    for (uint32_t k = 0; k < cnt; ++k) {
        if (w.world.data.elevation[nbs[k]] < w.world.seaLevelMeters) return true;
    }
    return false;
}

} // namespace

// ============================================================================
// Ocean and lake flags map straight to water biomes
// ============================================================================

TEST(BiomeStage, OceanAndLakeMapping) {
    TestWorld w;
    // Western hemisphere ocean, eastern land.
    w.setElevation([](double, double lon) { return lon < 0.0 ? -3000.0 : 500.0; });
    w.fillClimate(100, 600); // 10 C, 600 mm
    w.fillDrainage(0, 0.0f);
    w.runOcean();

    const TileId lake = w.grid.fromLatLon(20.0, 90.0);
    ASSERT_GE(w.world.data.elevation[lake], 0.0f);
    w.world.data.flags[lake] |= kFlagLake;

    w.runBiome();

    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        if ((w.world.data.flags[t] & kFlagOcean) != 0) {
            EXPECT_EQ(biomeAt(w, t), Biome::Ocean) << "tile " << t;
        }
    }
    EXPECT_EQ(biomeAt(w, lake), Biome::Lake);
}

// ============================================================================
// Whittaker matrix: representative temperature x precipitation cells
// ============================================================================

TEST(BiomeStage, WhittakerMatrix) {
    struct Case {
        double   tempC;
        uint16_t precip;
        Biome    expected;
    };
    const Case cases[] = {
        {25.0, 2500, Biome::TropicalRainforest},
        {25.0, 1500, Biome::TropicalSeasonalForest},
        {25.0, 700,  Biome::TropicalSavanna},
        {25.0, 300,  Biome::HotDesert},
        {10.0, 1600, Biome::TemperateRainforest},
        {10.0, 900,  Biome::TemperateDeciduousForest},
        {10.0, 600,  Biome::TemperateGrassland},
        {15.0, 400,  Biome::XericShrubland},
        {8.0,  400,  Biome::SemiDesert},
        {19.0, 200,  Biome::HotDesert},
        {10.0, 200,  Biome::ColdDesert},
        {0.0,  500,  Biome::BorealForest},
        {0.0,  200,  Biome::ColdDesert},
        {-10.0, 200, Biome::ArcticTundra},
        {-10.0, 100, Biome::PolarDesert},
    };

    TestWorld w;
    // 500 m: above the wetland lowland cutoff, below the montane band, and no
    // ocean anywhere so neither beach nor coast logic interferes.
    w.setElevation([](double, double) { return 500.0; });
    w.fillDrainage(0, 0.0f);

    for (const Case& c : cases) {
        w.fillClimate(static_cast<int16_t>(c.tempC * 10.0), c.precip);
        w.runBiome();
        for (TileId t = 0; t < w.grid.tileCount(); ++t) {
            ASSERT_EQ(biomeAt(w, t), c.expected)
                << "expected " << biomeToString(c.expected) << " at "
                << c.tempC << " C / " << c.precip << " mm, got "
                << biomeToString(biomeAt(w, t)) << " (tile " << t << ")";
        }
    }
}

// ============================================================================
// Elevation zonation ladder on an equatorial mountain (real atmosphere temps)
// ============================================================================

namespace {

// Equatorial strip stepping up west to east: 500 -> 1800 -> 3000 -> 5000 m.
double ladderElevation(double lat, double lon) {
    if (lat < -6.0 || lat > 6.0) return -3000.0;
    if (lon >= 0.0  && lon < 15.0) return 500.0;
    if (lon >= 15.0 && lon < 30.0) return 1800.0;
    if (lon >= 30.0 && lon < 45.0) return 3000.0;
    if (lon >= 45.0 && lon < 60.0) return 5000.0;
    return -3000.0;
}

} // namespace

TEST(BiomeStage, ElevationZonationLadder) {
    TestWorld w;
    w.setElevation(ladderElevation);
    w.runAtmosphere(); // real temps: lapse rate cools each step
    std::fill(w.world.data.precipitation.begin(),
              w.world.data.precipitation.end(), static_cast<uint16_t>(1500));
    w.fillDrainage(0, 0.0f);
    w.runOcean();
    w.runBiome();

    uint32_t cnt500 = 0, cnt1800 = 0, cnt3000 = 0, cnt5000 = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);
        if (lat < -5.0 || lat > 5.0) continue;
        const float e = w.world.data.elevation[t];
        if (e == 500.0f) {
            // ~27.5 C, 1500 mm: hot and wet, tropical base of the ladder.
            EXPECT_EQ(biomeAt(w, t), Biome::TropicalSeasonalForest)
                << "base tile " << t << " got " << biomeToString(biomeAt(w, t));
            ++cnt500;
        } else if (e == 1800.0f) {
            EXPECT_EQ(biomeAt(w, t), Biome::MontaneForest)
                << "1800 m tile " << t << " got " << biomeToString(biomeAt(w, t));
            ++cnt1800;
        } else if (e == 3000.0f) {
            EXPECT_EQ(biomeAt(w, t), Biome::AlpineGrassland)
                << "3000 m tile " << t << " got " << biomeToString(biomeAt(w, t));
            ++cnt3000;
        } else if (e == 5000.0f) {
            EXPECT_EQ(biomeAt(w, t), Biome::AlpineTundra)
                << "5000 m tile " << t << " got " << biomeToString(biomeAt(w, t));
            ++cnt5000;
        }
    }
    EXPECT_GT(cnt500,  2u);
    EXPECT_GT(cnt1800, 2u);
    EXPECT_GT(cnt3000, 2u);
    EXPECT_GT(cnt5000, 2u);
}

// ============================================================================
// Wetlands: poor drainage on warm wet lowland
// ============================================================================

TEST(BiomeStage, WetlandDrainageTrigger) {
    TestWorld w;
    w.setElevation([](double, double) { return 50.0; }); // lowland, no ocean
    w.fillClimate(250, 1000); // 25 C, 1000 mm
    w.fillDrainage(0, 0.0f);  // well drained by default

    const TileId highFlow = w.grid.fromLatLon(10.0, 50.0);
    const TileId sink     = w.grid.fromLatLon(-20.0, 100.0);
    const TileId control  = w.grid.fromLatLon(40.0, 150.0);
    w.world.data.flowAccum[highFlow] = kRiverFlowThreshold + 1.0f;
    w.world.data.downhill[sink]      = 0xFF;

    w.runBiome();
    EXPECT_EQ(biomeAt(w, highFlow), Biome::TropicalWetland);
    EXPECT_EQ(biomeAt(w, sink),     Biome::TropicalWetland);
    EXPECT_EQ(biomeAt(w, control),  Biome::TropicalSavanna);

    // Cooler run: same drainage, temperate wetland instead.
    w.fillClimate(100, 1000); // 10 C
    w.runBiome();
    EXPECT_EQ(biomeAt(w, highFlow), Biome::TemperateWetland);
    EXPECT_EQ(biomeAt(w, control),  Biome::TemperateDeciduousForest);
}

TEST(BiomeStage, WetlandRequiresWarmthLowlandAndRain) {
    TestWorld w;
    w.setElevation([](double, double) { return 50.0; });
    w.fillDrainage(0, 0.0f);
    const TileId poorDrain = w.grid.fromLatLon(10.0, 50.0);
    w.world.data.flowAccum[poorDrain] = kRiverFlowThreshold + 1.0f;

    // Too high: 300 m is above the lowland cutoff.
    w.world.data.elevation[poorDrain] = 300.0f;
    w.fillClimate(250, 1000);
    w.runBiome();
    EXPECT_EQ(biomeAt(w, poorDrain), Biome::TropicalSavanna);
    w.world.data.elevation[poorDrain] = 50.0f;

    // Too dry: 800 mm misses the wetland precipitation bar.
    w.fillClimate(250, 800);
    w.runBiome();
    EXPECT_EQ(biomeAt(w, poorDrain), Biome::TropicalSavanna);

    // Too cold: 0 C freezes the trigger out.
    w.fillClimate(0, 1000);
    w.runBiome();
    EXPECT_EQ(biomeAt(w, poorDrain), Biome::BorealForest);
}

// ============================================================================
// Beach on low coastal land; kFlagCoast on all coastal land
// ============================================================================

namespace {

// Land block |lat| < 30: low shore (10 m) west half, bluff (100 m) east half.
double coastElevation(double lat, double lon) {
    if (lat < -30.0 || lat > 30.0 || lon < 0.0 || lon >= 60.0) return -3000.0;
    return lon < 30.0 ? 10.0 : 100.0;
}

} // namespace

TEST(BiomeStage, BeachAndCoastFlag) {
    TestWorld w;
    w.setElevation(coastElevation);
    w.fillClimate(150, 400); // 15 C, 400 mm -> XericShrubland baseline
    w.fillDrainage(0, 0.0f);
    w.runOcean();
    w.runBiome();

    uint32_t beaches = 0, bluffCoasts = 0, interior = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        const bool ocean = (w.world.data.flags[t] & kFlagOcean) != 0;
        const bool coastal = !ocean && hasOceanNeighbor(w, t);
        const bool flagged = (w.world.data.flags[t] & kFlagCoast) != 0;

        if (ocean) {
            EXPECT_FALSE(flagged) << "ocean tile " << t << " has kFlagCoast";
            continue;
        }
        EXPECT_EQ(flagged, coastal) << "tile " << t;

        const float e = w.world.data.elevation[t];
        if (coastal && e == 10.0f) {
            EXPECT_EQ(biomeAt(w, t), Biome::Beach) << "tile " << t;
            ++beaches;
        } else {
            EXPECT_EQ(biomeAt(w, t), Biome::XericShrubland)
                << "tile " << t << " got " << biomeToString(biomeAt(w, t));
            if (coastal) ++bluffCoasts;
            else ++interior;
        }
    }
    EXPECT_GT(beaches, 3u);
    EXPECT_GT(bluffCoasts, 3u) << "coastal bluffs must keep their climate biome";
    EXPECT_GT(interior, 10u);
}

TEST(BiomeStage, FrozenCoastIsNotBeach) {
    TestWorld w;
    w.setElevation(coastElevation);
    w.fillClimate(-60, 400); // -6 C: below freezing, polar branch
    w.fillDrainage(0, 0.0f);
    w.runOcean();
    w.runBiome();

    uint32_t frozenCoasts = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        if ((w.world.data.flags[t] & kFlagOcean) != 0) continue;
        if (!hasOceanNeighbor(w, t)) continue;
        EXPECT_TRUE((w.world.data.flags[t] & kFlagCoast) != 0) << "tile " << t;
        EXPECT_EQ(biomeAt(w, t), Biome::ArcticTundra)
            << "frozen coast tile " << t << " got "
            << biomeToString(biomeAt(w, t));
        ++frozenCoasts;
    }
    EXPECT_GT(frozenCoasts, 3u);
}

// ============================================================================
// Showcase world: real atmosphere + precipitation, >= 8 distinct land biomes
// ============================================================================

namespace {

// Pole-to-pole continent with a western cordillera stepping down eastward
// into a broad plain and an eastern coastal lowland strip.
double showcaseElevation(double, double lon) {
    if (lon < 0.0 || lon >= 90.0) return -3000.0;
    if (lon < 10.0) return 4500.0;
    if (lon < 20.0) return 3000.0;
    if (lon < 30.0) return 1500.0;
    if (lon < 75.0) return 300.0;
    return 10.0;
}

} // namespace

TEST(BiomeStage, ShowcaseWorldBiomeDiversity) {
    TestWorld w;
    w.setElevation(showcaseElevation);
    w.runAtmosphere();
    w.runPrecipitation();
    w.runOcean();
    w.runBiome();

    std::set<Biome> landBiomes;
    bool sawTropicalRain = false;
    bool sawDrySubtropics = false;
    int equatorialPlain = 0;
    int equatorialTropicalForest = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        if ((w.world.data.flags[t] & kFlagOcean) != 0) continue;
        const Biome b = biomeAt(w, t);
        landBiomes.insert(b);

        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);
        const double absLat = lat < 0.0 ? -lat : lat;
        const float  e = w.world.data.elevation[t];

        if (e == 300.0f && absLat < 8.0) {
            // Equatorial wet plain: tropical forest should dominate, but the
            // real precipitation stage legitimately dries rain-shadowed and
            // deep-interior tiles into savanna (rarely further), so assert
            // the majority, not every tile.
            ++equatorialPlain;
            if (b == Biome::TropicalRainforest ||
                b == Biome::TropicalSeasonalForest) {
                ++equatorialTropicalForest;
            }
            if (b == Biome::TropicalRainforest) sawTropicalRain = true;
        } else if (e == 300.0f && absLat >= 28.0 && absLat < 33.0) {
            if (b == Biome::HotDesert || b == Biome::ColdDesert ||
                b == Biome::SemiDesert || b == Biome::XericShrubland) {
                sawDrySubtropics = true;
            }
        } else if (absLat >= 72.0) {
            EXPECT_TRUE(b == Biome::ArcticTundra || b == Biome::PolarDesert ||
                        b == Biome::AlpineTundra)
                << "polar tile " << t << " (elev " << e << ") got "
                << biomeToString(b);
        }

        // 3000 m equatorial (~11 C, wet): above the alpine-grass floor, well
        // short of the tundra ceiling. 4500 m is always AlpineTundra (> 3500 m
        // regardless of climate). The 1500 m band sits at ~21 C, straddling
        // the 20 C tropical boundary, so its exact biome is left to the
        // controlled ElevationZonationLadder test.
        if (e == 3000.0f && absLat < 10.0) {
            EXPECT_EQ(b, Biome::AlpineGrassland)
                << "tile " << t << " got " << biomeToString(b);
        } else if (e == 4500.0f && absLat < 10.0) {
            EXPECT_EQ(b, Biome::AlpineTundra)
                << "tile " << t << " got " << biomeToString(b);
        }
    }

    EXPECT_TRUE(sawTropicalRain) << "no TropicalRainforest on the wet equator";
    ASSERT_GT(equatorialPlain, 0);
    EXPECT_GE(static_cast<double>(equatorialTropicalForest) / equatorialPlain, 0.6)
        << "tropical forest covers only " << equatorialTropicalForest << "/"
        << equatorialPlain << " equatorial plain tiles";
    EXPECT_TRUE(sawDrySubtropics) << "no dry biome in the 28-33 deg interior";
    EXPECT_TRUE(landBiomes.count(Biome::Beach) != 0u)
        << "no Beach on the low eastern coast";
    EXPECT_TRUE(landBiomes.count(Biome::TropicalWetland) != 0u)
        << "no TropicalWetland on the flat equatorial lowland";
    EXPECT_GE(landBiomes.size(), 8u)
        << "expected at least 8 distinct land biomes, got " << landBiomes.size();
}

// ============================================================================
// validFields: each stage claims exactly its own bits, in pipeline order
// ============================================================================

TEST(BiomeStage, ValidFieldsExactness) {
    TestWorld w;
    w.setElevation([](double, double lon) { return lon < 0.0 ? -3000.0 : 100.0; });
    w.fillClimate(100, 600);
    w.fillDrainage(0, 0.0f);

    ASSERT_EQ(w.world.validFields, 0u);
    w.runOcean();
    EXPECT_EQ(w.world.validFields, fieldBit(WorldField::WaterDepth));

    w.runBiome();
    EXPECT_EQ(w.world.validFields,
              fieldBit(WorldField::WaterDepth) | fieldBit(WorldField::Biome))
        << "BiomeStage must add only Biome";

    w.runSnow();
    EXPECT_EQ(w.world.validFields,
              fieldBit(WorldField::WaterDepth) | fieldBit(WorldField::Biome) |
              fieldBit(WorldField::Flags) | fieldBit(WorldField::SnowCover))
        << "SnowStage must add only Flags and SnowCover";
}

// ============================================================================
// Determinism: bit-identical biome/flags/snow at different thread counts
// ============================================================================

TEST(BiomeStage, DeterministicAcrossThreadCounts) {
    TestWorld w1(24, 1);
    TestWorld w2(24, 5);
    for (TestWorld* w : {&w1, &w2}) {
        w->setElevation(showcaseElevation);
        w->runAtmosphere();
        w->runPrecipitation();
        w->runOcean();
        w->runBiome();
        w->runSnow();
    }
    EXPECT_EQ(w1.world.data.biome,     w2.world.data.biome);
    EXPECT_EQ(w1.world.data.flags,     w2.world.data.flags);
    EXPECT_EQ(w1.world.data.snowCover, w2.world.data.snowCover);
}

// ============================================================================
// Full pipeline: WorldSummary populated on a small Earth-like run
// ============================================================================

TEST(BiomeStage, WorldSummaryFullPipeline) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 12;
    params.seed = 0xB10BE5EED12345ULL;

    PlanetGenerator gen;
    gen.start(params);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state != GenerationProgress::State::Running) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto world = gen.takeResult();
    ASSERT_NE(world, nullptr);

    const auto& s = world->summary;
    const uint32_t totalTiles = world->grid->tileCount();

    uint64_t histTotal = 0;
    for (uint32_t c : s.biomeHistogram) histTotal += c;
    EXPECT_EQ(histTotal, totalTiles) << "every tile must be classified";
    EXPECT_GT(s.biomeHistogram[static_cast<size_t>(Biome::Ocean)], 0u);

    EXPECT_GT(s.landFraction, 0.05f);
    EXPECT_LT(s.landFraction, 0.70f);
    EXPECT_GT(s.meanTemperatureC, 5.0f);
    EXPECT_LT(s.meanTemperatureC, 25.0f);
    EXPECT_GT(s.riverTileCount, 0u) << "a wet Earth-like world must have rivers";
    EXPECT_GE(s.habitability, 0.25f);
    EXPECT_LE(s.habitability, 0.90f);
}

} // namespace worldgen
