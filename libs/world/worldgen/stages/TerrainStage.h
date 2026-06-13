#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

class TerrainStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Terrain"; }
    float       weight() const override { return 0.25f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
