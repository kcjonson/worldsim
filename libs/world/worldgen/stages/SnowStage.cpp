// STUB stage: replaced in M3e — produces placeholder data so downstream tracks can build.

#include "worldgen/stages/SnowStage.h"

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
constexpr float kPermanentSnowThresholdC = -10.0f;
} // namespace

void SnowStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            if ((ctx.data.flags[t] & kFlagOcean) != 0) continue;

            float tempC = static_cast<float>(ctx.data.temperatureMean[t]) * 0.1f;
            if (tempC < kPermanentSnowThresholdC) {
                ctx.data.flags[t]    |= kFlagPermanentSnow;
                float coldness = (-tempC - 10.0f) / 50.0f;
                if (coldness > 1.0f) coldness = 1.0f;
                ctx.data.snowCover[t] = static_cast<uint8_t>(coldness * 255.0f);
            }
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    // SnowStage is the last writer of flags (adds kFlagPermanentSnow after OceanStage
    // added kFlagOcean). It owns the Flags valid bit; no stage may write flags afterward.
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Flags);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::SnowCover);
    // FlowAccum and Downhill are stub-initialized by WorldData::allocate()
    // (0.0 and 0xFF respectively) and owned by the M3 precipitation stage;
    // claim the bits here so the pipeline's AllFieldsValid invariant holds.
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::FlowAccum);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Downhill);
}

} // namespace worldgen
