#include "worldgen/stages/TectonicHistoryStage.h"

#include "worldgen/tectonics/PlateSim.h"
#include "worldgen/tectonics/TectonicParams.h"

namespace worldgen {

void TectonicHistoryStage::run(StageContext& ctx) {
    tectonics::PlateSimParams sp;
    // Clamp the coarse grid to the full-res grid when fullN < kCoarseN (small-n
    // runs and unit-test grids). CrustStage then degenerates gracefully: when
    // coarseN == fullN the "upsampling" is a simple nearest-sample copy.
    const uint32_t fullN = ctx.params.gridSubdivision;
    sp.coarseN        = (tectonics::kCoarseN < fullN) ? tectonics::kCoarseN : fullN;
    sp.seed           = ctx.stageSeed;
    sp.plateCount     = ctx.params.tectonicPlateCount;
    sp.waterAmount    = ctx.params.waterAmount;
    sp.planetAge      = ctx.params.planetAge;
    sp.planetRadiusKm = ctx.derived.planetRadiusMeters / 1000.0;

    tectonics::PlateSim sim(sp);

    auto cancel   = [&]() { throwIfCancelled(ctx); };
    auto progress = [&](float f) { ctx.reportProgress(f); };

    auto history = sim.run(cancel, progress);
    ctx.world.tectonicHistory = history;
    ctx.reportProgress(1.0f);
}

} // namespace worldgen
