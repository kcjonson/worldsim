#include "worldgen/sampling/SpawnSite.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/sampling/PlanetSampler.h"
#include "worldgen/sampling/RiverNetwork2D.h"

#include <math/DeterministicMath.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace worldgen {

namespace {

constexpr double kStepMeters = 3.0; // candidate spacing in the search grid

double distPointSegment(double px, double py, double ax, double ay,
                        double bx, double by, double& outT) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = 0.0;
    if (len2 > 0.0) {
        t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = std::clamp(t, 0.0, 1.0);
    }
    const double cx = ax + dx * t;
    const double cy = ay + dy * t;
    outT = t;
    const double ex = px - cx;
    const double ey = py - cy;
    return foundation::det_math::sqrt(ex * ex + ey * ey);
}

} // namespace

SpawnSite findRiverbankSpawn(const std::shared_ptr<const GeneratedWorld>& world,
                             double landingLatDeg, double landingLonDeg,
                             double searchRadiusMeters, double bankDistanceMeters) {
    PlanetSampler ps(world, landingLatDeg, landingLonDeg);

    std::vector<RiverNetwork2D::Segment> segs;
    constexpr uint32_t kRiverFields = static_cast<uint32_t>(WorldField::FlowAccum) |
                                      static_cast<uint32_t>(WorldField::Downhill);
    if ((world->validFields & kRiverFields) == kRiverFields) {
        RiverNetwork2D rn(world, landingLatDeg, landingLonDeg);
        const double pad = searchRadiusMeters + bankDistanceMeters;
        rn.gatherSegments(-pad, -pad, pad, pad, segs);
    }

    // Signed distance to the nearest river channel edge: <= 0 means inside the
    // channel (water), > 0 means dry land that far from the bank.
    auto channelSignedDist = [&](double x, double y) -> double {
        double best = std::numeric_limits<double>::max();
        for (const auto& s : segs) {
            double t = 0.0;
            const double d = distPointSegment(x, y, s.x0, s.y0, s.x1, s.y1, t);
            const double hw = static_cast<double>(s.halfWidth0) +
                              (static_cast<double>(s.halfWidth1) - static_cast<double>(s.halfWidth0)) * t;
            best = std::min(best, d - hw);
        }
        return best;
    };

    // Track the three water kinds separately so the documented preference order
    // (riverbank > lake shore > salt coast) holds regardless of which is closest:
    // a riverbank anywhere in range outranks even a nearer lake shore.
    double bestBank = std::numeric_limits<double>::max();
    SpawnSite bank{};
    double bestLake = std::numeric_limits<double>::max();
    SpawnSite lake{};
    double bestCoast = std::numeric_limits<double>::max();
    SpawnSite coast{};

    const double offs[4][2] = {{bankDistanceMeters, 0.0}, {-bankDistanceMeters, 0.0},
                               {0.0, bankDistanceMeters}, {0.0, -bankDistanceMeters}};

    for (double y = -searchRadiusMeters; y <= searchRadiusMeters; y += kStepMeters) {
        for (double x = -searchRadiusMeters; x <= searchRadiusMeters; x += kStepMeters) {
            const auto here = ps.sampleAt(x, y);
            if (here.water) continue; // biome water (ocean/lake): not land

            const double ch = segs.empty() ? std::numeric_limits<double>::max()
                                           : channelSignedDist(x, y);
            if (ch <= 0.0) continue; // inside a river channel: water, not land

            const double d0 = foundation::det_math::sqrt(x * x + y * y);

            if (ch <= bankDistanceMeters) { // dry land on a riverbank
                if (d0 < bestBank) { bestBank = d0; bank = {x, y, true, true}; }
                continue;
            }

            // No riverbank here: is biome water (lake = fresh, ocean = salt) within reach?
            bool lakeNear = false;
            bool oceanNear = false;
            for (const auto& o : offs) {
                const auto n = ps.sampleAt(x + o[0], y + o[1]);
                if (!n.water) continue;
                if (n.weights[0].biome == Biome::Lake) lakeNear = true;
                else oceanNear = true;
            }
            if (lakeNear) {
                if (d0 < bestLake) { bestLake = d0; lake = {x, y, true, true}; }
            } else if (oceanNear) {
                if (d0 < bestCoast) { bestCoast = d0; coast = {x, y, true, false}; }
            }
        }
    }

    if (bestBank < std::numeric_limits<double>::max()) return bank;
    if (bestLake < std::numeric_limits<double>::max()) return lake;
    if (bestCoast < std::numeric_limits<double>::max()) return coast;
    return SpawnSite{0.0, 0.0, false, false};
}

} // namespace worldgen
