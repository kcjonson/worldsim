// STUB stage: replaced in M3e — produces placeholder data so downstream tracks can build.

#include "worldgen/stages/BiomeStage.h"

#include "worldgen/data/Biome.h"

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
} // namespace

void BiomeStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            if ((ctx.data.flags[t] & kFlagOcean) != 0) {
                ctx.data.biome[t] = static_cast<uint8_t>(Biome::Ocean);
                continue;
            }

            float tempC  = static_cast<float>(ctx.data.temperatureMean[t]) * 0.1f;
            float precip = static_cast<float>(ctx.data.precipitation[t]);

            Biome biome = Biome::TemperateGrassland;

            if (tempC > 20.0f) {
                if (precip > 2000.0f) biome = Biome::TropicalRainforest;
                else if (precip > 1000.0f) biome = Biome::TropicalSeasonalForest;
                else if (precip > 500.0f)  biome = Biome::TropicalSavanna;
                else biome = Biome::HotDesert;
            } else if (tempC > 5.0f) {
                if (precip > 1400.0f) biome = Biome::TemperateRainforest;
                else if (precip > 750.0f) biome = Biome::TemperateDeciduousForest;
                else if (precip > 500.0f) biome = Biome::TemperateGrassland;
                else biome = Biome::SemiDesert;
            } else if (tempC > -5.0f) {
                if (precip > 300.0f) biome = Biome::BorealForest;
                else biome = Biome::ColdDesert;
            } else {
                if (precip > 150.0f) biome = Biome::ArcticTundra;
                else biome = Biome::PolarDesert;
            }

            ctx.data.biome[t] = static_cast<uint8_t>(biome);
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Biome);
}

} // namespace worldgen
