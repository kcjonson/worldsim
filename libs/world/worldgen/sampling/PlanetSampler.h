#pragma once

// Samples the 3D generated planet at 2D gameplay positions (meters, origin at
// the landing site). Pure worldgen-side: no engine types here; the engine
// adapter (GeneratedWorldSampler) converts PositionSample to its own types.
//
// Unit contract: GeneratedWorld elevations are meters relative to mean radius
// with sea level at GeneratedWorld::seaLevelMeters; all elevations returned
// here are meters above sea level. Water positions (ocean/lake-flagged tiles,
// or land below sea level) report elevation <= 0.

#include "worldgen/data/Biome.h"
#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/sampling/SphericalProjection.h"

#include <array>
#include <cstdint>
#include <memory>

namespace worldgen {

class PlanetSampler {
  public:
    struct BiomeWeight {
        Biome biome{Biome::Ocean};
        float weight{};
    };

    // At most two biomes contribute: the containing tile plus, within the
    // 500 m blend band at spherical tile boundaries, the nearest neighbor.
    // weights[0] is always the containing tile's biome; weights sum to 1.
    struct PositionSample {
        std::array<BiomeWeight, 2> weights{};
        uint32_t weightCount{};
        float elevationMeters{};  // meters above sea level (<= 0 when water)
        bool water{};
        TileId tile{kInvalidTile};
    };

    // Requires Elevation, Biome, and Flags valid in world->validFields.
    PlanetSampler(std::shared_ptr<const GeneratedWorld> generatedWorld,
                  double landingLatDeg, double landingLonDeg);

    [[nodiscard]] PositionSample sampleAt(double xMeters, double yMeters) const;

    // Elevation in meters above sea level (boundary-blended like sampleAt).
    [[nodiscard]] float elevationAt(double xMeters, double yMeters) const;

    // Worldgen tile containing a 2D position (via SphereGrid::fromUnitVector).
    [[nodiscard]] TileId tileAt(double xMeters, double yMeters) const;

    [[nodiscard]] uint64_t seed() const { return world->params.seed; }

  private:
    struct TileSample {
        Biome biome{};
        float elevationMeters{};
        bool water{};
    };

    [[nodiscard]] TileSample resolveTile(TileId t) const;

    std::shared_ptr<const GeneratedWorld> world;
    SphericalProjection projection;
};

} // namespace worldgen
