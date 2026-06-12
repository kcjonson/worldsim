#include "worldgen/stages/TectonicHistoryStage.h"

#include "worldgen/tectonics/PlateSim.h"
#include "worldgen/tectonics/TectonicParams.h"

namespace worldgen {

void TectonicHistoryStage::run(StageContext& ctx) {
    tectonics::PlateSimParams sp;
    sp.coarseN        = tectonics::kCoarseN;
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
