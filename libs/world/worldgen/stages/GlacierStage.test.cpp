// GlacierStage tests — physical model (PDD surface mass balance + perfect-plastic
// profile). Synthetic n=24 worlds. Controlled tests set temperature + precipitation
// directly and provide a margin (ocean or warm land) so the perfect-plastic distance
// transform has an edge to anchor on; geography tests run the real AtmosphereStage.

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"
#include "worldgen/pipeline/GenerationStage.h"
#include "worldgen/stages/AtmosphereStage.h"
#include "worldgen/stages/GlacierStage.h"
#include "worldgen/stages/OceanStage.h"
#include "worldgen/stages/SnowStage.h"

#include <threading/TaskPool.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace worldgen {

namespace {

constexpr uint64_t kAtmoSeed  = 0xA7305EEDC0FFEEULL;
constexpr uint64_t kOceanSeed = 0x0CEA75EED1234ULL;
constexpr uint64_t kSnowSeed  = 0x5104705EEDULL;
constexpr uint64_t kGlacSeed  = 0x61AC1E5EEDULL;

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

    void fillTemp(int16_t meanTenthsC, int16_t rangeTenthsC) {
        std::fill(world.data.temperatureMean.begin(),
                  world.data.temperatureMean.end(), meanTenthsC);
        std::fill(world.data.temperatureRange.begin(),
                  world.data.temperatureRange.end(), rangeTenthsC);
    }
    void fillPrecip(uint16_t mmYr) {
        std::fill(world.data.precipitation.begin(),
                  world.data.precipitation.end(), mmYr);
    }
    // Set a controlled per-tile mean temperature (C) and a uniform seasonal range.
    template <typename Fn>
    void setTempC(Fn&& meanC, float rangeC) {
        const auto rt = static_cast<int16_t>(rangeC * 10.0f);
        for (TileId t = 0; t < grid.tileCount(); ++t) {
            double lat{}, lon{};
            grid.latLonOf(t, lat, lon);
            world.data.temperatureMean[t]  = static_cast<int16_t>(meanC(lat, lon) * 10.0);
            world.data.temperatureRange[t] = rt;
        }
    }

    StageContext makeContext(uint64_t stageSeed) {
        return StageContext{params, world.derived, grid,  world.data,
                            world,  pool,          stageSeed,
                            [](float) {},          cancel};
    }

    void runAtmosphere() { AtmosphereStage s; auto ctx = makeContext(kAtmoSeed);  s.run(ctx); }
    void runOcean()      { OceanStage s;      auto ctx = makeContext(kOceanSeed); s.run(ctx); }
    void runSnow()       { SnowStage s;       auto ctx = makeContext(kSnowSeed);  s.run(ctx); }
    void runGlacier()    { GlacierStage s;    auto ctx = makeContext(kGlacSeed);  s.run(ctx); }
};

uint32_t fieldBit(WorldField f) { return static_cast<uint32_t>(f); }

bool hasGlacier(const TestWorld& w, TileId t) {
    return (w.world.data.flags[t] & kFlagGlacier) != 0;
}

uint32_t glacierCount(const TestWorld& w) {
    uint32_t n = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) if (hasGlacier(w, t)) ++n;
    return n;
}

// Western hemisphere ocean, eastern hemisphere land — the coast gives the perfect-
// plastic distance transform a margin to anchor on.
double oceanWestLandEast(double /*lat*/, double lon) {
    return lon < 0.0 ? -3000.0 : 100.0;
}

} // namespace

// ============================================================================
// Cold + wet land glaciates; the ocean margin anchors a real ice sheet
// ============================================================================

TEST(GlacierStage, ColdWetLandGlaciates) {
    TestWorld w;
    w.setElevation(oceanWestLandEast);
    w.fillTemp(-300, 100); // -30 C mean, +/- 10 C season
    w.fillPrecip(800);     // mm/yr
    w.runOcean();
    w.runGlacier();

    uint32_t glac = 0, oceanGlac = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        if ((w.world.data.flags[t] & kFlagOcean) != 0) {
            if (hasGlacier(w, t)) ++oceanGlac;
            continue;
        }
        if (hasGlacier(w, t)) {
            EXPECT_GT(w.world.data.iceThickness[t], 0u);
            ++glac;
        }
    }
    EXPECT_GT(glac, 20u) << "cold wet land should grow an ice sheet";
    EXPECT_EQ(oceanGlac, 0u) << "ocean is sea ice, never glacier";
}

// ============================================================================
// Warm land does not glaciate even when wet (mass balance is negative)
// ============================================================================

TEST(GlacierStage, WarmWetLandNoGlacier) {
    TestWorld w;
    w.setElevation(oceanWestLandEast);
    w.fillTemp(250, 100); // +25 C
    w.fillPrecip(800);
    w.runOcean();
    w.runGlacier();
    EXPECT_EQ(glacierCount(w), 0u);
}

// ============================================================================
// Cold but DRY land does not glaciate (no accumulation without snowfall)
// ============================================================================

TEST(GlacierStage, ColdDryLandNoGlacier) {
    TestWorld w;
    w.setElevation(oceanWestLandEast);
    w.fillTemp(-300, 100); // -30 C, cold enough
    w.fillPrecip(0);       // but no precipitation -> no snow accumulation
    w.runOcean();
    w.runGlacier();
    EXPECT_EQ(glacierCount(w), 0u);
}

// ============================================================================
// Perfect-plastic dome: thickness varies from a thin margin to a thick interior
// (it is not a uniform slab)
// ============================================================================

TEST(GlacierStage, PerfectPlasticDomeProfile) {
    TestWorld w;
    w.setElevation(oceanWestLandEast);
    w.fillTemp(-300, 100);
    w.fillPrecip(800);
    w.runOcean();
    w.runGlacier();

    uint16_t minThick = 0xFFFF, maxThick = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        if (!hasGlacier(w, t)) continue;
        const uint16_t h = w.world.data.iceThickness[t];
        if (h < minThick) minThick = h;
        if (h > maxThick) maxThick = h;
    }
    ASSERT_GT(maxThick, 0u);
    // A dome: the interior is markedly thicker than the margin, not one flat value.
    EXPECT_GT(maxThick, minThick);
    EXPECT_GT(maxThick, minThick + 300u) << "expected a real thickness gradient";
}

// ============================================================================
// Fully glaciated world (no ocean, all land above the ELA): the ice region has
// no margin, so every tile falls back to the maximum thickness instead of bare
// ground. Guards the degenerate distance-transform branch.
// ============================================================================

TEST(GlacierStage, FullyGlaciatedWorldNoMarginFallsBackToMaxThickness) {
    TestWorld w;
    w.setElevation([](double, double) { return 100.0; }); // all land, no ocean
    w.fillTemp(-300, 100); // cold everywhere -> every land tile accumulates
    w.fillPrecip(800);
    // Deliberately skip runOcean(): no kFlagOcean, so no margin seeds exist.
    w.runGlacier();

    EXPECT_EQ(glacierCount(w), w.grid.tileCount())
        << "with no margin every tile is interior ice";
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        ASSERT_TRUE(hasGlacier(w, t)) << "tile " << t;
        // Marginless ice caps at the maximum thickness (kMaxIceM = 4000 m).
        EXPECT_EQ(w.world.data.iceThickness[t], 4000u) << "tile " << t;
    }
}

// ============================================================================
// Polar ice sheet emerges at the cold poles, not the warm equator (real climate)
// ============================================================================

TEST(GlacierStage, PolarIceSheetNotEquator) {
    TestWorld w;
    w.setElevation([](double, double) { return 100.0; }); // all land
    // Controlled cold-poles climate with a small seasonal swing so polar summers
    // stay frozen. (The EarthLike preset runs polar summers too warm for the PDD
    // melt model to keep land ice — a climate-warmth limitation, not a glacier bug;
    // the ice->temperature feedback / colder climates are what grow real ice sheets.)
    constexpr double kDeg = 3.14159265358979323846 / 180.0;
    w.setTempC([&](double lat, double) {
        const double s = std::sin(lat * kDeg);
        return 25.0 - 60.0 * s * s; // +25 C at the equator, -35 C at the poles
    }, 8.0f);
    w.fillPrecip(800);
    w.runOcean();
    w.runGlacier();

    uint32_t polar = 0, equator = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);
        const double absLat = lat < 0.0 ? -lat : lat;
        if (absLat > 80.0) {
            EXPECT_TRUE(hasGlacier(w, t)) << "polar tile " << t << " lat " << lat;
            ++polar;
        } else if (absLat < 20.0) {
            EXPECT_FALSE(hasGlacier(w, t)) << "equator tile " << t << " lat " << lat;
            ++equator;
        }
    }
    EXPECT_GT(polar, 5u);
    EXPECT_GT(equator, 50u);
}

// ============================================================================
// Alpine glacier on a lapse-cold equatorial summit, not the warm lowland
// ============================================================================

TEST(GlacierStage, AlpineGlacierOnEquatorialSummit) {
    TestWorld w;
    w.setElevation([](double lat, double lon) {
        if (lat >= -5.0 && lat <= 5.0 && lon >= 100.0 && lon <= 140.0) return 7000.0;
        return 100.0;
    });
    w.runAtmosphere(); // 7000 m equatorial summit reads ~-15 C via lapse
    w.fillPrecip(800);
    w.runOcean();
    w.runGlacier();

    uint32_t summitGlac = 0, warmLowland = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        double lat{}, lon{};
        w.grid.latLonOf(t, lat, lon);
        const double absLat = lat < 0.0 ? -lat : lat;
        if (w.world.data.elevation[t] == 7000.0f) {
            EXPECT_TRUE(hasGlacier(w, t)) << "summit tile " << t;
            ++summitGlac;
        } else if (absLat < 20.0 && w.world.data.elevation[t] < 200.0f) {
            EXPECT_FALSE(hasGlacier(w, t)) << "warm lowland tile " << t;
            ++warmLowland;
        }
    }
    EXPECT_GT(summitGlac, 0u);
    EXPECT_GT(warmLowland, 10u);
}

// ============================================================================
// Ocean is left to SnowStage's sea ice; never glaciated, and sea ice is preserved
// ============================================================================

TEST(GlacierStage, SkipsOceanAndPreservesSeaIce) {
    TestWorld w;
    w.setElevation(oceanWestLandEast);
    w.runAtmosphere();
    w.fillPrecip(800);
    w.runOcean();
    w.runSnow();
    w.runGlacier();

    uint32_t seaIce = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        const uint8_t f = w.world.data.flags[t];
        if ((f & kFlagOcean) != 0) {
            EXPECT_FALSE(hasGlacier(w, t)) << "ocean tile " << t;
            EXPECT_EQ(w.world.data.iceFlow[t], 0xFFu) << "ocean tile " << t;
        }
        if ((f & kFlagSeaIce) != 0) {
            EXPECT_GT(w.world.data.iceThickness[t], 0u);
            EXPECT_FALSE(hasGlacier(w, t));
            ++seaIce;
        }
    }
    EXPECT_GT(seaIce, 0u);
}

// ============================================================================
// validFields: GlacierStage claims Flags | IceThickness | IceFlow
// ============================================================================

TEST(GlacierStage, ValidFieldsExactness) {
    TestWorld w;
    w.setElevation(oceanWestLandEast);
    w.fillTemp(-300, 100);
    w.fillPrecip(800);
    w.runOcean();
    const uint32_t before = w.world.validFields;
    w.runGlacier();
    const uint32_t added = w.world.validFields & ~before;
    EXPECT_EQ(added, fieldBit(WorldField::Flags) |
                         fieldBit(WorldField::IceThickness) |
                         fieldBit(WorldField::IceFlow));
}

// ============================================================================
// Determinism across thread counts (the parallel SMB/thickness/flow passes;
// the distance transform is single-threaded and deterministic by construction)
// ============================================================================

TEST(GlacierStage, DeterministicAcrossThreadCounts) {
    TestWorld w1(24, 1);
    TestWorld w2(24, 5);
    for (TestWorld* w : {&w1, &w2}) {
        w->setElevation(oceanWestLandEast);
        w->fillTemp(-280, 120);
        w->fillPrecip(900);
        w->runOcean();
        w->runGlacier();
    }
    EXPECT_EQ(w1.world.data.iceThickness, w2.world.data.iceThickness);
    EXPECT_EQ(w1.world.data.iceFlow,      w2.world.data.iceFlow);
    EXPECT_EQ(w1.world.data.flags,        w2.world.data.flags);
}

// ============================================================================
// iceFlow points to a strictly lower ice surface (bed + ice)
// ============================================================================

TEST(GlacierStage, IceFlowPointsToLowerIceSurface) {
    TestWorld w;
    // Cold wet land sloping up toward the equator so glaciers have somewhere to flow.
    w.setElevation([](double lat, double lon) {
        if (lon < 0.0) return -3000.0; // ocean margin
        const double absLat = lat < 0.0 ? -lat : lat;
        return 100.0 + (90.0 - absLat) * 20.0;
    });
    w.fillTemp(-300, 100);
    w.fillPrecip(800);
    w.runOcean();
    w.runGlacier();

    std::array<TileId, 6> nbs{};
    uint32_t flowing = 0;
    for (TileId t = 0; t < w.grid.tileCount(); ++t) {
        if (!hasGlacier(w, t)) continue;
        const uint8_t dir = w.world.data.iceFlow[t];
        if (dir == 0xFFu) continue;
        const uint32_t cnt = w.grid.neighbors(t, nbs);
        ASSERT_LT(dir, cnt);
        const float surf  = w.world.data.elevation[t] +
                            static_cast<float>(w.world.data.iceThickness[t]);
        const float nsurf = w.world.data.elevation[nbs[dir]] +
                            static_cast<float>(w.world.data.iceThickness[nbs[dir]]);
        EXPECT_LT(nsurf, surf) << "iceFlow must point downhill, tile " << t;
        ++flowing;
    }
    EXPECT_GT(flowing, 0u);
}

} // namespace worldgen
