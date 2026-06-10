#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

class PrecipitationStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Precipitation"; }
    float       weight() const override { return 0.20f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
