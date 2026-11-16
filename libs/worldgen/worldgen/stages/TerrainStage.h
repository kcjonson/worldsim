#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

class TerrainStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Terrain"; }
    // M-T4: elevation synthesis (isostasy + depth-age + orogeny-aged ridged belts +
    // active-boundary kernels). The 0.10 boundary/BFS share moved to CrustStage in M-T3.
    float       weight() const override { return 0.20f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
