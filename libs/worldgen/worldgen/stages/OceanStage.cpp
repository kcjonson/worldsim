// OceanStage (pipeline phase P6): flag tiles below sea level as ocean and
// record water depth. seaLevelMeters was chosen by TerrainStage to hit the
// requested water fraction; this stage just materializes it per tile.
// Claims only WaterDepth — the Flags valid bit is owned by GlacierStage (last writer).

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

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::WaterDepth);
}

} // namespace worldgen
