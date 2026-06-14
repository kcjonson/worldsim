// WorldStats — compute analytical statistics from a completed GeneratedWorld.
//
// All algorithms are single-threaded and deterministic.  BFS uses ascending
// TileId seed order + FIFO queue, which matches the determinism contract in
// TerrainStage.

#include "worldgen/debug/WorldStats.h"

#include "worldgen/data/Biome.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace worldgen {

namespace {

// ============================================================================
// Helpers
// ============================================================================

static constexpr double kPi = 3.14159265358979323846;

// Tile width in km using the same equatorial-circumference approximation as
// TerrainStage (consistency matters more than absolute accuracy).
float tileWidthKm(const SphereGrid& grid, double planetRadiusMeters) {
    double circumference = 2.0 * kPi * planetRadiusMeters;
    double widthM = circumference / std::sqrt(static_cast<double>(grid.tileCount()));
    return static_cast<float>(widthM / 1000.0);
}

// km -> tile-distance conversion matching TerrainStage's lambda.
float kmToTiles(float km, float tileKm) {
    return km / tileKm;
}

// ============================================================================
// Median of a vector (modifies in place — caller uses a copy if needed)
// ============================================================================

float median(std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<ptrdiff_t>(mid), v.end());
    if (v.size() % 2 == 1) return v[mid];
    // Even: average of two middle elements.
    float upper = v[mid];
    std::nth_element(v.begin(), v.begin() + static_cast<ptrdiff_t>(mid - 1), v.begin() + static_cast<ptrdiff_t>(mid));
    return (v[mid - 1] + upper) * 0.5f;
}

// Tile-count-weighted median: expand each belt's value by its tileCount weight,
// then find the weighted 50th-percentile without literally expanding.
// Builds a sorted list of (value, weight) pairs and walks the CDF.
float tileWeightedMedian(const std::vector<BeltStats>& belts,
                         float BeltStats::* field) {
    if (belts.empty()) return 0.0f;
    // Collect (value, tileCount) and sort by value.
    std::vector<std::pair<float, uint32_t>> vw;
    vw.reserve(belts.size());
    uint64_t totalTiles = 0;
    for (const auto& b : belts) {
        float v = b.*field;
        vw.push_back({v, b.tileCount});
        totalTiles += b.tileCount;
    }
    if (totalTiles == 0) return 0.0f;
    std::sort(vw.begin(), vw.end(),
              [](const std::pair<float,uint32_t>& a, const std::pair<float,uint32_t>& b) {
                  return a.first < b.first;
              });
    // Walk until cumulative weight >= 50% of total.
    uint64_t half = (totalTiles + 1) / 2;
    uint64_t cumul = 0;
    for (const auto& p : vw) {
        cumul += p.second;
        if (cumul >= half) return p.first;
    }
    return vw.back().first;
}

// ============================================================================
// Hypsometry
// ============================================================================

struct HypsoResult {
    float             binMin{};
    float             binWidth{};
    std::vector<uint32_t> hist; // 256 bins
    std::vector<float>    modeElevations;
    std::vector<uint32_t> modeCounts;
    float                 troughElevation{};
    float                 troughFraction{};
};

HypsoResult computeHypsometry(const std::vector<float>& elevation) {
    static constexpr int kBins = 256;
    HypsoResult res;
    if (elevation.empty()) return res;

    float minE = *std::min_element(elevation.begin(), elevation.end());
    float maxE = *std::max_element(elevation.begin(), elevation.end());
    if (maxE <= minE) maxE = minE + 1.0f;

    res.binMin   = minE;
    res.binWidth = (maxE - minE) / kBins;
    res.hist.assign(kBins, 0u);

    for (float e : elevation) {
        int bin = static_cast<int>((e - minE) / res.binWidth);
        if (bin < 0)         bin = 0;
        if (bin >= kBins)    bin = kBins - 1;
        res.hist[static_cast<size_t>(bin)]++;
    }

    // Two-threshold bimodality. Earth's hypsometry has three populations: abyssal
    // plains (~-5000 m), the C-3 shelf shoulder (~-120/-140 m), and the continental
    // platform (+100 to +500 m). A single split at -1500 m groups the shelf
    // shoulder with the land peak, which is fine at Earth-like waterAmount. But at
    // high waterAmount (sparse land), the shelf shoulder can dominate the "land"
    // slot and suppress the true platform mode.
    //
    // Fix: use TWO thresholds that exclude the shelf shoulder from BOTH canonical
    // modes. The abyssal mode must be at or below kAbyssalModeMaxM (well into the
    // abyssal plain, below the slope foot); the land mode must be at or above
    // kLandModeMinM (on or above sea level, i.e. the continental platform). The
    // shelf shoulder in between is a valid third population but not one of the two
    // reported modes.
    static constexpr float kAbyssalModeMaxM = -2000.0f; // abyssal mode must be <= this
    static constexpr float kLandModeMinM    =     0.0f; // land mode must be >= this

    auto binForElev = [&](float e) -> int {
        int b = static_cast<int>((e - minE) / res.binWidth);
        if (b < 0)      b = 0;
        if (b >= kBins) b = kBins - 1;
        return b;
    };

    const int abyssalMaxBin = binForElev(kAbyssalModeMaxM);
    const int landMinBin    = binForElev(kLandModeMinM);

    auto tallestBinInRange = [&](int lo, int hi) -> int {
        int bestBin = -1;
        uint32_t bestCount = 0;
        for (int b = lo; b < hi; ++b) {
            if (res.hist[static_cast<size_t>(b)] > bestCount) {
                bestCount = res.hist[static_cast<size_t>(b)];
                bestBin   = b;
            }
        }
        return bestBin;
    };

    // abyssal mode: tallest bin at elevation <= kAbyssalModeMaxM
    const int abyssalBin = tallestBinInRange(0, abyssalMaxBin + 1);
    // land mode: tallest bin at elevation >= kLandModeMinM
    int landBin          = tallestBinInRange(landMinBin, kBins);

    // For a normal world the two ranges are disjoint, so abyssalBin != landBin. But a
    // degenerate world (all-ocean or all-land with a narrow elevation span) can clamp
    // both thresholds onto the same histogram bin; report a single mode in that case
    // rather than a spurious duplicate that downstream reads as bimodal.
    if (landBin == abyssalBin) landBin = -1;

    // Emit the dominant (higher-count) mode first to preserve the convention
    // that modeElevations[0] is the larger population.
    struct Mode { int bin; uint32_t count; };
    std::vector<Mode> modes;
    if (abyssalBin >= 0) modes.push_back({abyssalBin, res.hist[static_cast<size_t>(abyssalBin)]});
    if (landBin >= 0)    modes.push_back({landBin,    res.hist[static_cast<size_t>(landBin)]});
    std::sort(modes.begin(), modes.end(),
              [](const Mode& a, const Mode& b) { return a.count > b.count; });

    for (const Mode& m : modes) {
        float center = res.binMin + (static_cast<float>(m.bin) + 0.5f) * res.binWidth;
        res.modeElevations.push_back(center);
        res.modeCounts.push_back(m.count);
    }

    // Trough = the shallowest-count bin between the two modes (the genuine
    // land/abyssal gap). The two-threshold design above places abyssalBin well
    // below kAbyssalModeMaxM and landBin at or above kLandModeMinM, so they are
    // never adjacent in practice; the guard below handles any degenerate world
    // where they somehow end up in the same or adjacent bins.
    if (abyssalBin >= 0 && landBin >= 0) {
        int binA = abyssalBin;
        int binB = landBin;
        if (binA > binB) std::swap(binA, binB);

        if (binB <= binA + 1) {
            // Modes are adjacent or identical — no gap exists. Report the midpoint
            // and troughFraction = 1.0 (no separation).
            float midElev = res.binMin + (static_cast<float>(binA) + 1.0f) * res.binWidth;
            res.troughElevation = midElev;
            res.troughFraction  = 1.0f;
        } else {
            uint32_t troughCount = std::numeric_limits<uint32_t>::max();
            int      troughBin   = binA;
            for (int b = binA + 1; b < binB; ++b) {
                if (res.hist[static_cast<size_t>(b)] < troughCount) {
                    troughCount = res.hist[static_cast<size_t>(b)];
                    troughBin   = b;
                }
            }
            res.troughElevation = res.binMin + (static_cast<float>(troughBin) + 0.5f) * res.binWidth;
            uint32_t lowerPeak  = std::min(modes[0].count, modes[1].count);
            res.troughFraction  = lowerPeak > 0
                ? static_cast<float>(troughCount) / static_cast<float>(lowerPeak)
                : 1.0f;
        }
    }

    return res;
}

// ============================================================================
// Plate area stats
// ============================================================================

PlateAreaStats computePlateAreaStats(const std::vector<uint8_t>& plateId) {
    PlateAreaStats s;
    if (plateId.empty()) return s;

    static constexpr size_t kMaxPlates = 256;
    std::array<uint32_t, kMaxPlates> counts{};
    counts.fill(0u);
    for (uint8_t pid : plateId) {
        counts[static_cast<size_t>(pid)]++;
    }

    std::vector<uint32_t> areas;
    areas.reserve(32);
    for (size_t i = 0; i < kMaxPlates; ++i) {
        if (counts[i] > 0) areas.push_back(counts[i]);
    }
    if (areas.empty()) return s;

    // Sort descending.
    std::sort(areas.begin(), areas.end(), std::greater<uint32_t>{});
    s.sortedAreas = areas;

    if (areas.back() > 0) {
        s.largestToSmallestRatio = static_cast<float>(areas.front()) /
                                   static_cast<float>(areas.back());
    }

    // R^2 of log(area) vs rank (1-indexed).
    size_t K = areas.size();
    if (K < 2) {
        s.logAreaRankR2 = 1.0f;
        return s;
    }

    // Mean of x=rank, y=log(area).
    double sumX = 0.0, sumY = 0.0;
    for (size_t r = 0; r < K; ++r) {
        sumX += static_cast<double>(r + 1);
        sumY += std::log(static_cast<double>(areas[r]));
    }
    double n   = static_cast<double>(K);
    double xBar = sumX / n;
    double yBar = sumY / n;

    double sXX = 0.0, sXY = 0.0, sYY = 0.0;
    for (size_t r = 0; r < K; ++r) {
        double dx = static_cast<double>(r + 1) - xBar;
        double dy = std::log(static_cast<double>(areas[r])) - yBar;
        sXX += dx * dx;
        sXY += dx * dy;
        sYY += dy * dy;
    }

    if (sXX > 0.0 && sYY > 0.0) {
        double r = sXY / std::sqrt(sXX * sYY);
        s.logAreaRankR2 = static_cast<float>(r * r);
    }
    return s;
}

// ============================================================================
// Mountain belts: connected components of land tiles above meanLand+1500m
// ============================================================================

// PCA over a set of unit-vector tile positions projected into a tangent plane.
// Returns elongation = sqrt(lambda1/lambda2), and lambdas themselves.
struct PcaResult {
    float elongation{1.0f}; // sqrt(lambda1/lambda2); 1 = isotropic, high = elongated
    float lambda1{};
    float lambda2{};
};

PcaResult pcaOnUnitVectors(const SphereGrid& grid, const std::vector<TileId>& tiles) {
    PcaResult res;
    if (tiles.size() < 2) return res;

    // Compute centroid in 3D, then project each tile into the tangent plane.
    double cx = 0.0, cy = 0.0, cz = 0.0;
    for (TileId t : tiles) {
        Vec3d p = grid.tileCenter(t);
        cx += p.x; cy += p.y; cz += p.z;
    }
    double norm = std::sqrt(cx*cx + cy*cy + cz*cz);
    if (norm < 1e-10) return res;
    cx /= norm; cy /= norm; cz /= norm;

    // Build two orthogonal tangent vectors at centroid.
    // e1: any vector perpendicular to centroid.
    double ax = 0.0, ay = 1.0, az = 0.0;
    double dot = ax*cx + ay*cy + az*cz;
    ax -= dot*cx; ay -= dot*cy; az -= dot*cz;
    double aNorm = std::sqrt(ax*ax + ay*ay + az*az);
    if (aNorm < 1e-10) {
        ax = 1.0; ay = 0.0; az = 0.0;
        dot = ax*cx;
        ax -= dot*cx; ay -= dot*cy; az -= dot*cz;
        aNorm = std::sqrt(ax*ax + ay*ay + az*az);
    }
    ax /= aNorm; ay /= aNorm; az /= aNorm;

    // e2: centroid × e1
    double bx = cy*az - cz*ay;
    double by = cz*ax - cx*az;
    double bz = cx*ay - cy*ax;

    // 2x2 covariance of projected coordinates.
    double s11 = 0.0, s12 = 0.0, s22 = 0.0;
    double n = static_cast<double>(tiles.size());
    std::vector<double> projU(tiles.size()), projV(tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) {
        Vec3d p = grid.tileCenter(tiles[i]);
        projU[i] = p.x*ax + p.y*ay + p.z*az;
        projV[i] = p.x*bx + p.y*by + p.z*bz;
    }
    double uBar = 0.0, vBar = 0.0;
    for (size_t i = 0; i < tiles.size(); ++i) { uBar += projU[i]; vBar += projV[i]; }
    uBar /= n; vBar /= n;
    for (size_t i = 0; i < tiles.size(); ++i) {
        double du = projU[i] - uBar;
        double dv = projV[i] - vBar;
        s11 += du*du;
        s12 += du*dv;
        s22 += dv*dv;
    }
    s11 /= n; s12 /= n; s22 /= n;

    // Eigenvalues of 2x2 symmetric matrix.
    double trace = s11 + s22;
    double det   = s11*s22 - s12*s12;
    double disc  = trace*trace - 4.0*det;
    if (disc < 0.0) disc = 0.0;
    double sqrtDisc = std::sqrt(disc);
    double lam1 = (trace + sqrtDisc) * 0.5;
    double lam2 = (trace - sqrtDisc) * 0.5;
    if (lam2 < 1e-15) lam2 = 1e-15;

    res.lambda1 = static_cast<float>(lam1);
    res.lambda2 = static_cast<float>(lam2);
    res.elongation = static_cast<float>(std::sqrt(lam1 / lam2));
    return res;
}

// ============================================================================
// Double-BFS graph diameter approximation (standard two-pass BFS).
// Returns hop count of the diameter.  Restricted to tiles in the component,
// identified by a bool membership array.
// Tie-break: when multiple tiles share the maximum distance in each BFS pass,
// pick the smallest TileId (ascending iteration ensures this naturally since we
// overwrite only on strict improvement).
// ============================================================================

static uint32_t bfsDiameter(const SphereGrid& grid,
                             const std::vector<TileId>& tiles,
                             const std::vector<bool>& inComp,
                             std::vector<int32_t>& dist) {
    if (tiles.empty()) return 0;
    if (tiles.size() == 1) return 0;

    std::array<TileId, 6> nbrs{};
    std::vector<TileId> queue;
    queue.reserve(tiles.size());

    auto bfsFrom = [&](TileId src) -> TileId {
        // dist is pre-allocated to N and pre-filled with -1 by caller.
        // We only set values for tiles in this component.
        dist[src] = 0;
        queue.clear();
        queue.push_back(src);
        TileId farthest = src;
        int32_t maxDist = 0;
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            TileId t = queue[qi];
            uint32_t cnt = grid.neighbors(t, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId nb = nbrs[k];
                if (inComp[nb] && dist[nb] < 0) {
                    dist[nb] = dist[t] + 1;
                    queue.push_back(nb);
                    if (dist[nb] > maxDist ||
                        (dist[nb] == maxDist && nb < farthest)) {
                        maxDist = dist[nb];
                        farthest = nb;
                    }
                }
            }
        }
        // Reset dist entries we set (avoids a full N-wide fill between calls).
        for (TileId t : queue) dist[t] = -1;
        dist[src] = -1;
        return farthest;
    };

    // Pass 1: BFS from tiles[0] (arbitrary, ascending-sorted → deterministic).
    TileId a = bfsFrom(tiles[0]);
    // Pass 2: BFS from a → farthest tile b.
    TileId b = bfsFrom(a);
    // Pass 3: BFS from b to get the actual diameter distance.
    dist[b] = 0;
    queue.clear();
    queue.push_back(b);
    int32_t diameter = 0;
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        TileId t = queue[qi];
        if (dist[t] > diameter) diameter = dist[t];
        uint32_t cnt = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            TileId nb = nbrs[k];
            if (inComp[nb] && dist[nb] < 0) {
                dist[nb] = dist[t] + 1;
                queue.push_back(nb);
            }
        }
    }
    for (TileId t : queue) dist[t] = -1;
    dist[b] = -1;
    return static_cast<uint32_t>(diameter);
}

std::vector<BeltStats> computeBeltStats(const GeneratedWorld& world,
                                        float meanLandElevation,
                                        float tileKm) {
    const SphereGrid& grid    = *world.grid;
    const uint32_t    N       = grid.tileCount();
    const float       thresh  = meanLandElevation + 1500.0f;

    // Mark candidate tiles.
    std::vector<bool> candidate(N, false);
    for (uint32_t t = 0; t < N; ++t) {
        if ((world.data.flags[t] & kFlagOcean) == 0 &&
            world.data.elevation[t] > thresh) {
            candidate[t] = true;
        }
    }

    // BFS connected-components over candidates (ascending TileId seed order).
    std::vector<int32_t> comp(N, -1);
    int32_t nextComp = 0;
    std::vector<TileId> queue;
    queue.reserve(N / 10);
    std::array<TileId, 6> nbrs{};

    for (uint32_t seed = 0; seed < N; ++seed) {
        if (!candidate[seed] || comp[seed] >= 0) continue;
        int32_t c = nextComp++;
        comp[seed] = c;
        queue.clear();
        queue.push_back(seed);
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            TileId t = queue[qi];
            uint32_t cnt = grid.neighbors(t, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId nb = nbrs[k];
                if (candidate[nb] && comp[nb] < 0) {
                    comp[nb] = c;
                    queue.push_back(nb);
                }
            }
        }
    }

    // Gather tiles per component.
    std::vector<std::vector<TileId>> compTiles(static_cast<size_t>(nextComp));
    for (uint32_t t = 0; t < N; ++t) {
        if (comp[t] >= 0) compTiles[static_cast<size_t>(comp[t])].push_back(t);
    }

    // Shared dist workspace for double-BFS (reused across components).
    std::vector<int32_t> dist(N, -1);

    // Membership bitset for BFS (built per component).
    std::vector<bool> inComp(N, false);

    // Compute stats only for components >= 32 tiles.
    float tileAreaKm2 = tileKm * tileKm;
    double radiusKm   = world.derived.planetRadiusMeters / 1000.0;

    std::vector<BeltStats> stats;
    for (const auto& tiles : compTiles) {
        if (tiles.size() < 32) continue;

        BeltStats bs;
        bs.tileCount = static_cast<uint32_t>(tiles.size());

        PcaResult pca = pcaOnUnitVectors(grid, tiles);
        bs.elongation = pca.elongation;

        // PCA-derived width (kept as second signal).
        float pcaLengthKm = static_cast<float>(2.0 * std::sqrt(static_cast<double>(pca.lambda1)) * radiusKm);
        float areaKm2     = static_cast<float>(tiles.size()) * tileAreaKm2;
        bs.widthKm        = (pcaLengthKm > 0.0f) ? areaKm2 / pcaLengthKm : 0.0f;

        // Geodesic aspect ratio via double-BFS diameter.
        // Mark membership for this component.
        for (TileId t : tiles) inComp[t] = true;

        uint32_t diamHops = bfsDiameter(grid, tiles, inComp, dist);
        bs.lengthKm   = static_cast<float>(diamHops) * tileKm;
        bs.geoWidthKm = (bs.lengthKm > 0.0f) ? areaKm2 / bs.lengthKm : static_cast<float>(tileKm);
        float denom   = (bs.geoWidthKm > tileKm) ? bs.geoWidthKm : tileKm;
        bs.aspectRatio = (bs.lengthKm > 0.0f) ? bs.lengthKm / denom : 1.0f;

        // Clear membership.
        for (TileId t : tiles) inComp[t] = false;

        stats.push_back(bs);
    }
    return stats;
}

// ============================================================================
// Continent stats: connected components of land >= 0.5% of N
// ============================================================================

std::vector<ContinentStats> computeContinentStats(const GeneratedWorld& world, float tileKm) {
    const SphereGrid& grid = *world.grid;
    const uint32_t    N    = grid.tileCount();

    // Mark land tiles.
    std::vector<bool> isLand(N, false);
    for (uint32_t t = 0; t < N; ++t) {
        if ((world.data.flags[t] & kFlagOcean) == 0) isLand[t] = true;
    }

    // BFS connected components (ascending seed order).
    std::vector<int32_t> comp(N, -1);
    int32_t nextComp = 0;
    std::vector<TileId> queue;
    queue.reserve(N / 10);
    std::array<TileId, 6> nbrs{};

    for (uint32_t seed = 0; seed < N; ++seed) {
        if (!isLand[seed] || comp[seed] >= 0) continue;
        int32_t c = nextComp++;
        comp[seed] = c;
        queue.clear();
        queue.push_back(seed);
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            TileId t = queue[qi];
            uint32_t cnt = grid.neighbors(t, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                TileId nb = nbrs[k];
                if (isLand[nb] && comp[nb] < 0) {
                    comp[nb] = c;
                    queue.push_back(nb);
                }
            }
        }
    }

    // Gather tiles and count perimeter edges per component.
    size_t nc = static_cast<size_t>(nextComp);
    std::vector<uint32_t> compCount(nc, 0u);
    std::vector<uint32_t> compPerim(nc, 0u);  // land->ocean edge crossings

    for (uint32_t t = 0; t < N; ++t) {
        if (comp[t] < 0) continue;
        size_t ci = static_cast<size_t>(comp[t]);
        compCount[ci]++;

        // Count edges to ocean neighbours.
        uint32_t cnt = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            TileId nb = nbrs[k];
            if (!isLand[nb]) compPerim[ci]++;
        }
    }

    // Threshold: >= 0.5% of N.
    uint32_t minSize = std::max(uint32_t{1}, N / 200u);

    // Mean edge length (radians, hex approximation):
    //   tileArea = 4*pi/N sr;  hexEdge ≈ sqrt(tileArea * 2/(3*sqrt(3)))
    // Convert to km: radiusKm * edgeRad.
    double radiusKm     = world.derived.planetRadiusMeters / 1000.0;
    double tileAreaRad  = 4.0 * kPi / static_cast<double>(N);
    double hexEdgeRad   = std::sqrt(tileAreaRad * 2.0 / (3.0 * std::sqrt(3.0)));
    double edgeLenKm    = radiusKm * hexEdgeRad;

    // Tile area in km^2.
    double tileAreaKm2  = tileKm * tileKm;

    std::vector<ContinentStats> stats;
    for (size_t ci = 0; ci < nc; ++ci) {
        if (compCount[ci] < minSize) continue;

        ContinentStats cs;
        cs.tileCount = compCount[ci];

        // Flat-space isoperimetric ratio: P^2 / (4*pi*A).
        double A = static_cast<double>(compCount[ci]) * tileAreaKm2;
        double P = static_cast<double>(compPerim[ci]) * edgeLenKm;
        cs.isoperimetricRatio = (A > 0.0)
            ? static_cast<float>((P * P) / (4.0 * kPi * A))
            : 0.0f;

        stats.push_back(cs);
    }
    return stats;
}

// ============================================================================
// Interior mountain fraction
// ============================================================================

float computeInteriorMountainFraction(const GeneratedWorld& world,
                                      const std::vector<BeltStats>& /*belts*/,
                                      float meanLandElevation,
                                      float tileKm) {
    const SphereGrid& grid   = *world.grid;
    const uint32_t    N      = grid.tileCount();
    const float       thresh = meanLandElevation + 1500.0f;
    const float       distThreshTiles = kmToTiles(500.0f, tileKm);

    uint32_t total   = 0;
    uint32_t interior = 0;
    for (uint32_t t = 0; t < N; ++t) {
        if ((world.data.flags[t] & kFlagOcean) != 0) continue;
        if (world.data.elevation[t] <= thresh) continue;
        ++total;
        if (static_cast<float>(world.data.boundaryDistance[t]) > distThreshTiles) ++interior;
    }
    return (total > 0) ? static_cast<float>(interior) / static_cast<float>(total) : 0.0f;
}

} // namespace

// ============================================================================
// Public entry point
// ============================================================================

WorldStats computeWorldStats(const GeneratedWorld& world) {
    assert(world.grid);
    const SphereGrid& grid = *world.grid;
    const uint32_t N = grid.tileCount();

    WorldStats s;
    s.tileCount = N;

    if (N == 0) return s;

    // ---- Ocean fraction ----
    uint32_t oceanCount = 0;
    for (uint32_t t = 0; t < N; ++t) {
        if (world.data.flags[t] & kFlagOcean) ++oceanCount;
    }
    s.oceanFraction = static_cast<float>(oceanCount) / static_cast<float>(N);

    // ---- Hypsometry ----
    {
        auto hyp = computeHypsometry(world.data.elevation);
        s.hypsoBinMin    = hyp.binMin;
        s.hypsoBinWidth  = hyp.binWidth;
        s.hypsoHist      = std::move(hyp.hist);
        s.modeElevations = std::move(hyp.modeElevations);
        s.modeCounts     = std::move(hyp.modeCounts);
        s.troughElevation = hyp.troughElevation;
        s.troughFraction  = hyp.troughFraction;
    }

    // ---- Plate areas ----
    s.plates = computePlateAreaStats(world.data.plateId);

    // ---- Belt and continent stats need tile width ----
    float tkm = tileWidthKm(grid, world.derived.planetRadiusMeters);

    // ---- Mountain belts ----
    {
        // Mean land elevation.
        double sumLand = 0.0;
        uint32_t landCount = 0;
        for (uint32_t t = 0; t < N; ++t) {
            if ((world.data.flags[t] & kFlagOcean) == 0) {
                sumLand += static_cast<double>(world.data.elevation[t]);
                ++landCount;
            }
        }
        float meanLand = landCount > 0
            ? static_cast<float>(sumLand / static_cast<double>(landCount))
            : 0.0f;

        s.belts = computeBeltStats(world, meanLand, tkm);

        // Median elongation (PCA).
        std::vector<float> elongs;
        elongs.reserve(s.belts.size());
        for (const auto& b : s.belts) elongs.push_back(b.elongation);
        s.medianBeltElongation = median(elongs);

        // Geodesic aspect ratio medians.
        std::vector<float> aspects;
        aspects.reserve(s.belts.size());
        for (const auto& b : s.belts) aspects.push_back(b.aspectRatio);
        s.medianAspectRatio = median(aspects);
        s.tileWeightedMedianAspectRatio = tileWeightedMedian(s.belts, &BeltStats::aspectRatio);

        // Interior mountain fraction.
        s.interiorMountainFraction = computeInteriorMountainFraction(world, s.belts, meanLand, tkm);
    }

    // ---- Continents ----
    {
        s.continents = computeContinentStats(world, tkm);

        std::vector<float> ratios;
        ratios.reserve(s.continents.size());
        for (const auto& c : s.continents) ratios.push_back(c.isoperimetricRatio);
        s.medianIsoperimetric = median(ratios);
    }

    // ---- Biome fractions (land tiles only) ----
    {
        std::array<uint32_t, static_cast<size_t>(Biome::Count)> biomeCounts{};
        biomeCounts.fill(0u);
        uint32_t landCount = 0;
        for (uint32_t t = 0; t < N; ++t) {
            auto b = static_cast<Biome>(world.data.biome[t]);
            if (b == Biome::Ocean || b == Biome::Lake) continue;
            ++landCount;
            const size_t idx = static_cast<size_t>(b);
            if (idx < static_cast<size_t>(Biome::Count)) ++biomeCounts[idx];
        }
        s.landTileCount = static_cast<float>(landCount);
        s.biomeFraction.fill(0.0f);
        if (landCount > 0) {
            for (size_t i = 0; i < static_cast<size_t>(Biome::Count); ++i) {
                s.biomeFraction[i] = static_cast<float>(biomeCounts[i]) /
                                     static_cast<float>(landCount);
            }
        }
    }

    // ---- Continental shelf: kFlagContinentalCrust tiles below sea level ----
    {
        uint32_t contCrustTotal = 0;
        uint32_t contCrustSubmerged = 0;
        const float seaLevel = world.seaLevelMeters;
        for (uint32_t t = 0; t < N; ++t) {
            if ((world.data.flags[t] & kFlagContinentalCrust) == 0) continue;
            ++contCrustTotal;
            if (world.data.elevation[t] < seaLevel) ++contCrustSubmerged;
        }
        s.shelfSubmergedFraction = (contCrustTotal > 0)
            ? static_cast<float>(contCrustSubmerged) / static_cast<float>(contCrustTotal)
            : 0.0f;
    }

    return s;
}

} // namespace worldgen
