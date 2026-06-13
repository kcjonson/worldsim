#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

// Upsamples the coarse TectonicHistory to the full-res grid.
//
// Inputs (from ctx): TectonicHistory on ctx.world.tectonicHistory (set by TectonicHistoryStage).
// Outputs per full-res tile:
//   data.plateId          — 0..plateCount-1 (clamped; asserted != 255 in debug)
//   data.flags            — kFlagContinentalCrust set for continental tiles
//   data.crustAge         — Myr, u16 capped at 65534
//   data.orogenyAge       — Myr since last orogeny, u16; 65535 = never
// Also populates world.plates (PlateInfo list) from TectonicHistory::plates so that
// TerrainStage can compute Euler-pole velocities exactly as before.
//
// Algorithm: domain-warp + coarse nearest-sample for plateId/crustType;
// same-crust-type inverse-distance blend for crustAge; suture-guarded blend
// for orogenyAge. Fully parallelFor, pure function of tile id.
class CrustStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Crust"; }
    // Weight 0.15: takes 0.10 from the old TerrainStage (which drops 0.25->0.15
    // for now; M-T4 will reassign after the TerrainStage rewrite).
    float       weight() const override { return 0.15f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
