// STUB stage: replaced in M3a — produces placeholder data so downstream tracks can build.

#include "worldgen/stages/PlateMovementStage.h"

#include <random/HashNoise.h>

namespace worldgen {

namespace {
constexpr size_t kGrainSize = 4096;
}

void PlateMovementStage::run(StageContext& ctx) {
    // Plate velocities are implicit via eulerPole + angularSpeed already set by PlateStage.
    // Boundary type is a stub: tiles whose neighbors have different plateId get type 1.
    const uint32_t totalTiles = ctx.grid.tileCount();

    ctx.pool.parallelFor(0, totalTiles, kGrainSize, [&](size_t begin, size_t end) {
        throwIfCancelled(ctx);
        std::array<uint32_t, 8> nbrs{};
        for (size_t t = begin; t < end; ++t) {
            uint32_t count = ctx.grid.neighbors(static_cast<uint32_t>(t), nbrs);
            uint8_t pid = ctx.data.plateId[t];
            uint8_t btype = 0;
            for (uint32_t k = 0; k < count; ++k) {
                if (ctx.data.plateId[nbrs[k]] != pid) { btype = 1; break; }
            }
            ctx.data.boundaryType[t] = btype;
        }
        ctx.reportProgress(static_cast<float>(end) / static_cast<float>(totalTiles));
    });

    ctx.world.validFields |= static_cast<uint32_t>(WorldField::BoundaryType);
    ctx.world.validFields |= static_cast<uint32_t>(WorldField::BoundaryDistance);
}

} // namespace worldgen
