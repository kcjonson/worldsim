// STUB stage: replaced in M3e — produces placeholder data so downstream tracks can build.

#include "worldgen/stages/OceanStage.h"

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
} // namespace

void OceanStage::run(StageContext& ctx) {
    const uint32_t totalTiles = ctx.grid.tileCount();
    const float seaLevel = ctx.world.seaLevelMeters;

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        for (size_t t = begin; t < end; ++t) {
            float elev = ctx.data.elevation[t];
            if (elev < seaLevel) {
                ctx.data.flags[t] |= kFlagOcean;
                float depth = seaLevel - elev;
                uint32_t depthU = static_cast<uint32_t>(depth);
                ctx.data.waterDepth[t] = static_cast<uint16_t>(depthU < 65535 ? depthU : 65535);
            }
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Flags);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::WaterDepth);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::FlowAccum);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::Downhill);
}

} // namespace worldgen
