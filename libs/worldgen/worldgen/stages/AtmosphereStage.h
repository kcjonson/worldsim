#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

class AtmosphereStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Atmosphere"; }
    float       weight() const override { return 0.15f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
