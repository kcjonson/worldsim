#pragma once

#include "GeneratedWorld.h"
#include <functional>

namespace worldgen {

struct PlanetGeneratorParams {
    int seed{42};
    uint32_t subdivision{256}; // tiles per rhombus edge
    float seaLevel{0.0F};      // normalised elevation; 0 = halfway
    float radius{6371000.0F};
};

// Stub planet generator.  Produces plausible-looking placeholder data
// sufficient for M3f visual verification without real tectonics/climate.
class PlanetGenerator {
  public:
    using ProgressCallback = std::function<void(float progress, const char* status)>;

    // Synchronous generation.  progress callback is optional.
    GeneratedWorld generate(
        const PlanetGeneratorParams& params,
        ProgressCallback progress = nullptr
    );
};

} // namespace worldgen
