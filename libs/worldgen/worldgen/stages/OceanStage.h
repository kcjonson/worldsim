#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

class OceanStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Ocean"; }
    float       weight() const override { return 0.05f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
