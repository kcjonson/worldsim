#pragma once

#include "worldgen/pipeline/GenerationStage.h"

namespace worldgen {

// Fluvial erosion: carves valleys into the continental terrain so rivers (rendered
// later from the climate drainage) land in real valleys instead of running across flat
// platforms. Detachment-limited stream-power incision solved with the Braun & Willett
// (2013) implicit O(n) scheme, on a PROVISIONAL uniform-rainfall drainage; the real
// climate-weighted drainage is recomputed downstream by PrecipitationStage on the carved
// terrain. Runs between Terrain and Atmosphere so climate, biomes, and the final drainage
// all see the dissected terrain. Single-threaded and deterministic (drainage-stack order).
class ErosionStage : public IGenerationStage {
  public:
    const char* name()   const override { return "Erosion"; }
    float       weight() const override { return 0.12f; }
    void        run(StageContext& ctx) override;
};

} // namespace worldgen
