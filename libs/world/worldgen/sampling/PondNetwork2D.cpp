#include "worldgen/sampling/PondNetwork2D.h"

#include "worldgen/data/Biome.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"

#include <math/DeterministicMath.h>
#include <utils/WorldHash.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath> // std::floor
#include <unordered_set>
#include <vector>

namespace worldgen {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Pond lattice: cells ~380 m on a side (~3-4 cells per 512 m chunk), each of which
// deterministically maybe-spawns one pond. Cell size + the spawn probabilities
// below set the ~0-2 ponds-per-chunk density (wet biomes denser, deserts ~none).
constexpr double kCellMeters = 380.0;
constexpr double kJitterFrac = 0.34; // pond center jitter within its cell

constexpr double  kRadiusMin = 6.0;  // base radius band (meters)
constexpr double  kRadiusMax = 22.0;
constexpr double  kRimAmp    = 0.34; // rim wobble as a fraction of radius
constexpr uint8_t kPondDepth = 220; // cosmetic depth byte (fresh, fairly deep)

// Natural ponds: spawn chance = biomeWeight * kBaseProb * (floor + (1-floor)*wetness),
// where wetness is precipitation normalized to kPrecipFull. So a wet biome in a wet
// region approaches kBaseProb; a dry biome approaches zero.
constexpr double kBaseProb   = 0.42;
constexpr double kPrecipFull = 1800.0; // mm/yr counted as fully wet
constexpr double kWetFloor   = 0.35;

// Oasis: where a (dry-biome) cell sits farther than the biome threshold from any 3D
// water, a spring-fed pond appears with this (rare) chance, ramped by how far. The
// 3D grid is coarse (~tens of km/tile) so the ramp is mostly saturated; kOasisProb
// is what keeps oases rare and precious rather than carpeting deserts.
constexpr double kOasisProb        = 0.10;
constexpr double kOasisRampMeters  = 4000.0;
// BFS ring cap for the distance-to-water search. The 3D grid is coarse (tens of
// km/tile), so a cell with no water within a handful of rings is already far past
// any km-scale oasis threshold; a small cap bounds the worst-case cost in large
// waterless regions without changing the outcome.
constexpr int    kMaxWaterRings    = 8;

constexpr uint64_t kPondSalt = 0x9E3779B97F4A7C15ull; // distinct from the river seed space

// FNV hash -> double in [0,1) using the top 53 bits (the double mantissa).
double hashUnit(uint64_t h) {
    return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0);
}

// Per-biome pond likelihood (0 = never from the natural path). Wetlands wettest,
// forests moderate, grassland a bit less, savanna/tundra low, deserts ~none (the
// oasis path covers genuinely dry interiors), and open water/beach never.
double biomeWeight(Biome b) {
    switch (b) {
        case Biome::TemperateWetland:
        case Biome::TropicalWetland:         return 1.00;
        case Biome::TropicalRainforest:
        case Biome::TemperateRainforest:     return 0.70;
        case Biome::TemperateDeciduousForest:
        case Biome::BorealForest:
        case Biome::MontaneForest:           return 0.50;
        case Biome::TropicalSeasonalForest:  return 0.45;
        case Biome::TemperateGrassland:
        case Biome::AlpineGrassland:         return 0.40;
        case Biome::TropicalSavanna:         return 0.30;
        case Biome::ArcticTundra:
        case Biome::AlpineTundra:            return 0.15;
        case Biome::SemiDesert:
        case Biome::XericShrubland:          return 0.08;
        case Biome::PolarDesert:             return 0.05;
        case Biome::HotDesert:
        case Biome::ColdDesert:              return 0.02;
        case Biome::Ocean:
        case Biome::Lake:
        case Biome::Beach:                   return 0.00;
        default:                             return 0.20;
    }
}

// Distance (meters) past which a cell of this biome may sprout an oasis. Negative =
// never (already-wet biomes / open water). Deserts are large so oases stay rare and
// precious; other land biomes use a few km so no land is left far from any water.
double oasisThreshold(Biome b) {
    switch (b) {
        case Biome::HotDesert:
        case Biome::ColdDesert:              return 5000.0;
        case Biome::SemiDesert:
        case Biome::XericShrubland:          return 3500.0;
        case Biome::TropicalSavanna:         return 3000.0;
        case Biome::ArcticTundra:
        case Biome::AlpineTundra:            return 3000.0;
        case Biome::TemperateGrassland:
        case Biome::AlpineGrassland:
        case Biome::TemperateDeciduousForest:
        case Biome::BorealForest:
        case Biome::MontaneForest:
        case Biome::TropicalSeasonalForest:  return 2500.0;
        default:                             return -1.0; // wetlands, rainforest, water, beach, polar
    }
}

} // namespace

PondNetwork2D::PondNetwork2D(std::shared_ptr<const GeneratedWorld> generatedWorld,
                             double landingLatDeg, double landingLonDeg)
    : world(std::move(generatedWorld)),
      projection(world->derived.planetRadiusMeters, landingLatDeg, landingLonDeg),
      seaLevelMeters(world->seaLevelMeters) {
    constexpr uint32_t kRequiredFields =
        static_cast<uint32_t>(WorldField::Elevation) |
        static_cast<uint32_t>(WorldField::Flags) |
        static_cast<uint32_t>(WorldField::FlowAccum) |
        static_cast<uint32_t>(WorldField::Downhill) |
        static_cast<uint32_t>(WorldField::Precipitation) |
        static_cast<uint32_t>(WorldField::Biome);
    assert(world->grid != nullptr);
    assert((world->validFields & kRequiredFields) == kRequiredFields);
}

TileId PondNetwork2D::tileAt(double xMeters, double yMeters) const {
    return world->grid->fromUnitVector(projection.worldToUnitVector(xMeters, yMeters));
}

bool PondNetwork2D::isWaterTile(TileId t) const {
    const uint8_t f = world->data.flags[t];
    return (f & (kFlagOcean | kFlagLake | kFlagRiver)) != 0 ||
           world->data.elevation[t] < seaLevelMeters;
}

double PondNetwork2D::nearestWaterMeters(TileId from) const {
    const SphereGrid& grid = *world->grid;
    const double tileW = grid.tileWidthMeters(from, world->derived.planetRadiusMeters);

    std::unordered_set<TileId> visited;
    visited.insert(from);
    std::vector<TileId> frontier{from};

    for (int ring = 0; ring <= kMaxWaterRings; ++ring) {
        for (TileId t : frontier) {
            if (isWaterTile(t)) return static_cast<double>(ring) * tileW;
        }
        std::vector<TileId> next;
        for (TileId t : frontier) {
            std::array<TileId, 6> nbrs{};
            const uint32_t count = grid.neighbors(t, nbrs);
            for (uint32_t i = 0; i < count; ++i) {
                if (visited.insert(nbrs[i]).second) next.push_back(nbrs[i]);
            }
        }
        if (next.empty()) break;
        frontier = std::move(next);
    }
    return static_cast<double>(kMaxWaterRings) * tileW; // none within cap: treat as very far
}

bool PondNetwork2D::cellPond(long i, long j, Pond& out) const {
    const uint64_t h = foundation::hashCombine(
        foundation::hashCombine(world->params.seed ^ kPondSalt, static_cast<uint64_t>(i)),
        static_cast<uint64_t>(j));

    const double jx = (hashUnit(foundation::hashCombine(h, 0x11)) - 0.5) * 2.0 * kJitterFrac;
    const double jy = (hashUnit(foundation::hashCombine(h, 0x12)) - 0.5) * 2.0 * kJitterFrac;
    const double cx = (static_cast<double>(i) + 0.5 + jx) * kCellMeters;
    const double cy = (static_cast<double>(j) + 0.5 + jy) * kCellMeters;

    const TileId t = tileAt(cx, cy);
    if (isWaterTile(t)) return false; // don't drop a pond inside the sea/lake/river

    const Biome  biome = static_cast<Biome>(world->data.biome[t]);
    const double bw = biomeWeight(biome);
    const double wet = std::clamp(static_cast<double>(world->data.precipitation[t]) / kPrecipFull, 0.0, 1.0);

    // Two ways a pond surfaces in this cell, but only one kind of pond results:
    //   1. the local biome/precipitation is wet enough (the common case), or
    //   2. the cell is far from any water, so a spring surfaces -- an oasis when
    //      that happens to be a desert. An oasis is not a separate thing; it is just
    //      a spring-fed pond in a dry place.
    const double naturalProb = bw * kBaseProb * (kWetFloor + (1.0 - kWetFloor) * wet);
    bool spawn = hashUnit(foundation::hashCombine(h, 0x21)) < naturalProb;
    if (!spawn) {
        const double thr = oasisThreshold(biome);
        if (thr < 0.0) return false; // already-wet biomes / open water never need a spring
        const double d = nearestWaterMeters(t);
        if (d <= thr) return false;
        const double ramp = std::clamp((d - thr) / kOasisRampMeters, 0.0, 1.0);
        spawn = hashUnit(foundation::hashCombine(h, 0x22)) < kOasisProb * ramp;
    }
    if (!spawn) return false;

    // Size grows with local wetness, so a desert spring is naturally a small pool and
    // a wetland pond is broad.
    const double radiusFrac = 0.55 + 0.45 * wet;
    const double radius =
        (kRadiusMin + (kRadiusMax - kRadiusMin) * hashUnit(foundation::hashCombine(h, 0x31))) * radiusFrac;
    out.cx = cx;
    out.cy = cy;
    out.radius = static_cast<float>(radius);
    out.phaseA = static_cast<float>(2.0 * kPi * hashUnit(foundation::hashCombine(h, 0x41)));
    out.phaseB = static_cast<float>(2.0 * kPi * hashUnit(foundation::hashCombine(h, 0x42)));
    out.depth = kPondDepth;
    return true;
}

void PondNetwork2D::gatherPonds(double minX, double minY, double maxX, double maxY,
                                std::vector<Pond>& out) const {
    const double maxFootprint = kRadiusMax * (1.0 + kRimAmp) + 1.0;
    const long i0 = static_cast<long>(std::floor((minX - maxFootprint) / kCellMeters));
    const long i1 = static_cast<long>(std::floor((maxX + maxFootprint) / kCellMeters));
    const long j0 = static_cast<long>(std::floor((minY - maxFootprint) / kCellMeters));
    const long j1 = static_cast<long>(std::floor((maxY + maxFootprint) / kCellMeters));

    for (long j = j0; j <= j1; ++j) {
        for (long i = i0; i <= i1; ++i) {
            Pond p;
            if (!cellPond(i, j, p)) continue;
            const double pr = static_cast<double>(p.radius) * (1.0 + kRimAmp);
            if (p.cx + pr < minX || p.cx - pr > maxX || p.cy + pr < minY || p.cy - pr > maxY) continue;
            out.push_back(p);
        }
    }
}

uint8_t PondNetwork2D::sampleDepth(const Pond& p, double x, double y) {
    const double dx = x - p.cx;
    const double dy = y - p.cy;
    const double maxR = static_cast<double>(p.radius) * (1.0 + kRimAmp);
    if (dx < -maxR || dx > maxR || dy < -maxR || dy > maxR) return 0; // cheap AABB reject
    const double d2 = dx * dx + dy * dy;
    const double innerR = static_cast<double>(p.radius) * (1.0 - kRimAmp);
    if (d2 <= innerR * innerR) return p.depth;   // well inside, skip the trig
    if (d2 > maxR * maxR) return 0;              // well outside
    const double theta = foundation::det_math::atan2(dy, dx);
    const double wobble = 0.6 * foundation::det_math::sin(3.0 * theta + static_cast<double>(p.phaseA)) +
                          0.4 * foundation::det_math::sin(5.0 * theta + static_cast<double>(p.phaseB));
    const double edge = static_cast<double>(p.radius) * (1.0 + kRimAmp * wobble);
    return (d2 <= edge * edge) ? p.depth : 0;
}

uint8_t PondNetwork2D::depthAt(double xMeters, double yMeters) const {
    const double margin = kRadiusMax * (1.0 + kRimAmp) + 1.0;
    std::vector<Pond> ponds;
    gatherPonds(xMeters - margin, yMeters - margin, xMeters + margin, yMeters + margin, ponds);
    uint8_t best = 0;
    for (const Pond& p : ponds) best = std::max(best, sampleDepth(p, xMeters, yMeters));
    return best;
}

} // namespace worldgen
