#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

class BiomeStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Biome"; }
    float       weight() const override { return 0.15f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
