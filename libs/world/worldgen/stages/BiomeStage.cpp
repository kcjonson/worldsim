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
//   2. Wetland: T > 0 C, precip > 900 mm/yr, poor drainage (flowAccum >=
//      kRiverFlowThreshold or downhill == 0xFF inland sink), elevation
//      < 200 m above sea level -> TropicalWetland (T > 18 C) else
//      TemperateWetland
//   3. Beach: ocean neighbor, <= 30 m above sea level, T > 0 C
//   4. Elevation zonation (meters above sea level):
//        > 3500, or > 2500 with T < -2 C -> AlpineTundra
//        2500..3500: precip >= 250 -> AlpineGrassland, else ColdDesert
//        1200..2500: forested base biome -> MontaneForest
//   5. Whittaker base matrix (T in C, precip in mm/yr):
//        T > 20:  > 2000 TropicalRainforest | > 1000 TropicalSeasonalForest |
//                 > 500 TropicalSavanna | else HotDesert
//        5..20:   > 1400 TemperateRainforest | > 750 TemperateDeciduousForest |
//                 > 500 TemperateGrassland | > 250: T >= 12 XericShrubland
//                 else SemiDesert | <= 250: T >= 18 HotDesert else ColdDesert
//        -5..5:   > 300 BorealForest | else ColdDesert
//        <= -5:   > 150 ArcticTundra | else PolarDesert
//
// Also sets kFlagCoast on every non-water tile with at least one ocean
// neighbor, independent of the Beach biome. The Flags validFields bit stays
// owned by SnowStage; this stage claims only Biome.

#include "worldgen/stages/BiomeStage.h"

#include "worldgen/data/Biome.h"

#include <array>

namespace worldgen {

namespace {

constexpr size_t kGrainSize = 4096;

constexpr float kWetlandMinPrecipMmYr   = 900.0f;
constexpr float kWetlandMaxElevMeters   = 200.0f;
constexpr float kWetlandTropicalTempC   = 18.0f;
constexpr float kBeachMaxElevMeters     = 30.0f;
constexpr float kMontaneMinElevMeters   = 1200.0f;
constexpr float kAlpineGrassMinElevMeters  = 2500.0f;
constexpr float kAlpineTundraMinElevMeters = 3500.0f;
constexpr float kAlpineGrassMinPrecipMmYr  = 250.0f;
constexpr float kHighElevTundraTempC    = -2.0f;

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
        return tempC >= 18.0f ? Biome::HotDesert : Biome::ColdDesert;
    }
    if (tempC > -5.0f) {
        return precip > 300.0f ? Biome::BorealForest : Biome::ColdDesert;
    }
    return precip > 150.0f ? Biome::ArcticTundra : Biome::PolarDesert;
}

Biome classifyLand(float tempC, float precip, float elevAboveSea,
                   bool oceanNeighbor, float flowAccum, uint8_t downhill) {
    const bool poorDrainage =
        flowAccum >= kRiverFlowThreshold || downhill == 0xFFu;
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

    const Biome base = classifyBase(tempC, precip);
    if (elevAboveSea > kMontaneMinElevMeters && isForest(base)) {
        return Biome::MontaneForest;
    }
    return base;
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

            // Neighbor ocean-ness by elevation < seaLevel — the exact
            // predicate OceanStage applied. Slabs concurrently |= kFlagCoast
            // into their own tiles' flags, so reading a neighbor's flags byte
            // here would be a data race.
            std::array<TileId, 8> nbs{};
            const uint32_t cnt =
                ctx.grid.neighbors(static_cast<TileId>(t), nbs);
            bool oceanNeighbor = false;
            for (uint32_t k = 0; k < cnt; ++k) {
                if (ctx.data.elevation[nbs[k]] < seaLevel) {
                    oceanNeighbor = true;
                    break;
                }
            }
            if (oceanNeighbor) {
                ctx.data.flags[t] = tileFlags | kFlagCoast;
            }

            const float tempC =
                static_cast<float>(ctx.data.temperatureMean[t]) * 0.1f;
            const float precip = static_cast<float>(ctx.data.precipitation[t]);
            const float elevAboveSea = ctx.data.elevation[t] - seaLevel;

            ctx.data.biome[t] = static_cast<uint8_t>(
                classifyLand(tempC, precip, elevAboveSea, oceanNeighbor,
                             ctx.data.flowAccum[t], ctx.data.downhill[t]));
        }
        ctx.reportProgress(static_cast<float>(end) /
                           static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Biome);
}

} // namespace worldgen
