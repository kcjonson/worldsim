#include "worldgen/sampling/SpawnSite.h"

#include "worldgen/data/WorldData.h"
#include "worldgen/sampling/PlanetSampler.h"
#include "worldgen/sampling/RiverNetwork2D.h"

#include <math/DeterministicMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace worldgen {

namespace {

constexpr double kStepMeters = 3.0; // spacing of the search grid

// The squared distance a sample with no water anywhere starts from. Finite, so the envelope arithmetic of
// the distance transform stays defined.
constexpr double kNoWaterSquared = 1e20;

enum class Surface : uint8_t { Land, Lake, Ocean };

double distanceToSegment(double x, double y, const RiverNetwork2D::Segment& segment, double& along) {
    const double spanX = segment.x1 - segment.x0;
    const double spanY = segment.y1 - segment.y0;
    const double lengthSquared = spanX * spanX + spanY * spanY;
    along = 0.0;
    if (lengthSquared > 0.0) {
        along = std::clamp(((x - segment.x0) * spanX + (y - segment.y0) * spanY) / lengthSquared, 0.0, 1.0);
    }
    const double offX = x - (segment.x0 + spanX * along);
    const double offY = y - (segment.y0 + spanY * along);
    return foundation::det_math::sqrt(offX * offX + offY * offY);
}

// Squared distance, in grid steps, from every sample of a side-by-side grid to the nearest sample that
// holds zero: the exact separable Euclidean transform (Felzenszwalb and Huttenlocher), rows then columns.
void squaredDistanceTransform(std::vector<double>& grid, int side) {
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> line(static_cast<std::size_t>(side));
    std::vector<int>    vertices(static_cast<std::size_t>(side));
    std::vector<double> boundaries(static_cast<std::size_t>(side) + 1);

    const auto pass = [&](std::size_t start, std::size_t stride) {
        for (int i = 0; i < side; ++i) line[static_cast<std::size_t>(i)] = grid[start + static_cast<std::size_t>(i) * stride];
        const auto crossing = [&](int later, int earlier) {
            const double laterHeight = line[static_cast<std::size_t>(later)] + static_cast<double>(later) * later;
            const double earlierHeight = line[static_cast<std::size_t>(earlier)] + static_cast<double>(earlier) * earlier;
            return (laterHeight - earlierHeight) / (2.0 * (later - earlier));
        };
        std::size_t envelope = 0;
        vertices[0] = 0;
        boundaries[0] = -infinity;
        boundaries[1] = infinity;
        for (int i = 1; i < side; ++i) {
            double boundary = crossing(i, vertices[envelope]);
            while (boundary <= boundaries[envelope]) {
                --envelope;
                boundary = crossing(i, vertices[envelope]);
            }
            ++envelope;
            vertices[envelope] = i;
            boundaries[envelope] = boundary;
            boundaries[envelope + 1] = infinity;
        }
        envelope = 0;
        for (int i = 0; i < side; ++i) {
            while (boundaries[envelope + 1] < i) ++envelope;
            const int    vertex = vertices[envelope];
            const double offset = static_cast<double>(i - vertex);
            grid[start + static_cast<std::size_t>(i) * stride] = offset * offset + line[static_cast<std::size_t>(vertex)];
        }
    };

    const auto count = static_cast<std::size_t>(side);
    for (std::size_t row = 0; row < count; ++row) pass(row * count, 1);
    for (std::size_t column = 0; column < count; ++column) pass(column, count);
}

struct Choice {
    SpawnSite site;
    double    waterDistance{};
    double    originDistance{};
    bool      found{};
};

} // namespace

SpawnSite findWatersideSpawn(const std::shared_ptr<const GeneratedWorld>& world, double landingLatDeg,
                             double landingLonDeg, double searchRadiusMeters) {
    const PlanetSampler sampler(world, landingLatDeg, landingLonDeg);
    const double        reach = kLandingWaterDistanceMeters + kLandingWaterDistanceToleranceMeters;

    std::vector<RiverNetwork2D::Segment> channels;
    constexpr uint32_t kRiverFields = static_cast<uint32_t>(WorldField::FlowAccum) |
                                      static_cast<uint32_t>(WorldField::Downhill);
    if ((world->validFields & kRiverFields) == kRiverFields) {
        const RiverNetwork2D rivers(world, landingLatDeg, landingLonDeg);
        const double         extent = searchRadiusMeters + reach;
        rivers.gatherSegments(-extent, -extent, extent, extent, channels);
    }

    // Signed distance to the nearest channel edge: <= 0 inside a channel.
    const auto channelEdgeDistance = [&](double x, double y) {
        double nearest = std::numeric_limits<double>::max();
        for (const RiverNetwork2D::Segment& segment : channels) {
            double       along = 0.0;
            const double centreline = distanceToSegment(x, y, segment, along);
            const double halfWidth = static_cast<double>(segment.halfWidth0) +
                                     (static_cast<double>(segment.halfWidth1) - static_cast<double>(segment.halfWidth0)) * along;
            nearest = std::min(nearest, centreline - halfWidth);
        }
        return nearest;
    };

    // Candidates lie on the search square's grid. The samples classified for lakes and ocean run a border
    // past it, so a candidate at the square's edge is not judged far from water it could not see.
    const int candidatesPerSide = static_cast<int>(std::floor(2.0 * searchRadiusMeters / kStepMeters)) + 1;
    const int border = static_cast<int>(std::ceil(reach / kStepMeters)) + 1;
    const int side = candidatesPerSide + 2 * border;
    const auto coordinate = [&](int index) { return -searchRadiusMeters + (index - border) * kStepMeters; };
    const auto cellOf = [side](int column, int row) {
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(side) + static_cast<std::size_t>(column);
    };

    const std::size_t    cellCount = static_cast<std::size_t>(side) * static_cast<std::size_t>(side);
    std::vector<Surface> surface(cellCount, Surface::Land);
    std::vector<double>  lakeSquared(cellCount, kNoWaterSquared);
    std::vector<double>  oceanSquared(cellCount, kNoWaterSquared);
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            const PlanetSampler::PositionSample sample = sampler.sampleAt(coordinate(column), coordinate(row));
            if (!sample.water) continue;
            const std::size_t cell = cellOf(column, row);
            if (sample.weights[0].biome == Biome::Lake) {
                surface[cell] = Surface::Lake;
                lakeSquared[cell] = 0.0;
            } else {
                surface[cell] = Surface::Ocean;
                oceanSquared[cell] = 0.0;
            }
        }
    }
    squaredDistanceTransform(lakeSquared, side);
    squaredDistanceTransform(oceanSquared, side);

    // The shore lies between the nearest water sample and the dry sample before it, so its edge is taken
    // half a step short of that sample.
    const auto shoreEdgeDistance = [](double squaredSteps) {
        if (squaredSteps >= 0.5 * kNoWaterSquared) return std::numeric_limits<double>::max();
        return foundation::det_math::sqrt(squaredSteps) * kStepMeters - 0.5 * kStepMeters;
    };

    // Indexed by SpawnWater minus one: river, lake, ocean.
    std::array<Choice, 3> atDistance{};
    std::array<Choice, 3> farthest{};
    for (int row = border; row < border + candidatesPerSide; ++row) {
        const double y = coordinate(row);
        for (int column = border; column < border + candidatesPerSide; ++column) {
            const double      x = coordinate(column);
            const std::size_t cell = cellOf(column, row);
            if (surface[cell] != Surface::Land) continue;
            const double channel = channels.empty() ? std::numeric_limits<double>::max() : channelEdgeDistance(x, y);
            if (channel <= 0.0) continue;

            SpawnWater nearest = SpawnWater::River;
            double     distance = channel;
            if (const double lake = shoreEdgeDistance(lakeSquared[cell]); lake < distance) {
                nearest = SpawnWater::Lake;
                distance = lake;
            }
            if (const double ocean = shoreEdgeDistance(oceanSquared[cell]); ocean < distance) {
                nearest = SpawnWater::Ocean;
                distance = ocean;
            }
            if (distance > reach) continue;

            const double originDistance = foundation::det_math::sqrt(x * x + y * y);
            const Choice candidate{{x, y, nearest, static_cast<float>(distance)}, distance, originDistance, true};
            const auto   kind = static_cast<std::size_t>(nearest) - 1;
            if (distance >= kLandingWaterDistanceMeters - kLandingWaterDistanceToleranceMeters) {
                Choice& best = atDistance[kind];
                if (!best.found || originDistance < best.originDistance) best = candidate;
            } else {
                Choice& best = farthest[kind];
                if (!best.found || distance > best.waterDistance ||
                    (distance == best.waterDistance && originDistance < best.originDistance)) {
                    best = candidate;
                }
            }
        }
    }

    for (const Choice& choice : atDistance) {
        if (choice.found) return choice.site;
    }
    for (const Choice& choice : farthest) {
        if (choice.found) return choice.site;
    }
    return SpawnSite{};
}

} // namespace worldgen
