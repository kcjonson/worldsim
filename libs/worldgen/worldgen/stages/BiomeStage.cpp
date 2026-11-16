// BiomeStage — M3e implementation (pipeline phase P7).
//
// Whittaker-style temperature x precipitation classification with elevation
// zonation, drainage-driven wetlands, and coastal biomes. Effective
// temperature is temperatureMean * 0.1 C; AtmosphereStage already applied the
// lapse rate on land, so elevation here shapes structure (zonation bands),
// never re-cools the tile.
//
// Per-tile decision order (first match wins):
//   1. kFlagOcean -> Ocean; kFlagLake -> Lake
//   2. Wetland: T > 0 C, precip > 900 mm/yr, poor drainage (inland sink
//      downhill == 0xFF, OR flat-and-low: localRelief < kFlatReliefM &&
//      elevAboveSea < kWetlandMaxElevMeters), elevation < 200 m above sea
//      level -> TropicalWetland (T > 18 C) else TemperateWetland
//   3. Beach: ocean neighbor, <= 50 m above sea level, T > 0 C
//   4. Elevation zonation (meters above sea level):
//        > 3500, or > 2500 with T < -2 C -> AlpineTundra
//        2500..3500: precip >= 250 -> AlpineGrassland, else ColdDesert
//        1200..2500: T > 3 C and precip > 400 mm -> MontaneForest (decoupled
//                    from the lowland base; the slope's own climate decides),
//                    else fall through to the base matrix
//   5. Whittaker base matrix (T in C, precip in mm/yr):
//        T > 20:  > 2000 TropicalRainforest | > 1000 TropicalSeasonalForest |
//                 > 500 TropicalSavanna | else HotDesert
//        5..20:   > 1400 TemperateRainforest | > 750 TemperateDeciduousForest |
//                 > 500 TemperateGrassland | > 250: T >= 12 XericShrubland
//                 else SemiDesert | <= 250: T >= 12 HotDesert else ColdDesert
//        -10..5:  > 300 BorealForest | else ColdDesert
//        <= -10:  > 150 ArcticTundra | else PolarDesert
//
// kFlagCoast has been removed; BiomeStage computes oceanNeighbor locally per
// tile for the Beach decision and does not store it. The Flags validFields bit
// stays owned by SnowStage; this stage claims only Biome.

#include "worldgen/stages/BiomeStage.h"

#include "worldgen/data/Biome.h"

#include <array>

namespace worldgen {

namespace {

constexpr size_t kGrainSize = 4096;

constexpr float kWetlandMinPrecipMmYr   = 900.0f;
constexpr float kWetlandMaxElevMeters   = 200.0f;
constexpr float kWetlandTropicalTempC   = 18.0f;
// Flat local relief threshold for wetland poor-drainage predicate (meters).
// A tile is considered flat when max-neighbor-elev minus min-neighbor-elev
// falls below this value; combined with low elevation it indicates a poor-
// drainage depression rather than a well-drained hillslope.
constexpr float kFlatReliefM            = 40.0f;
constexpr float kBeachMaxElevMeters     = 50.0f;
constexpr float kMontaneMinElevMeters   = 1200.0f;
constexpr float kAlpineGrassMinElevMeters  = 2500.0f;
constexpr float kAlpineTundraMinElevMeters = 3500.0f;
constexpr float kAlpineGrassMinPrecipMmYr  = 250.0f;
constexpr float kHighElevTundraTempC    = -2.0f;
// Montane forest is assigned in the 1200-2500 m band from the TILE'S OWN
// climate, not the lowland base biome. A warm-enough, wet-enough mid-elevation
// slope grows forest even when the lowland below it is desert or tundra; this
// gives the mid-latitude belts a real montane-forest flank instead of jumping
// from lowland tundra straight to bare alpine rock.
constexpr float kMontaneMinTempC        = 3.0f;
constexpr float kMontaneMinPrecipMmYr   = 400.0f;

Biome classifyBase(float tempC, float precip) {
    if (tempC > 20.0f) {
        if (precip > 2000.0f) return Biome::TropicalRainforest;
        if (precip > 1000.0f) return Biome::TropicalSeasonalForest;
        if (precip > 500.0f)  return Biome::TropicalSavanna;
        return Biome::HotDesert;
    }
    if (tempC > 5.0f) {
        if (precip > 1400.0f) return Biome::TemperateRainforest;
        if (precip > 750.0f)  return Biome::TemperateDeciduousForest;
        if (precip > 500.0f)  return Biome::TemperateGrassland;
        if (precip > 250.0f) {
            return tempC >= 12.0f ? Biome::XericShrubland : Biome::SemiDesert;
        }
        // Dry tail of the warm/temperate band. Warm subtropical interiors
        // (C-2 dries them, lower contrastA warms them) read HotDesert; the
        // floor is 12 C (was 18) because this coarse model runs the subtropics
        // cool, so the 12-20 C dry subtropics that should be Sahara/Arabian/
        // Australian analogs stop landing in ColdDesert. Below 12 C, where
        // winters genuinely freeze, it stays a cold desert.
        return tempC >= 12.0f ? Biome::HotDesert : Biome::ColdDesert;
    }
    // Boreal/taiga band widened down to -10 C (was -5) so it occupies the
    // ~50-70 deg belt instead of yielding to tundra at ~55 deg. Dry sub-freezing
    // interiors are cold desert.
    if (tempC > -10.0f) {
        return precip > 300.0f ? Biome::BorealForest : Biome::ColdDesert;
    }
    // ArcticTundra floor lowered to -10 C (was -5): the -5 C cutoff made tundra
    // the default for the entire 55-90 deg cap. Polar desert below the dry bar.
    return precip > 150.0f ? Biome::ArcticTundra : Biome::PolarDesert;
}

Biome classifyLand(float tempC, float precip, float elevAboveSea,
                   bool oceanNeighbor, uint8_t downhill, float localRelief) {
    // Poor drainage: inland sinks (no downhill path), or flat low terrain
    // where water pools. High flowAccum means a river trunk — well drained,
    // not a swamp — so we do NOT use it here.
    const bool poorDrainage =
        (downhill == 0xFFu) ||
        (localRelief < kFlatReliefM && elevAboveSea < kWetlandMaxElevMeters);
    if (tempC > 0.0f && precip > kWetlandMinPrecipMmYr && poorDrainage &&
        elevAboveSea < kWetlandMaxElevMeters) {
        return tempC > kWetlandTropicalTempC ? Biome::TropicalWetland
                                             : Biome::TemperateWetland;
    }

    if (oceanNeighbor && elevAboveSea <= kBeachMaxElevMeters && tempC > 0.0f) {
        return Biome::Beach;
    }

    if (elevAboveSea > kAlpineTundraMinElevMeters ||
        (elevAboveSea > kAlpineGrassMinElevMeters &&
         tempC < kHighElevTundraTempC)) {
        return Biome::AlpineTundra;
    }
    if (elevAboveSea > kAlpineGrassMinElevMeters) {
        return precip >= kAlpineGrassMinPrecipMmYr ? Biome::AlpineGrassland
                                                   : Biome::ColdDesert;
    }

    // Montane band (1200-2500 m): decoupled from the lowland base. The slope's
    // own temperature and precipitation decide whether forest grows here, so a
    // desert or tundra lowland still gets a montane-forest flank where the
    // mid-elevation climate supports it. Too cold or too dry falls through to
    // the lowland classifier (cold-desert / steppe slopes).
    if (elevAboveSea > kMontaneMinElevMeters &&
        tempC > kMontaneMinTempC && precip > kMontaneMinPrecipMmYr) {
        return Biome::MontaneForest;
    }
    return classifyBase(tempC, precip);
}

} // namespace

void BiomeStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();
    const float seaLevel = ctx.world.seaLevelMeters;

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            const uint8_t tileFlags = ctx.data.flags[t];

            if ((tileFlags & kFlagOcean) != 0) {
                ctx.data.biome[t] = static_cast<uint8_t>(Biome::Ocean);
                continue;
            }
            if ((tileFlags & kFlagLake) != 0) {
                ctx.data.biome[t] = static_cast<uint8_t>(Biome::Lake);
                continue;
            }

            // Scan neighbors: detect ocean adjacency (for Beach) and compute
            // local relief (max - min neighbor elevation, for wetland test).
            // Uses elevation rather than kFlagOcean/kFlagCoast because slabs
            // run in parallel — reading a neighbor's flags byte written by
            // another slab would be a data race.
            std::array<TileId, 6> nbs{};
            const uint32_t cnt =
                ctx.grid.neighbors(static_cast<TileId>(t), nbs);
            bool oceanNeighbor = false;
            float minNbElev =  1e9f;
            float maxNbElev = -1e9f;
            for (uint32_t k = 0; k < cnt; ++k) {
                const float nbElev = ctx.data.elevation[nbs[k]];
                if (nbElev < seaLevel) oceanNeighbor = true;
                if (nbElev < minNbElev) minNbElev = nbElev;
                if (nbElev > maxNbElev) maxNbElev = nbElev;
            }
            const float localRelief = (cnt > 0) ? (maxNbElev - minNbElev) : 0.0f;

            const float tempC =
                static_cast<float>(ctx.data.temperatureMean[t]) * 0.1f;
            const float precip = static_cast<float>(ctx.data.precipitation[t]);
            const float elevAboveSea = ctx.data.elevation[t] - seaLevel;

            ctx.data.biome[t] = static_cast<uint8_t>(
                classifyLand(tempC, precip, elevAboveSea, oceanNeighbor,
                             ctx.data.downhill[t], localRelief));
        }
        ctx.reportProgress(static_cast<float>(end) /
                           static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Biome);
}

} // namespace worldgen
