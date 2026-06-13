#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

class PlateStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Plates"; }
    float       weight() const override { return 0.10f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
