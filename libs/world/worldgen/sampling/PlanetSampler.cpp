#include "worldgen/sampling/PlanetSampler.h"

#include <algorithm>
#include <cassert>

namespace worldgen {

namespace {

// Biome blend half-width at spherical tile boundaries (3d-to-2d-sampling.md).
constexpr float kBlendDistanceMeters = 500.0f;

double vecDot(Vec3d a, Vec3d b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

} // namespace

PlanetSampler::PlanetSampler(std::shared_ptr<const GeneratedWorld> generatedWorld,
                             double landingLatDeg, double landingLonDeg)
    : world(std::move(generatedWorld)),
      projection(world->derived.planetRadiusMeters, landingLatDeg, landingLonDeg) {
    constexpr uint32_t kRequiredFields =
        static_cast<uint32_t>(WorldField::Elevation) |
        static_cast<uint32_t>(WorldField::Biome) |
        static_cast<uint32_t>(WorldField::Flags);
    assert(world->grid != nullptr);
    assert((world->validFields & kRequiredFields) == kRequiredFields);
}

PlanetSampler::TileSample PlanetSampler::resolveTile(TileId t) const {
    const WorldData& data = world->data;
    uint8_t tileFlags = data.flags[t];
    float elevation = data.elevation[t] - world->seaLevelMeters;
    bool water = (tileFlags & (kFlagOcean | kFlagLake)) != 0 || elevation < 0.0f;

    Biome biome{};
    if ((tileFlags & kFlagOcean) != 0) {
        biome = Biome::Ocean;
    } else if ((tileFlags & kFlagLake) != 0) {
        biome = Biome::Lake;
    } else if (water) {
        // Unflagged land below sea level still samples as ocean.
        biome = Biome::Ocean;
    } else {
        biome = static_cast<Biome>(data.biome[t]);
    }

    // Water surfaces sit at or below gameplay sea level (e.g. lakes whose
    // surface elevation exceeds the planet's sea level).
    if (water && elevation > 0.0f) elevation = 0.0f;

    return {biome, elevation, water};
}

PlanetSampler::PositionSample PlanetSampler::sampleAt(double xMeters, double yMeters) const {
    const SphereGrid& grid = *world->grid;

    LatLon latLon = projection.worldToLatLon(xMeters, yMeters);
    TileId tile = kInvalidTile;
    float u = 0.0f;
    float v = 0.0f;
    grid.locate(latLon.latDeg, latLon.lonDeg, tile, u, v);

    TileSample primary = resolveTile(tile);

    float edgeFraction = std::min(std::min(u, 1.0f - u), std::min(v, 1.0f - v));
    float distToBoundary = edgeFraction * grid.tileWidthMeters(tile, world->derived.planetRadiusMeters);

    PositionSample result;
    result.tile = tile;
    result.water = primary.water;

    if (distToBoundary >= kBlendDistanceMeters) {
        result.weights[0] = {primary.biome, 1.0f};
        result.weightCount = 1;
        result.elevationMeters = primary.elevationMeters;
        return result;
    }

    // Boundary path: blend with the tile across the nearest edge — the
    // neighbor whose center is closest in direction to the query point.
    Vec3d dir = projection.worldToUnitVector(xMeters, yMeters);
    std::array<TileId, 8> neighborIds{};
    uint32_t neighborCount = grid.neighbors(tile, neighborIds);

    TileId nearestNeighbor = tile;
    double bestDot = -2.0;
    for (uint32_t i = 0; i < neighborCount; ++i) {
        double d = vecDot(grid.tileCenter(neighborIds[i]), dir);
        if (d > bestDot) {
            bestDot = d;
            nearestNeighbor = neighborIds[i];
        }
    }

    TileSample secondary = resolveTile(nearestNeighbor);

    // Continuous across the boundary: 50/50 on the edge itself, pure at the
    // blend distance. (The spec pseudocode assigns the primary t = d/blend,
    // which is discontinuous at the edge; this is the continuous reading.)
    float t = distToBoundary / kBlendDistanceMeters;
    float primaryWeight = 0.5f * (1.0f + t);
    float secondaryWeight = 1.0f - primaryWeight;

    result.elevationMeters = primary.elevationMeters * primaryWeight +
                             secondary.elevationMeters * secondaryWeight;
    if (result.water && result.elevationMeters > 0.0f) result.elevationMeters = 0.0f;

    if (secondary.biome == primary.biome) {
        result.weights[0] = {primary.biome, 1.0f};
        result.weightCount = 1;
    } else {
        result.weights[0] = {primary.biome, primaryWeight};
        result.weights[1] = {secondary.biome, secondaryWeight};
        result.weightCount = 2;
    }
    return result;
}

float PlanetSampler::elevationAt(double xMeters, double yMeters) const {
    return sampleAt(xMeters, yMeters).elevationMeters;
}

TileId PlanetSampler::tileAt(double xMeters, double yMeters) const {
    return world->grid->fromUnitVector(projection.worldToUnitVector(xMeters, yMeters));
}

} // namespace worldgen
