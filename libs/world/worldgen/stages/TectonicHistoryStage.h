#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

// Provisional stage: runs the coarse PlateSim and attaches its TectonicHistory
// product to the world. NOT yet in PlanetGenerator's stage list (the old
// PlateStage/PlateMovementStage pipeline keeps running until M-T3). Exists so the
// sim can be driven through the standard stage harness (cancel + progress).
class TectonicHistoryStage : public IGenerationStage {
  public:
    const char* name()   const override { return "TectonicHistory"; }
    float       weight() const override { return 0.10f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
