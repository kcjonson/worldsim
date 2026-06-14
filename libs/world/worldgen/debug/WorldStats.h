#pragma once

#include "worldgen/data/Biome.h"
#include "worldgen/data/GeneratedWorld.h"

#include <array>
#include <cstdint>
#include <vector>

namespace worldgen {

// Per-plate area stats (tile counts, sorted descending by area).
struct PlateAreaStats {
    std::vector<uint32_t> sortedAreas; // tile counts, descending
    float largestToSmallestRatio{};    // sortedAreas[0] / sortedAreas.back()
    float logAreaRankR2{};             // R^2 of linear fit: log(area) vs rank
};

// One connected continent (land component >= 0.5% of N).
struct ContinentStats {
    uint32_t tileCount{};
    float    isoperimetricRatio{};     // P^2 / (4*pi*A), flat approximation
};

// One connected mountain belt (land tiles with elev > meanLand + 1500 m, component >= 32 tiles).
struct BeltStats {
    uint32_t tileCount{};
    float    elongation{};    // sqrt(lambda1 / lambda2) from PCA of unit-vector positions
    float    widthKm{};       // area / length, both derived from PCA extents

    // Geodesic aspect ratio (honest curved-belt linearity metric).
    float    lengthKm{};      // graph diameter in hops * tileKm (double-BFS approximation)
    float    geoWidthKm{};    // area / lengthKm (mean width along the arc)
    float    aspectRatio{};   // lengthKm / max(geoWidthKm, tileKm)
};

// Aggregated world statistics computed from a fully-generated world.
// All fields use plain numbers/vectors so the CLI can format them without
// pulling JSON or other serialization into the world library.
struct WorldStats {
    uint32_t tileCount{};
    float    oceanFraction{};  // tiles with kFlagOcean / N

    // Hypsometry: 256-bin histogram of elevation (uniform bins from minElev to maxElev).
    float             hypsoBinMin{};   // elevation at bin 0 lower edge (meters)
    float             hypsoBinWidth{}; // meters per bin
    std::vector<uint32_t> hypsoHist;  // 256 bins

    // Detected hypsometry modes (up to 2 largest peaks, by bin count).
    // modeElevations[0] = dominant mode, [1] = secondary (if bimodal).
    std::vector<float>    modeElevations; // meters (bin center)
    std::vector<uint32_t> modeCounts;
    float                 troughElevation{};  // minimum between the two largest modes (meters)
    float                 troughFraction{};   // troughCount / lower-mode count (< 1 means bimodal)

    PlateAreaStats plates;

    std::vector<BeltStats> belts;
    float                  medianBeltElongation{};
    float                  interiorMountainFraction{}; // fraction of belt tiles with boundaryDist > 500 km

    // Geodesic aspect-ratio medians.
    float                  medianAspectRatio{};         // plain median across belts >= 32 tiles
    float                  tileWeightedMedianAspectRatio{}; // headline: tile-count-weighted median

    std::vector<ContinentStats> continents;
    float                       medianIsoperimetric{};

    // Biome fractions over LAND tiles only (each value in [0,1]; sum ~1.0).
    // biomeFraction[i] = (tiles with biome i that are not ocean/lake) /
    //                    landTileCount.  Water biomes (Ocean, Lake) are
    //                    excluded from both numerator and denominator.
    std::array<float, static_cast<size_t>(Biome::Count)> biomeFraction{};
    float landTileCount{};

    // Fraction of continental-crust tiles (kFlagContinentalCrust) that sit
    // below sea level.  Measures how much shelf the terrain stage produced.
    float shelfSubmergedFraction{};

    // Water / drainage stats.  All fractions are over LAND tiles (tiles without
    // kFlagOcean) unless noted.  Captured pre-fix so W-1 has a before/after.
    float    riverTileFraction{};        // land tiles with kFlagRiver set (~kRiverLandFraction)
    uint32_t sinkTileCount{};           // land tiles with downhill == 0xFF (inland sinks)
    float    endorheicSinkFraction{};   // sinkTileCount / landTileCount
    float    lakeTileFraction{};        // tiles with kFlagLake set (0 pre-W1)
    float    maxFlowAccum{};            // max flowAccum across all land tiles
    float    meanFlowAccumLand{};       // mean flowAccum across land tiles
    float    landWithWaterNearbyFraction{}; // land tiles that are a river OR sink OR
                                            // have an ocean/lake neighbor
};

// Compute WorldStats from a completed GeneratedWorld.
// Single-threaded, deterministic.  Elevation and Flags fields must be valid.
WorldStats computeWorldStats(const GeneratedWorld& world);

} // namespace worldgen
