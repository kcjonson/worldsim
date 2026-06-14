#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

// Stage 0: runs the coarse PlateSim and attaches its TectonicHistory product to
// the world. Drives cancel + progress through the standard stage harness.
class TectonicHistoryStage : public IGenerationStage {
  public:
    const char* name()   const override { return "TectonicHistory"; }
    float       weight() const override { return 0.05f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
