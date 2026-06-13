#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

class TerrainStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Terrain"; }
    // Weight reduced from 0.25 to 0.15; the 0.10 moved to CrustStage (M-T3).
    // Will be reassigned in M-T4 after the full TerrainStage rewrite.
    float       weight() const override { return 0.15f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
